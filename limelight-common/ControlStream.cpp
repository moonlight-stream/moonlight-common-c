#include "Limelight.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"

typedef struct _CONTROL_STREAM {
	SOCKET s;
	STREAM_CONFIGURATION streamConfig;
	PLT_THREAD heartbeatThread;
	PLT_THREAD jitterThread;
	PLT_THREAD resyncThread;
} CONTROL_STREAM, *PCONTROL_STREAM;

typedef struct _NVCTL_PACKET_HEADER {
	unsigned short type;
	unsigned short payloadLength;
} NVCTL_PACKET_HEADER, *PNVCTL_PACKET_HEADER;

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

void* initializeControlStream(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig) {
	PCONTROL_STREAM ctx;

	ctx = (PCONTROL_STREAM) malloc(sizeof(*ctx));
	if (ctx == NULL) {
		return NULL;
	}

	ctx->s = connectTcpSocket(host, 47995);
	if (ctx->s == INVALID_SOCKET) {
		free(ctx);
		return NULL;
	}

	enableNoDelay(ctx->s);

	memcpy(&ctx->streamConfig, streamConfig, sizeof(*streamConfig));

	return ctx;
}

static PNVCTL_PACKET_HEADER readNvctlPacket(PCONTROL_STREAM stream) {
	NVCTL_PACKET_HEADER staticHeader;
	PNVCTL_PACKET_HEADER fullPacket;
	int err;

	err = recv(stream->s, (char*) &staticHeader, sizeof(staticHeader), 0);
	if (err != sizeof(staticHeader)) {
		return NULL;
	}

	fullPacket = (PNVCTL_PACKET_HEADER) malloc(staticHeader.payloadLength + sizeof(staticHeader));
	if (fullPacket == NULL) {
		return NULL;
	}

	memcpy(fullPacket, &staticHeader, sizeof(staticHeader));
	err = recv(stream->s, (char*) (fullPacket + 1), staticHeader.payloadLength, 0);
	if (err != staticHeader.payloadLength) {
		free(fullPacket);
		return NULL;
	}

	return fullPacket;
}

static PNVCTL_PACKET_HEADER sendNoPayloadAndReceive(PCONTROL_STREAM stream, short ptype, short paylen) {
	NVCTL_PACKET_HEADER header;
	int err;

	header.type = ptype;
	header.payloadLength = paylen;
	err = send(stream->s, (char*) &header, sizeof(header), 0);
	if (err != sizeof(header)) {
		return NULL;
	}

	return readNvctlPacket(stream);
}

static void heartbeatThreadFunc(void* context) {
	PCONTROL_STREAM stream = (PCONTROL_STREAM) context;
	int err;
	NVCTL_PACKET_HEADER header;

	for (;;) {
		header.type = PTYPE_HEARTBEAT;
		header.payloadLength = PPAYLEN_HEARTBEAT;
		err = send(stream->s, (char*) &header, sizeof(header), 0);
		if (err != sizeof(header)) {
			Limelog("Heartbeat thread terminating\n");
			return;
		}

		Sleep(3000);
	}
}

static void jitterThreadFunc(void* context) {
	PCONTROL_STREAM stream = (PCONTROL_STREAM) context;
	int payload[4];
	NVCTL_PACKET_HEADER header;
	int err;

	header.type = PTYPE_JITTER;
	header.payloadLength = PPAYLEN_JITTER;
	for (;;) {
		err = send(stream->s, (char*) &header, sizeof(header), 0);
		if (err != sizeof(header)) {
			Limelog("Jitter thread terminating #1\n");
			return;
		}

		payload[0] = 0;
		payload[1] = 77;
		payload[2] = 888;
		payload[3] = 0; // FIXME: Sequence number?

		err = send(stream->s, (char*) payload, sizeof(payload), 0);
		if (err != sizeof(payload)) {
			Limelog("Jitter thread terminating #2\n");
			return;
		}

		Sleep(100);
	}
}

static void resyncThreadFunc(void* context) {
	PCONTROL_STREAM stream = (PCONTROL_STREAM) context;
}

int stopControlStream(void* context) {
	PCONTROL_STREAM stream = (PCONTROL_STREAM) context;

	closesocket(stream->s);

	PltJoinThread(stream->heartbeatThread);
	PltJoinThread(stream->jitterThread);
	PltJoinThread(stream->resyncThread);

	PltCloseThread(stream->heartbeatThread);
	PltCloseThread(stream->jitterThread);
	PltCloseThread(stream->resyncThread);

	return 0;
}

int startControlStream(void* context) {
	PCONTROL_STREAM stream = (PCONTROL_STREAM) context;
	int err;
	char* config;
	int configSize;
	PNVCTL_PACKET_HEADER response;

	configSize = getConfigDataSize(&stream->streamConfig);
	config = allocateConfigDataForStreamConfig(&stream->streamConfig);
	if (config == NULL) {
		return NULL;
	}

	// Send config
	err = send(stream->s, config, configSize, 0);
	free(config);
	if (err != configSize) {
		return NULL;
	}

	// Ping pong
	response = sendNoPayloadAndReceive(stream, PTYPE_HEARTBEAT, PPAYLEN_HEARTBEAT);
	if (response == NULL) {
		return NULL;
	}
	free(response);

	// 1405
	response = sendNoPayloadAndReceive(stream, PTYPE_1405, PPAYLEN_1405);
	if (response == NULL) {
		return NULL;
	}
	free(response);

	err = PltCreateThread(heartbeatThreadFunc, context, &stream->heartbeatThread);
	if (err != 0) {
		return err;
	}

	err = PltCreateThread(jitterThreadFunc, context, &stream->jitterThread);
	if (err != 0) {
		return err;
	}

	err = PltCreateThread(resyncThreadFunc, context, &stream->resyncThread);
	if (err != 0) {
		return err;
	}

	return 0;
}