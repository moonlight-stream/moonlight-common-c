#include "Platform.h"
#include "Limelight-internal.h"
#include "LinkedBlockingQueue.h"
#include "Video.h"

static PLENTRY nalChainHead;
static int nalChainDataLength;
static int decodingAvc;

static int nextFrameNumber = 1;
static int nextPacketNumber;
static int startFrameNumber = 1;
static int waitingForNextSuccessfulFrame;
static int gotNextFrameStart;
static int lastPacketInStream = 0;

static LINKED_BLOCKING_QUEUE decodeUnitQueue;
static int packetSize;

static unsigned short lastSequenceNumber;

typedef struct _BUFFER_DESC {
	char* data;
	unsigned int offset;
	unsigned int length;
} BUFFER_DESC, *PBUFFER_DESC;

void initializeVideoDepacketizer(int pktSize) {
	LbqInitializeLinkedBlockingQueue(&decodeUnitQueue, 15);
	packetSize = pktSize;
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
		nextEntry = entry->flink;
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

static void reassembleFrame(int frameNumber) {
	if (nalChainHead != NULL) {
		PDECODE_UNIT du = (PDECODE_UNIT) malloc(sizeof(*du));
		if (du != NULL) {
			du->bufferList = nalChainHead;
			du->fullLength = nalChainDataLength;

			nalChainHead = NULL;
			nalChainDataLength = 0;

			if (LbqOfferQueueItem(&decodeUnitQueue, du) == LBQ_BOUND_EXCEEDED) {
				Limelog("Decode unit queue overflow\n");

				nalChainHead = du->bufferList;
				nalChainDataLength = du->fullLength;
				free(du);

				clearAvcNalState();

				// FIXME: Get proper lower bound
				connectionSinkTooSlow(0, frameNumber);
			}

			// Notify the control connection
			connectionReceivedFrame(frameNumber);
		}
	}
}

int LiGetNextDecodeUnit(PDECODE_UNIT *du) {
	int err = LbqWaitForQueueElement(&decodeUnitQueue, (void**)du);
	if (err == LBQ_SUCCESS) {
		return 1;
	}
	else {
		return 0;
	}
}

int LiPollNextDecodeUnit(PDECODE_UNIT *du) {
	int err = LbqPollQueueElement(&decodeUnitQueue, (void**) du);
	if (err == LBQ_SUCCESS) {
		return 1;
	}
	else {
		LC_ASSERT(err == LBQ_EMPTY);
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

void queueFragment(char *data, int offset, int length) {
	PLENTRY entry = (PLENTRY) malloc(sizeof(*entry));
	if (entry != NULL) {
		entry->next = NULL;
		entry->length = length;
		entry->data = (char*) malloc(entry->length);
		if (entry->data == NULL) {
			free(entry);
			return;
		}

		memcpy(entry->data, &data[offset], entry->length);

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

void processRtpPayloadSlow(PNV_VIDEO_PACKET videoPacket, PBUFFER_DESC currentPos) {
	BUFFER_DESC specialSeq;

	while (currentPos->length != 0) {
		int start = currentPos->offset;

		if (getSpecialSeq(currentPos, &specialSeq)) {
			if (isSeqAvcStart(&specialSeq)) {
				decodingAvc = 1;

				if (isSeqFrameStart(&specialSeq)) {
					reassembleFrame(videoPacket->frameIndex);
				}

				currentPos->length -= specialSeq.length;
				currentPos->offset += specialSeq.length;
			}
			else {
				if (decodingAvc && isSeqPadding(currentPos)) {
					reassembleFrame(videoPacket->frameIndex);
				}

				decodingAvc = 0;

				currentPos->length--;
				currentPos->offset++;
			}
		}

		while (currentPos->length != 0) {
			if (getSpecialSeq(currentPos, &specialSeq)) {
				if (decodingAvc || !isSeqPadding(&specialSeq)) {
					break;
				}
			}

			currentPos->offset++;
			currentPos->length--;
		}

		if (decodingAvc) {
			queueFragment(currentPos->data, start, currentPos->offset - start);
		}
	}
}

void processRtpPayloadFast(PNV_VIDEO_PACKET videoPacket, BUFFER_DESC location) {
	queueFragment(location.data, location.offset, location.length);
}

void processRtpPayload(PNV_VIDEO_PACKET videoPacket, int length) {
	BUFFER_DESC currentPos, specialSeq;
	int isFirstPacket;
	int streamPacketIndex;
	
	currentPos.data = (char*) (videoPacket + 1);
	currentPos.offset = 0;
	currentPos.length = length - sizeof(*videoPacket);

	if (currentPos.length < packetSize - sizeof(NV_VIDEO_PACKET)) {
		processRtpPayloadSlow(videoPacket, &currentPos);
		return;
	}

	// We can use FEC to correct single packet errors
	// on single packet frames because we just get a
	// duplicate of the original packet
	if (videoPacket->totalPackets == 1 &&
		videoPacket->packetIndex == 1 &&
		nextPacketNumber == 0 &&
		videoPacket->frameIndex == nextFrameNumber) {
		Limelog("Using FEC for error correction\n");
		nextPacketNumber = 1;
	}
	// Discard the rest of the FEC data until we know how to use it
	else if (videoPacket->packetIndex >= videoPacket->totalPackets) {
		return;
	}

	// Check that this is the next frame
	isFirstPacket = (videoPacket->flags & FLAG_SOF) != 0;
	if (videoPacket->frameIndex > nextFrameNumber) {
		// Nope, but we can still work with it if it's
		// the start of the next frame
		if (isFirstPacket) {
			Limelog("Got start of frame %d when expecting %d of frame %d\n",
				videoPacket->frameIndex, nextPacketNumber, nextFrameNumber);

			nextFrameNumber = videoPacket->frameIndex;
			nextPacketNumber = 0;
			clearAvcNalState();

			// Tell the encoder when we're done decoding this frame
			// that we lost some previous frames
			waitingForNextSuccessfulFrame = 1;
			gotNextFrameStart = 0;
		}
		else {
			Limelog("Got packet %d of frame %d when expecting packet %d of frame %d\n",
				videoPacket->packetIndex, videoPacket->frameIndex,
				nextPacketNumber, nextFrameNumber);

			// We dropped the start of this frame too
			waitingForNextSuccessfulFrame = 1;
			gotNextFrameStart = 0;

			// Try to pickup on the next frame
			nextFrameNumber = videoPacket->frameIndex + 1;
			nextPacketNumber = 0;
			clearAvcNalState();
			return;
		}
	}
	else if (videoPacket->frameIndex < nextFrameNumber) {
		Limelog("Frame %d is behind our current frame number %d\n",
			videoPacket->frameIndex, nextFrameNumber);
		return;
	}

	// We know it's the right frame, now check the packet number
	if (videoPacket->packetIndex != nextPacketNumber) {
		Limelog("Frame %d: expected packet %d but got %d\n",
			videoPacket->frameIndex, nextPacketNumber, videoPacket->packetIndex);

		// At this point, we're guaranteed that it's not FEC data that we lost
		waitingForNextSuccessfulFrame = 1;
		gotNextFrameStart = 0;

		// Skip this frame
		nextFrameNumber++;
		nextPacketNumber = 0;
		clearAvcNalState();
		return;
	}

	if (waitingForNextSuccessfulFrame) {
		if (!gotNextFrameStart) {
			if (!isFirstPacket) {
				// We're waiting for the next frame, but this one is a fragment of a frame
				// so we must discard it and wait for the next one
				Limelog("Expected start of frame %d\n", videoPacket->frameIndex);

				nextFrameNumber = videoPacket->frameIndex;
				nextPacketNumber = 0;
				clearAvcNalState();
				return;
			} {
				gotNextFrameStart = 1;
			}
		}
	}

	streamPacketIndex = videoPacket->streamPacketIndex;
	if (streamPacketIndex != (int) (lastPacketInStream + 1)) {
		// Packets were lost so report this to the server
		connectionLostPackets(lastPacketInStream, streamPacketIndex);
	}
	lastPacketInStream = streamPacketIndex;

	nextPacketNumber++;

	// Remove extra padding
	currentPos.length = videoPacket->payloadLength;

	if (isFirstPacket) {
		if (getSpecialSeq(&currentPos, &specialSeq) &&
			isSeqFrameStart(&specialSeq) &&
			specialSeq.data[specialSeq.offset+specialSeq.length] == 0x67) {
			// SPS and PPS prefix is padded between NALs, so we must decode it with the slow path
			clearAvcNalState();
			processRtpPayloadSlow(videoPacket, &currentPos);
			return;
		}
	}

	processRtpPayloadFast(videoPacket, currentPos);

	// We can't use the EOF flag here because real frames can be split across
	// multiple "frames" when packetized to fit under the bandwidth ceiling
	if (videoPacket->packetIndex + 1 >= videoPacket->totalPackets) {
		nextFrameNumber++;
		nextPacketNumber = 0;
	}

	if (videoPacket->flags & FLAG_EOF) {
		reassembleFrame(videoPacket->frameIndex);

		if (waitingForNextSuccessfulFrame) {
			// This is the next successful frame after a loss event
			connectionDetectedFrameLoss(startFrameNumber, nextFrameNumber - 1);
			waitingForNextSuccessfulFrame = 0;
		}

		startFrameNumber = nextFrameNumber;
	}
}

void queueRtpPacket(PRTP_PACKET rtpPacket, int length) {
	processRtpPayload((PNV_VIDEO_PACKET) (rtpPacket + 1), length - sizeof(*rtpPacket));
}
