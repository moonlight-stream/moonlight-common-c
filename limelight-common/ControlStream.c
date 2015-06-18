#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

#include "ByteBuffer.h"

/* NV control stream packet header */
typedef struct _NVCTL_PACKET_HEADER {
	unsigned short type;
	unsigned short payloadLength;
} NVCTL_PACKET_HEADER, *PNVCTL_PACKET_HEADER;

static SOCKET ctlSock = INVALID_SOCKET;
static PLT_THREAD lossStatsThread;
static PLT_THREAD resyncThread;
static PLT_EVENT resyncEvent;
static int lossCountSinceLastReport = 0;
static long currentFrame = 0;

#define IDX_START_A 0
#define IDX_START_B 1
#define IDX_RESYNC 2
#define IDX_LOSS_STATS 3

static const short packetTypesGen3[] = {
    0x140b, // Start A
    0x1410, // Start B
    0x1404, // Resync
    0x140c, // Loss Stats
    0x1417, // Frame Stats (unused)
};
static const short packetTypesGen4[] = {
    0x0606, // Start A
    0x0609, // Start B
    0x0604, // Resync
    0x060a, // Loss Stats
    0x0611, // Frame Stats (unused)
};

static const char startAGen3[] = {0};
static const int startBGen3[] = {0, 0, 0, 0xa};

static const char startAGen4[] = {0, 0};
static const char startBGen4[] = {0};

static const short payloadLengthsGen3[] = {
    sizeof(startAGen3), // Start A
    sizeof(startBGen3), // Start B
    24, // Resync
    32, // Loss Stats
    64, // Frame Stats
};
static const short payloadLengthsGen4[] = {
    sizeof(startAGen4), // Start A
    sizeof(startBGen4), // Start B
    24, // Resync
    32, // Loss Stats
    64, // Frame Stats
};

static const char* preconstructedPayloadsGen3[] = {
    startAGen3,
    (char*)startBGen3
};
static const char* preconstructedPayloadsGen4[] = {
    startAGen4,
    startBGen4
};

static short *packetTypes;
static short *payloadLengths;
static char **preconstructedPayloads;

#define LOSS_REPORT_INTERVAL_MS 50

/* Initializes the control stream */
int initializeControlStream(void) {
	PltCreateEvent(&resyncEvent);
    
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

	return 0;
}

/* Cleans up control stream */
void destroyControlStream(void) {
	PltCloseEvent(&resyncEvent);
}

/* Resync on demand by the decoder */
void resyncOnDemand(void) {
    // FIXME: Send ranges
    PltSetEvent(&resyncEvent);
}

/* Resync if the connection is too slow */
void connectionSinkTooSlow(int startFrame, int endFrame) {
	// FIXME: Send ranges
	PltSetEvent(&resyncEvent);
}

/* Resync if we're losing frames */
void connectionDetectedFrameLoss(int startFrame, int endFrame) {
	// FIXME: Send ranges
	PltSetEvent(&resyncEvent);
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
		Limelog("Loss stats thread terminating #0\n");
		ListenerCallbacks.connectionTerminated(LastSocketError());
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
			Limelog("Loss stats thread terminating #1\n");
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

static void resyncThreadFunc(void* context) {
	long long payload[3];

	while (!PltIsThreadInterrupted(&resyncThread)) {
		// Wait for a resync request
		PltWaitForEvent(&resyncEvent);

		// Form the payload
		payload[0] = 0;
		payload[1] = 0xFFFFF;
		payload[2] = 0;

		// Done capturing the parameters
		PltClearEvent(&resyncEvent);

		// Send the resync request and read the response
		if (!sendMessageAndDiscardReply(packetTypes[IDX_RESYNC], payloadLengths[IDX_RESYNC], payload)) {
			Limelog("Resync thread terminating #1\n");
			ListenerCallbacks.connectionTerminated(LastSocketError());
			return;
		}
		Limelog("Resync complete\n");
	}
}

/* Stops the control stream */
int stopControlStream(void) {
	PltInterruptThread(&lossStatsThread);
	PltInterruptThread(&resyncThread);

	if (ctlSock != INVALID_SOCKET) {
		closesocket(ctlSock);
		ctlSock = INVALID_SOCKET;
	}

	PltJoinThread(&lossStatsThread);
	PltJoinThread(&resyncThread);

	PltCloseThread(&lossStatsThread);
	PltCloseThread(&resyncThread);

	return 0;
}

/* Starts the control stream */
int startControlStream(void) {
	int err;

	ctlSock = connectTcpSocket(&RemoteAddr, RemoteAddrLen, 47995);
	if (ctlSock == INVALID_SOCKET) {
		return LastSocketError();
	}

	enableNoDelay(ctlSock);

	// Send START A
	if (!sendMessageAndDiscardReply(packetTypes[IDX_START_A],
                                    payloadLengths[IDX_START_A],
                                    preconstructedPayloads[IDX_START_A])) {
        return LastSocketError();
    }

	// Send START B
    if (!sendMessageAndDiscardReply(packetTypes[IDX_START_B],
                                    payloadLengths[IDX_START_B],
                                    preconstructedPayloads[IDX_START_B])) {
        return LastSocketError();
    }

	err = PltCreateThread(lossStatsThreadFunc, NULL, &lossStatsThread);
	if (err != 0) {
		return err;
	}

	err = PltCreateThread(resyncThreadFunc, NULL, &resyncThread);
	if (err != 0) {
		return err;
	}

	return 0;
}