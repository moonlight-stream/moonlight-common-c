#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

#include "ByteBuffer.h"

/* NV control stream packet header */
typedef struct _NVCTL_PACKET_HEADER {
	unsigned short type;
	unsigned short payloadLength;
} NVCTL_PACKET_HEADER, *PNVCTL_PACKET_HEADER;

typedef struct _QUEUED_FRAME_INVALIDATION_TUPLE {
	int startFrame;
	int endFrame;
	LINKED_BLOCKING_QUEUE_ENTRY entry;
} QUEUED_FRAME_INVALIDATION_TUPLE, *PQUEUED_FRAME_INVALIDATION_TUPLE;

static SOCKET ctlSock = INVALID_SOCKET;
static PLT_THREAD lossStatsThread;
static PLT_THREAD invalidateRefFramesThread;
static PLT_EVENT invalidateRefFramesEvent;
static int lossCountSinceLastReport;
static long currentFrame;

static int idrFrameRequired;
static LINKED_BLOCKING_QUEUE invalidReferenceFrameTuples;

#define IDX_START_A 0
#define IDX_REQUEST_IDR_FRAME 0
#define IDX_START_B 1
#define IDX_INVALIDATE_REF_FRAMES 2
#define IDX_LOSS_STATS 3

static const short packetTypesGen3[] = {
    0x140b, // Start A
    0x1410, // Start B
    0x1404, // Invalidate reference frames
    0x140c, // Loss Stats
    0x1417, // Frame Stats (unused)
};
static const short packetTypesGen4[] = {
    0x0606, // Request IDR frame
    0x0609, // Start B
    0x0604, // Invalidate reference frames
    0x060a, // Loss Stats
    0x0611, // Frame Stats (unused)
};

static const char startAGen3[] = {0};
static const int startBGen3[] = {0, 0, 0, 0xa};

static const char requestIdrFrameGen4[] = {0, 0};
static const char startBGen4[] = {0};

static const short payloadLengthsGen3[] = {
    sizeof(startAGen3), // Start A
    sizeof(startBGen3), // Start B
    24, // Invalidate reference frames
    32, // Loss Stats
    64, // Frame Stats
};
static const short payloadLengthsGen4[] = {
    sizeof(requestIdrFrameGen4), // Request IDR frame
    sizeof(startBGen4), // Start B
    24, // Invalidate reference frames
    32, // Loss Stats
    64, // Frame Stats
};

static const char* preconstructedPayloadsGen3[] = {
    startAGen3,
    (char*)startBGen3
};
static const char* preconstructedPayloadsGen4[] = {
    requestIdrFrameGen4,
    startBGen4
};

static short *packetTypes;
static short *payloadLengths;
static char **preconstructedPayloads;

#define LOSS_REPORT_INTERVAL_MS 50

/* Initializes the control stream */
int initializeControlStream(void) {
	PltCreateEvent(&invalidateRefFramesEvent);
	LbqInitializeLinkedBlockingQueue(&invalidReferenceFrameTuples, 20);
    
    if (ServerMajorVersion == 3) {
        packetTypes = (short*)packetTypesGen3;
        payloadLengths = (short*)payloadLengthsGen3;
        preconstructedPayloads = (char**)preconstructedPayloadsGen3;
    }
    else {
        packetTypes = (short*)packetTypesGen4;
        payloadLengths = (short*)payloadLengthsGen4;
        preconstructedPayloads = (char**)preconstructedPayloadsGen4;
    }

    idrFrameRequired = 0;
    currentFrame = 0;
    lossCountSinceLastReport = 0;

	return 0;
}

void freeFrameInvalidationList(PLINKED_BLOCKING_QUEUE_ENTRY entry) {
	PLINKED_BLOCKING_QUEUE_ENTRY nextEntry;

	while (entry != NULL) {
		nextEntry = entry->flink;
		free(entry->data);
		entry = nextEntry;
	}
}

/* Cleans up control stream */
void destroyControlStream(void) {
    PltCloseEvent(&invalidateRefFramesEvent);
    freeFrameInvalidationList(LbqDestroyLinkedBlockingQueue(&invalidReferenceFrameTuples));
}

int getNextFrameInvalidationTuple(PQUEUED_FRAME_INVALIDATION_TUPLE *qfit) {
	int err = LbqPollQueueElement(&invalidReferenceFrameTuples, (void**) qfit);
	return (err == LBQ_SUCCESS);
}

void queueFrameInvalidationTuple(int startFrame, int endFrame) {

    if (VideoCallbacks.capabilities & CAPABILITY_REFERENCE_FRAME_INVALIDATION) {
        PQUEUED_FRAME_INVALIDATION_TUPLE qfit;
        qfit = malloc(sizeof(*qfit));
        if (qfit != NULL) {
            qfit->startFrame = startFrame;
            qfit->endFrame = endFrame;
            if (LbqOfferQueueItem(&invalidReferenceFrameTuples, qfit, &qfit->entry) == LBQ_BOUND_EXCEEDED) {
                // Too many invalidation tuples, so we need an IDR frame now
                free(qfit);
                idrFrameRequired = 1;
            }
        }
        else {
            idrFrameRequired = 1;
        }
    }
    else {
        idrFrameRequired = 1;
    }

    PltSetEvent(&invalidateRefFramesEvent);
}

/* Request an IDR frame on demand by the decoder */
void requestIdrOnDemand(void) {
    idrFrameRequired = 1;
    PltSetEvent(&invalidateRefFramesEvent);
}

/* Invalidate reference frames if the decoder is too slow */
void connectionSinkTooSlow(int startFrame, int endFrame) {
    queueFrameInvalidationTuple(startFrame, endFrame);
}

/* Invalidate reference frames lost by the network */
void connectionDetectedFrameLoss(int startFrame, int endFrame) {
    queueFrameInvalidationTuple(startFrame, endFrame);
}

/* When we receive a frame, update the number of our current frame */
void connectionReceivedFrame(int frameIndex) {
	currentFrame = frameIndex;
}

/* When we lose packets, update our packet loss count */
void connectionLostPackets(int lastReceivedPacket, int nextReceivedPacket) {
	lossCountSinceLastReport += (nextReceivedPacket - lastReceivedPacket) - 1;
}

/* Reads an NV control stream packet */
static PNVCTL_PACKET_HEADER readNvctlPacket(void) {
	NVCTL_PACKET_HEADER staticHeader;
	PNVCTL_PACKET_HEADER fullPacket;
	SOCK_RET err;

	err = recv(ctlSock, (char*) &staticHeader, sizeof(staticHeader), 0);
	if (err != sizeof(staticHeader)) {
		return NULL;
	}

	fullPacket = (PNVCTL_PACKET_HEADER) malloc(staticHeader.payloadLength + sizeof(staticHeader));
	if (fullPacket == NULL) {
		return NULL;
	}

	memcpy(fullPacket, &staticHeader, sizeof(staticHeader));
	if (staticHeader.payloadLength != 0) {
		err = recv(ctlSock, (char*) (fullPacket + 1), staticHeader.payloadLength, 0);
		if (err != staticHeader.payloadLength) {
			free(fullPacket);
			return NULL;
		}
	}

	return fullPacket;
}

static int sendMessageAndForget(short ptype, short paylen, const void* payload) {
	PNVCTL_PACKET_HEADER packet;
	SOCK_RET err;

	packet = malloc(sizeof(*packet) + paylen);
	if (packet == NULL) {
		return 0;
	}

	packet->type = ptype;
	packet->payloadLength = paylen;
	memcpy(&packet[1], payload, paylen);

	err = send(ctlSock, (char*) packet, sizeof(*packet) + paylen, 0);
	free(packet);

	if (err != sizeof(*packet) + paylen) {
		return 0;
	}

	return 1;
}

static PNVCTL_PACKET_HEADER sendMessage(short ptype, short paylen, const void* payload) {
    if (!sendMessageAndForget(ptype, paylen, payload)) {
        return NULL;
    }

	return readNvctlPacket();
}

static int sendMessageAndDiscardReply(short ptype, short paylen, const void* payload) {
    PNVCTL_PACKET_HEADER reply;
    
    reply = sendMessage(ptype, paylen, payload);
    if (reply == NULL) {
        return 0;
    }
    
    free(reply);
    return 1;
}

static void lossStatsThreadFunc(void* context) {
	char *lossStatsPayload;
	BYTE_BUFFER byteBuffer;

	lossStatsPayload = malloc(payloadLengths[IDX_LOSS_STATS]);
	if (lossStatsPayload == NULL) {
		Limelog("Loss Stats: malloc() failed\n");
		ListenerCallbacks.connectionTerminated(-1);
		return;
	}

	while (!PltIsThreadInterrupted(&lossStatsThread)) {
		// Construct the payload
		BbInitializeWrappedBuffer(&byteBuffer, lossStatsPayload, 0, payloadLengths[IDX_LOSS_STATS], BYTE_ORDER_LITTLE);
		BbPutInt(&byteBuffer, lossCountSinceLastReport);
		BbPutInt(&byteBuffer, LOSS_REPORT_INTERVAL_MS);
		BbPutInt(&byteBuffer, 1000);
		BbPutLong(&byteBuffer, currentFrame);
		BbPutInt(&byteBuffer, 0);
		BbPutInt(&byteBuffer, 0);
		BbPutInt(&byteBuffer, 0x14);

		// Send the message (and don't expect a response)
		if (!sendMessageAndForget(packetTypes[IDX_LOSS_STATS],
			payloadLengths[IDX_LOSS_STATS], lossStatsPayload)) {
			free(lossStatsPayload);
			Limelog("Loss Stats: Transaction failed: %d\n", (int)LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketError());
			return;
		}

		// Clear the transient state
		lossCountSinceLastReport = 0;

		// Wait a bit
		PltSleepMs(LOSS_REPORT_INTERVAL_MS);
	}

	free(lossStatsPayload);
}

static void requestIdrFrame(void) {
    long long payload[3];

    if (ServerMajorVersion == 3) {
        // Form the payload
        payload[0] = 0;
        payload[1] = 0xFFFFF;
        payload[2] = 0;

        // Send the reference frame invalidation request and read the response
        if (!sendMessageAndDiscardReply(packetTypes[IDX_INVALIDATE_REF_FRAMES],
            payloadLengths[IDX_INVALIDATE_REF_FRAMES], payload)) {
            Limelog("Request IDR Frame: Transaction failed: %d\n", (int) LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketError());
            return;
        }
    }
    else {
        // Send IDR frame request and read the response
        if (!sendMessageAndDiscardReply(packetTypes[IDX_REQUEST_IDR_FRAME],
            payloadLengths[IDX_REQUEST_IDR_FRAME], preconstructedPayloads[IDX_REQUEST_IDR_FRAME])) {
            Limelog("Request IDR Frame: Transaction failed: %d\n", (int) LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketError());
            return;
        }
    }

    Limelog("IDR frame request sent\n");
}

static void requestInvalidateReferenceFrames(void) {
    long long payload[3];
    PQUEUED_FRAME_INVALIDATION_TUPLE qfit;

    LC_ASSERT(VideoCallbacks.capabilities & CAPABILITY_REFERENCE_FRAME_INVALIDATION);

    if (!getNextFrameInvalidationTuple(&qfit)) {
        return;
    }

    LC_ASSERT(qfit->startFrame <= qfit->endFrame);

    payload[0] = qfit->startFrame;
    payload[1] = qfit->endFrame;
    payload[2] = 0;

    // Aggregate all lost frames into one range
    do {
        LC_ASSERT(qfit->endFrame >= payload[1]);
        payload[1] = qfit->endFrame;
        free(qfit);
    } while (getNextFrameInvalidationTuple(&qfit));

    // Send the reference frame invalidation request and read the response
    if (!sendMessageAndDiscardReply(packetTypes[IDX_INVALIDATE_REF_FRAMES],
        payloadLengths[IDX_INVALIDATE_REF_FRAMES], payload)) {
        Limelog("Request Invaldiate Reference Frames: Transaction failed: %d\n", (int) LastSocketError());
        ListenerCallbacks.connectionTerminated(LastSocketError());
        return;
    }

    Limelog("Invalidate reference frame request sent\n");
}

static void invalidateRefFramesFunc(void* context) {
    while (!PltIsThreadInterrupted(&invalidateRefFramesThread)) {
        // Wait for a request to invalidate reference frames
        PltWaitForEvent(&invalidateRefFramesEvent);
        PltClearEvent(&invalidateRefFramesEvent);

        // Sometimes we absolutely need an IDR frame
        if (idrFrameRequired) {
            // Empty invalidate reference frames tuples
            PQUEUED_FRAME_INVALIDATION_TUPLE qfit;
            while (getNextFrameInvalidationTuple(&qfit)) {
                free(qfit);
            }

            // Send an IDR frame request
            idrFrameRequired = 0;
            requestIdrFrame();
        } else {
            // Otherwise invalidate reference frames
            requestInvalidateReferenceFrames();
        }
    }
}

/* Stops the control stream */
int stopControlStream(void) {
	PltInterruptThread(&lossStatsThread);
	PltInterruptThread(&invalidateRefFramesThread);

	if (ctlSock != INVALID_SOCKET) {
		closesocket(ctlSock);
		ctlSock = INVALID_SOCKET;
	}

	PltJoinThread(&lossStatsThread);
	PltJoinThread(&invalidateRefFramesThread);

	PltCloseThread(&lossStatsThread);
	PltCloseThread(&invalidateRefFramesThread);

	return 0;
}

/* Starts the control stream */
int startControlStream(void) {
	int err;

	ctlSock = connectTcpSocket(&RemoteAddr, RemoteAddrLen, 47995);
	if (ctlSock == INVALID_SOCKET) {
		return LastSocketFail();
	}

	enableNoDelay(ctlSock);

	// Send START A
	if (!sendMessageAndDiscardReply(packetTypes[IDX_START_A],
                                    payloadLengths[IDX_START_A],
                                    preconstructedPayloads[IDX_START_A])) {
        Limelog("Start A failed: %d\n", (int)LastSocketError());
        return LastSocketFail();
    }

	// Send START B
    if (!sendMessageAndDiscardReply(packetTypes[IDX_START_B],
                                    payloadLengths[IDX_START_B],
                                    preconstructedPayloads[IDX_START_B])) {
        Limelog("Start B failed: %d\n", (int)LastSocketError());
        return LastSocketFail();
    }

	err = PltCreateThread(lossStatsThreadFunc, NULL, &lossStatsThread);
	if (err != 0) {
		return err;
	}

	err = PltCreateThread(invalidateRefFramesFunc, NULL, &invalidateRefFramesThread);
	if (err != 0) {
		return err;
	}

	return 0;
}