#include "Limelight.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

typedef struct _NVCTL_PACKET_HEADER {
	unsigned short type;
	unsigned short payloadLength;
} NVCTL_PACKET_HEADER, *PNVCTL_PACKET_HEADER;

SOCKET ctlSock;
STREAM_CONFIGURATION streamConfig;
PLT_THREAD heartbeatThread;
PLT_THREAD jitterThread;
PLT_THREAD resyncThread;

const short PTYPE_KEEPALIVE = 0x13ff;
const short PPAYLEN_KEEPALIVE = 0x0000;

const short PTYPE_HEARTBEAT = 0x1401;
const short PPAYLEN_HEARTBEAT = 0x0000;

const short PTYPE_1405 = 0x1405;
const short PPAYLEN_1405 = 0x0000;

const short PTYPE_RESYNC = 0x1404;
const short PPAYLEN_RESYNC = 16;

const short PTYPE_JITTER = 0x140c;
const short PPAYLEN_JITTER = 0x10;

int initializeControlStream(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfigPtr) {
	ctlSock = connectTcpSocket(host, 47995);
	if (ctlSock == INVALID_SOCKET) {
		return LastSocketError();
	}

	enableNoDelay(ctlSock);

	memcpy(&streamConfig, streamConfigPtr, sizeof(*streamConfigPtr));

	return 0;
}

void requestIdrFrame(void) {

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
	err = recv(ctlSock, (char*) (fullPacket + 1), staticHeader.payloadLength, 0);
	if (err != staticHeader.payloadLength) {
		free(fullPacket);
		return NULL;
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

	for (;;) {
		header.type = PTYPE_HEARTBEAT;
		header.payloadLength = PPAYLEN_HEARTBEAT;
		err = send(ctlSock, (char*) &header, sizeof(header), 0);
		if (err != sizeof(header)) {
			Limelog("Heartbeat thread terminating\n");
			return;
		}

		Sleep(3000);
	}
}

static void jitterThreadFunc(void* context) {
	int payload[4];
	NVCTL_PACKET_HEADER header;
	int err;

	header.type = PTYPE_JITTER;
	header.payloadLength = PPAYLEN_JITTER;
	for (;;) {
		err = send(ctlSock, (char*) &header, sizeof(header), 0);
		if (err != sizeof(header)) {
			Limelog("Jitter thread terminating #1\n");
			return;
		}

		payload[0] = 0;
		payload[1] = 77;
		payload[2] = 888;
		payload[3] = 0; // FIXME: Sequence number?

		err = send(ctlSock, (char*) payload, sizeof(payload), 0);
		if (err != sizeof(payload)) {
			Limelog("Jitter thread terminating #2\n");
			return;
		}

		Sleep(100);
	}
}

static void resyncThreadFunc(void* context) {
}

int stopControlStream(void) {
	closesocket(ctlSock);

	PltJoinThread(heartbeatThread);
	PltJoinThread(jitterThread);
	PltJoinThread(resyncThread);

	PltCloseThread(heartbeatThread);
	PltCloseThread(jitterThread);
	PltCloseThread(resyncThread);

	return 0;
}

int startControlStream(void) {
	int err;
	char* config;
	int configSize;
	PNVCTL_PACKET_HEADER response;

	configSize = getConfigDataSize(&streamConfig);
	config = allocateConfigDataForStreamConfig(&streamConfig);
	if (config == NULL) {
		return NULL;
	}

	// Send config
	err = send(ctlSock, config, configSize, 0);
	free(config);
	if (err != configSize) {
		return NULL;
	}

	// Ping pong
	response = sendNoPayloadAndReceive(PTYPE_HEARTBEAT, PPAYLEN_HEARTBEAT);
	if (response == NULL) {
		return NULL;
	}
	free(response);

	// 1405
	response = sendNoPayloadAndReceive(PTYPE_1405, PPAYLEN_1405);
	if (response == NULL) {
		return NULL;
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