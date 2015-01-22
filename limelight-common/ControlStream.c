#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

#include "ByteBuffer.h"

/* NV control stream packet header */
typedef struct _NVCTL_PACKET_HEADER {
	unsigned short type;
	unsigned short payloadLength;
} NVCTL_PACKET_HEADER, *PNVCTL_PACKET_HEADER;

static IP_ADDRESS host;
static SOCKET ctlSock = INVALID_SOCKET;
static STREAM_CONFIGURATION streamConfig;
static PLT_THREAD lossStatsThread;
static PLT_THREAD resyncThread;
static PLT_EVENT resyncEvent;
static PCONNECTION_LISTENER_CALLBACKS listenerCallbacks;
static int lossCountSinceLastReport = 0;
static long currentFrame = 0;

#define PTYPE_START_STREAM_A 0x0606
#define PPAYLEN_START_STREAM_A 2
static const char PPAYLOAD_START_STREAM_A[PPAYLEN_START_STREAM_A] = { 0, 0 };

#define PTYPE_START_STREAM_B 0x0609
#define PPAYLEN_START_STREAM_B 1
static const char PPAYLOAD_START_STREAM_B[PPAYLEN_START_STREAM_B] = { 0 };

#define PTYPE_RESYNC 0x0604
#define PPAYLEN_RESYNC 24

#define PTYPE_LOSS_STATS 0x060a
#define PPAYLEN_LOSS_STATS 32

#define PTYPE_FRAME_STATS 0x0611
#define PPAYLEN_FRAME_STATS 64

#define LOSS_REPORT_INTERVAL_MS 50

/* Initializes the control stream */
int initializeControlStream(IP_ADDRESS addr, PSTREAM_CONFIGURATION streamConfigPtr, PCONNECTION_LISTENER_CALLBACKS clCallbacks) {
	memcpy(&streamConfig, streamConfigPtr, sizeof(*streamConfigPtr));

	PltCreateEvent(&resyncEvent);

	host = addr;
	listenerCallbacks = clCallbacks;

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
	char lossStatsPayload[PPAYLEN_LOSS_STATS];
	BYTE_BUFFER byteBuffer;

	while (!PltIsThreadInterrupted(&lossStatsThread)) {
		// Construct the payload
		BbInitializeWrappedBuffer(&byteBuffer, lossStatsPayload, 0, PPAYLEN_LOSS_STATS, BYTE_ORDER_LITTLE);
		BbPutInt(&byteBuffer, lossCountSinceLastReport);
		BbPutInt(&byteBuffer, LOSS_REPORT_INTERVAL_MS);
		BbPutInt(&byteBuffer, 1000);
		BbPutLong(&byteBuffer, currentFrame);
		BbPutInt(&byteBuffer, 0);
		BbPutInt(&byteBuffer, 0);
		BbPutInt(&byteBuffer, 0x14);

		// Send the message (and don't expect a response)
		if (!sendMessageAndForget(PTYPE_LOSS_STATS,
			PPAYLEN_LOSS_STATS, lossStatsPayload)) {
			Limelog("Loss stats thread terminating #1\n");
            listenerCallbacks->connectionTerminated(LastSocketError());
			return;
		}

		// Clear the transient state
		lossCountSinceLastReport = 0;

		// Wait a bit
		PltSleepMs(LOSS_REPORT_INTERVAL_MS);
	}
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
		if (!sendMessageAndDiscardReply(PTYPE_RESYNC, PPAYLEN_RESYNC, payload)) {
			Limelog("Resync thread terminating #1\n");
			listenerCallbacks->connectionTerminated(LastSocketError());
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

	ctlSock = connectTcpSocket(host, 47995);
	if (ctlSock == INVALID_SOCKET) {
		return LastSocketError();
	}

	enableNoDelay(ctlSock);

	// Send START A
	if (!sendMessageAndDiscardReply(PTYPE_START_STREAM_A,
                                    PPAYLEN_START_STREAM_A,
                                    PPAYLOAD_START_STREAM_A)) {
        return LastSocketError();
    }

	// Send START B
	if (!sendMessageAndDiscardReply(PTYPE_START_STREAM_B,
                                    PPAYLEN_START_STREAM_B,
                                    PPAYLOAD_START_STREAM_B)) {
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