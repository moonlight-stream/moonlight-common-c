#include "Platform.h"
#include "Limelight-internal.h"
#include "LinkedBlockingQueue.h"
#include "Video.h"

PLENTRY nalChainHead;
int nalChainDataLength;
int decodingAvc;

LINKED_BLOCKING_QUEUE decodeUnitQueue;

unsigned short lastSequenceNumber;

typedef struct _BUFFER_DESC {
	char* data;
	int offset;
	int length;
} BUFFER_DESC, *PBUFFER_DESC;

void initializeVideoDepacketizer(void) {
	LbqInitializeLinkedBlockingQueue(&decodeUnitQueue, 15);
}

static void clearAvcNalState(void) {
	PLENTRY lastEntry;

	while (nalChainHead != NULL) {
		lastEntry = nalChainHead;
		nalChainHead = lastEntry->next;
		free(lastEntry->data);
		free(lastEntry);
	}

	nalChainDataLength = 0;
}

void destroyVideoDepacketizer(void) {
	PLINKED_BLOCKING_QUEUE_ENTRY entry, nextEntry;
	
	entry = LbqDestroyLinkedBlockingQueue(&decodeUnitQueue);
	while (entry != NULL) {
		nextEntry = entry->next;
		free(entry->data);
		free(entry);
		entry = nextEntry;
	}

	clearAvcNalState();
}

static int isSeqFrameStart(PBUFFER_DESC candidate) {
	return (candidate->length == 4 && candidate->data[candidate->offset + candidate->length - 1] == 1);
}

static int isSeqAvcStart(PBUFFER_DESC candidate) {
	return (candidate->data[candidate->offset + candidate->length - 1] == 1);
}

static int isSeqPadding(PBUFFER_DESC candidate) {
	return (candidate->data[candidate->offset + candidate->length - 1] == 0);
}

static int getSpecialSeq(PBUFFER_DESC current, PBUFFER_DESC candidate) {
	if (current->length < 3) {
		return 0;
	}

	if (current->data[current->offset] == 0 &&
		current->data[current->offset + 1] == 0) {
		// Padding or frame start
		if (current->data[current->offset + 2] == 0) {
			if (current->length >= 4 && current->data[current->offset + 3] == 1) {
				// Frame start
				candidate->data = current->data;
				candidate->offset = current->offset;
				candidate->length = 4;
				return 1;
			}
			else {
				// Padding
				candidate->data = current->data;
				candidate->offset = current->offset;
				candidate->length = 3;
				return 1;
			}
		}
		else if (current->data[current->offset + 2] == 1) {
			// NAL start
			candidate->data = current->data;
			candidate->offset = current->offset;
			candidate->length = 3;
			return 1;
		}
	}

	return 0;
}

static void reassembleFrame(void) {
	if (nalChainHead != NULL) {
		PDECODE_UNIT du = (PDECODE_UNIT) malloc(sizeof(*du));
		if (du != NULL) {
			du->bufferList = nalChainHead;
			du->fullLength = nalChainDataLength;

			nalChainHead = NULL;
			nalChainDataLength = 0;

			if (!LbqOfferQueueItem(&decodeUnitQueue, du)) {
				nalChainHead = du->bufferList;
				nalChainDataLength = du->fullLength;
				free(du);

				clearAvcNalState();

				requestIdrFrame();
			}
		}
	}
}

int getNextDecodeUnit(PDECODE_UNIT *du) {
	int err = LbqWaitForQueueElement(&decodeUnitQueue, (void**)du);
	if (err == LBQ_SUCCESS) {
		return 1;
	}
	else {
		return 0;
	}
}

void freeDecodeUnit(PDECODE_UNIT decodeUnit) {
	PLENTRY lastEntry;

	while (decodeUnit->bufferList != NULL) {
		lastEntry = decodeUnit->bufferList;
		decodeUnit->bufferList = lastEntry->next;
		free(lastEntry->data);
		free(lastEntry);
	}

	free(decodeUnit);
}

void processRtpPayload(PNV_VIDEO_PACKET videoPacket, int length) {
	BUFFER_DESC currentPos, specialSeq;
	
	currentPos.data = (char*) (videoPacket + 1);
	currentPos.offset = 0;
	currentPos.length = length - sizeof(*videoPacket);

	if (currentPos.length == 968) {
		if (videoPacket->packetIndex < videoPacket->totalPackets) {
			currentPos.length = videoPacket->payloadLength;
		}
		else {
			return;
		}
	}

	while (currentPos.length != 0) {
		int start = currentPos.offset;

		if (getSpecialSeq(&currentPos, &specialSeq)) {
			if (isSeqAvcStart(&specialSeq)) {
				if (isSeqFrameStart(&specialSeq)) {
					decodingAvc = 1;

					reassembleFrame();
				}

				currentPos.length -= specialSeq.length;
				currentPos.offset += specialSeq.length;
			}
			else {
				if (decodingAvc && isSeqPadding(&currentPos)) {
					reassembleFrame();
				}

				decodingAvc = 0;

				currentPos.length--;
				currentPos.offset++;
			}
		}

		while (currentPos.length != 0) {
			if (getSpecialSeq(&currentPos, &specialSeq)) {
				if (decodingAvc || !isSeqPadding(&specialSeq)) {
					break;
				}
			}

			currentPos.offset++;
			currentPos.length--;
		}

		if (decodingAvc) {
			PLENTRY entry = (PLENTRY) malloc(sizeof(*entry));
			if (entry != NULL) {
				entry->next = NULL;
				entry->length = currentPos.offset - start;
				entry->data = (char*) malloc(entry->length);
				if (entry->data == NULL) {
					free(entry);
					return;
				}

				memcpy(entry->data, &currentPos.data[start], entry->length);
				
				nalChainDataLength += entry->length;

				if (nalChainHead == NULL) {
					nalChainHead = entry;
				}
				else {
					PLENTRY currentEntry = nalChainHead;

					while (currentEntry->next != NULL) {
						currentEntry = currentEntry->next;
					}

					currentEntry->next = entry;
				}
			}
		}
	}
}

void queueRtpPacket(PRTP_PACKET rtpPacket, int length) {

	rtpPacket->sequenceNumber = htons(rtpPacket->sequenceNumber);

	if (lastSequenceNumber != 0 &&
		(unsigned short) (lastSequenceNumber + 1) != rtpPacket->sequenceNumber) {
		Limelog("Received OOS video data (expected %d, but got %d)\n", lastSequenceNumber + 1, rtpPacket->sequenceNumber);

		clearAvcNalState();

		requestIdrFrame();
	}

	lastSequenceNumber = rtpPacket->sequenceNumber;

	processRtpPayload((PNV_VIDEO_PACKET) (rtpPacket + 1), length - sizeof(*rtpPacket));
}

