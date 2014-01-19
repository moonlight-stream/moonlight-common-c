#include "Limelight.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "LinkedBlockingQueue.h"

PDECODER_RENDERER_CALLBACKS callbacks;
PSTREAM_CONFIGURATION configuration;
IP_ADDRESS remoteHost;

SOCKET rtpSocket;

LINKED_BLOCKING_QUEUE packetQueue;

PLT_THREAD udpPingThread;
PLT_THREAD receiveThread;
PLT_THREAD depacketizerThread;
PLT_THREAD decoderThread;

void initializeVideoStream(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig, PDECODER_RENDERER_CALLBACKS drCallbacks) {
	callbacks = drCallbacks;
	configuration = streamConfig;
	remoteHost = host;

	initializeLinkedBlockingQueue(&packetQueue, 15);
}

static void UdpPingThreadProc(void *context) {
	char pingData [] = { 0x50, 0x49, 0x4E, 0x47 };
	struct sockaddr_in saddr;
	int err;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(47998);
	memcpy(&saddr.sin_addr, &remoteHost, sizeof(remoteHost));

	for (;;) {
		err = sendto(rtpSocket, pingData, sizeof(pingData), 0, (struct sockaddr*)&saddr, sizeof(saddr));
		if (err != sizeof(pingData)) {
			Limelog("UDP ping thread terminating\n");
			return;
		}

		PltSleepMs(100);
	}
}

static void ReceiveThreadProc(void* context) {
	int err;

	for (;;) {
		char* buffer = (char*) malloc(1500 + sizeof(int));
		if (buffer == NULL) {
			Limelog("Receive thread terminating\n");
			return;
		}

		err = recv(rtpSocket, &buffer[sizeof(int)], 1500, 0);
		if (err <= 0) {
			Limelog("Receive thread terminating #2\n");
			return;
		}
		
		memcpy(buffer, &err, sizeof(err));

		if (!offerQueueItem(&packetQueue, buffer)) {
			free(buffer);
			Limelog("Packet queue overflow\n");
		}
	}
}

static void DepacketizerThreadProc(void* context) {
	int length;

	for (;;) {
		char* data = (char*) waitForQueueElement(&packetQueue);

		memcpy(&length, data, sizeof(int));
		queueRtpPacket((PRTP_PACKET) &data[sizeof(int)], length);

		free(data);
	}
}

static void DecoderThreadProc(void* context) {
	for (;;) {
		PDECODE_UNIT du = getNextDecodeUnit();

		callbacks->submitDecodeUnit(du);

		freeDecodeUnit(du);
	}
}

int readFirstFrame(void) {
	char firstFrame[1000];
	int err;
	int offset = 0;
	SOCKET s;

	s = connectTcpSocket(remoteHost, 47996);
	if (s == INVALID_SOCKET) {
		return LastSocketError();
	}

	Limelog("Waiting for first frame\n");
	for (;;) {
		err = recv(s, &firstFrame[offset], sizeof(firstFrame) - offset, 0);
		if (err <= 0) {
			break;
		}

		offset += err;
	}
	Limelog("Read %d bytes\n", offset);

	processRtpPayload((PNV_VIDEO_PACKET) firstFrame, offset);

	return 0;
}

int startVideoStream(void* rendererContext, int drFlags) {
	int err;

	if (callbacks != NULL) {
		callbacks->setup(configuration->width,
			configuration->height, 60, rendererContext, drFlags);
	}

	initializeVideoDepacketizer();

	// FIXME: Set socket options here
	rtpSocket = bindUdpSocket(47998);

	err = PltCreateThread(UdpPingThreadProc, NULL, &udpPingThread);
	if (err != 0) {
		return err;
	}

	err = readFirstFrame();
	if (err != 0) {
		return err;
	}

	if (callbacks != NULL) {
		err = PltCreateThread(ReceiveThreadProc, NULL, &receiveThread);
		if (err != 0) {
			return err;
		}

		err = PltCreateThread(DepacketizerThreadProc, NULL, &depacketizerThread);
		if (err != 0) {
			return err;
		}

		err = PltCreateThread(DecoderThreadProc, NULL, &decoderThread);
		if (err != 0) {
			return err;
		}

		callbacks->start();
	}

	return 0;
}

