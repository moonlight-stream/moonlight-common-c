#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

#include "ByteBuffer.h"

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

#define PTYPE_START_STREAM_A 0x140b
#define PPAYLEN_START_STREAM_A 1
static const char PPAYLOAD_START_STREAM_A[1] = { 0 };

#define PTYPE_START_STREAM_B 0x1410
#define PPAYLEN_START_STREAM_B 16
static const int PPAYLOAD_START_STREAM_B[4] = { 0, 0, 0, 0xa }; // FIXME: Little endian

#define PTYPE_RESYNC 0x1404
#define PPAYLEN_RESYNC 24

#define PTYPE_LOSS_STATS 0x140c
#define PPAYLEN_LOSS_STATS 32

#define PTYPE_FRAME_STATS 0x1417
#define PPAYLEN_FRAME_STATS 64

#define LOSS_REPORT_INTERVAL_MS 50

int initializeControlStream(IP_ADDRESS addr, PSTREAM_CONFIGURATION streamConfigPtr, PCONNECTION_LISTENER_CALLBACKS clCallbacks) {
	memcpy(&streamConfig, streamConfigPtr, sizeof(*streamConfigPtr));

	PltCreateEvent(&resyncEvent);

	host = addr;
	listenerCallbacks = clCallbacks;

	return 0;
}

void destroyControlStream(void) {
	PltCloseEvent(&resyncEvent);
}

void connectionSinkTooSlow(int startFrame, int endFrame) {
	// FIXME: Send ranges
	PltSetEvent(&resyncEvent);
}

void connectionDetectedFrameLoss(int startFrame, int endFrame) {
	// FIXME: Send ranges
	PltSetEvent(&resyncEvent);
}

void connectionReceivedFrame(int frameIndex) {
	currentFrame = frameIndex;
}

void connectionLostPackets(int lastReceivedPacket, int nextReceivedPacket) {
	lossCountSinceLastReport += (nextReceivedPacket - lastReceivedPacket) - 1;
}

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
	NVCTL_PACKET_HEADER header;
	SOCK_RET err;

	header.type = ptype;
	header.payloadLength = paylen;
	err = send(ctlSock, (char*) &header, sizeof(header), 0);
	if (err != sizeof(header)) {
		return 0;
	}

	if (payload != NULL) {
		err = send(ctlSock, payload, paylen, 0);
		if (err != paylen) {
			return 0;
		}
	}

	return 1;
}

static PNVCTL_PACKET_HEADER sendMessage(short ptype, short paylen, const void* payload) {
	int success;

	success = sendMessageAndForget(ptype, paylen, payload);
	if (!success) {
		return NULL;
	}

	return readNvctlPacket();
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
	PNVCTL_PACKET_HEADER response;

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
		response = sendMessage(PTYPE_RESYNC, PPAYLEN_RESYNC, payload);
		if (response == NULL) {
			Limelog("Resync thread terminating #1\n");
			listenerCallbacks->connectionTerminated(LastSocketError());
			return;
		}
		Limelog("Resync complete\n");
	}
}

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

int startControlStream(void) {
	int err;
	PNVCTL_PACKET_HEADER response;

	ctlSock = connectTcpSocket(host, 47995);
	if (ctlSock == INVALID_SOCKET) {
		return LastSocketError();
	}

	enableNoDelay(ctlSock);

	// Send START A
	response = sendMessage(PTYPE_START_STREAM_A,
		PPAYLEN_START_STREAM_A, PPAYLOAD_START_STREAM_A);
	if (response == NULL) {
		return LastSocketError();
	}

	// Send START B
	response = sendMessage(PTYPE_START_STREAM_B,
		PPAYLEN_START_STREAM_B, PPAYLOAD_START_STREAM_B);
	if (response == NULL) {
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