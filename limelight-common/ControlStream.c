#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

typedef struct _NVCTL_PACKET_HEADER {
	unsigned short type;
	unsigned short payloadLength;
} NVCTL_PACKET_HEADER, *PNVCTL_PACKET_HEADER;

static IP_ADDRESS host;
static SOCKET ctlSock = INVALID_SOCKET;
static STREAM_CONFIGURATION streamConfig;
static PLT_THREAD heartbeatThread;
static PLT_THREAD jitterThread;
static PLT_THREAD resyncThread;
static PLT_EVENT resyncEvent;
static PCONNECTION_LISTENER_CALLBACKS listenerCallbacks;

static const short PTYPE_KEEPALIVE = 0x13ff;
static const short PPAYLEN_KEEPALIVE = 0x0000;

static const short PTYPE_HEARTBEAT = 0x1401;
static const short PPAYLEN_HEARTBEAT = 0x0000;

static const short PTYPE_1405 = 0x1405;
static const short PPAYLEN_1405 = 0x0000;

static const short PTYPE_RESYNC = 0x1404;
static const short PPAYLEN_RESYNC = 16;

static const short PTYPE_JITTER = 0x140c;
static const short PPAYLEN_JITTER = 0x10;

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

static PNVCTL_PACKET_HEADER readNvctlPacket(void) {
	NVCTL_PACKET_HEADER staticHeader;
	PNVCTL_PACKET_HEADER fullPacket;
	int err;

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

static PNVCTL_PACKET_HEADER sendNoPayloadAndReceive(short ptype, short paylen) {
	NVCTL_PACKET_HEADER header;
	int err;

	header.type = ptype;
	header.payloadLength = paylen;
	err = send(ctlSock, (char*) &header, sizeof(header), 0);
	if (err != sizeof(header)) {
		return NULL;
	}

	return readNvctlPacket();
}

static void heartbeatThreadFunc(void* context) {
	int err;
	NVCTL_PACKET_HEADER header;

	header.type = PTYPE_HEARTBEAT;
	header.payloadLength = PPAYLEN_HEARTBEAT;
	while (!PltIsThreadInterrupted(&heartbeatThread)) {
		err = send(ctlSock, (char*) &header, sizeof(header), 0);
		if (err != sizeof(header)) {
			Limelog("Heartbeat thread terminating #1\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		PltSleepMs(3000);
	}
}

static void jitterThreadFunc(void* context) {
	int payload[4];
	NVCTL_PACKET_HEADER header;
	int err;

	header.type = PTYPE_JITTER;
	header.payloadLength = PPAYLEN_JITTER;
	while (!PltIsThreadInterrupted(&jitterThread)) {
		err = send(ctlSock, (char*) &header, sizeof(header), 0);
		if (err != sizeof(header)) {
			Limelog("Jitter thread terminating #1\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		payload[0] = 0;
		payload[1] = 77;
		payload[2] = 888;
		payload[3] = 0; // FIXME: Sequence number?

		err = send(ctlSock, (char*) payload, sizeof(payload), 0);
		if (err != sizeof(payload)) {
			Limelog("Jitter thread terminating #2\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		PltSleepMs(100);
	}
}

static void resyncThreadFunc(void* context) {
	long long payload[2];
	NVCTL_PACKET_HEADER header;
	PNVCTL_PACKET_HEADER response;
	int err;

	header.type = PTYPE_RESYNC;
	header.payloadLength = PPAYLEN_RESYNC;
	while (!PltIsThreadInterrupted(&resyncThread)) {
		PltWaitForEvent(&resyncEvent);

		err = send(ctlSock, (char*) &header, sizeof(header), 0);
		if (err != sizeof(header)) {
			Limelog("Resync thread terminating #1\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		payload[0] = 0;
		payload[1] = 0xFFFFF;

		Limelog("Sending resync packet\n");
		err = send(ctlSock, (char*) payload, sizeof(payload), 0);
		if (err != sizeof(payload)) {
			Limelog("Resync thread terminating #2\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		response = readNvctlPacket();
		if (response == NULL) {
			Limelog("Resync thread terminating #3\n");
			listenerCallbacks->connectionTerminated(LastSocketError());
			return;
		}
		Limelog("Resync complete\n");

		PltClearEvent(&resyncEvent);
	}
}

int stopControlStream(void) {
	PltInterruptThread(&heartbeatThread);
	PltInterruptThread(&jitterThread);
	PltInterruptThread(&resyncThread);

	if (ctlSock != INVALID_SOCKET) {
		closesocket(ctlSock);
		ctlSock = INVALID_SOCKET;
	}

	PltJoinThread(&heartbeatThread);
	PltJoinThread(&jitterThread);
	PltJoinThread(&resyncThread);

	PltCloseThread(&heartbeatThread);
	PltCloseThread(&jitterThread);
	PltCloseThread(&resyncThread);

	return 0;
}

int startControlStream(void) {
	int err;
	char* config;
	int configSize;
	PNVCTL_PACKET_HEADER response;

	ctlSock = connectTcpSocket(host, 47995);
	if (ctlSock == INVALID_SOCKET) {
		return LastSocketError();
	}

	enableNoDelay(ctlSock);

	configSize = getConfigDataSize(&streamConfig);
	config = allocateConfigDataForStreamConfig(&streamConfig);
	if (config == NULL) {
		return -1;
	}

	// Send config
	err = send(ctlSock, config, configSize, 0);
	free(config);
	if (err != configSize) {
		return LastSocketError();
	}

	// Ping pong
	response = sendNoPayloadAndReceive(PTYPE_KEEPALIVE, PPAYLEN_KEEPALIVE);
	if (response == NULL) {
		return LastSocketError();
	}
	free(response);

	// 1405
	response = sendNoPayloadAndReceive(PTYPE_1405, PPAYLEN_1405);
	if (response == NULL) {
		return LastSocketError();
	}
	free(response);

	err = PltCreateThread(heartbeatThreadFunc, NULL, &heartbeatThread);
	if (err != 0) {
		return err;
	}

	err = PltCreateThread(jitterThreadFunc, NULL, &jitterThread);
	if (err != 0) {
		return err;
	}

	err = PltCreateThread(resyncThreadFunc, NULL, &resyncThread);
	if (err != 0) {
		return err;
	}

	return 0;
}