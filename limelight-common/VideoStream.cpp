#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "LinkedBlockingQueue.h"

PDECODER_RENDERER_CALLBACKS callbacks;
PSTREAM_CONFIGURATION configuration;
IP_ADDRESS remoteHost;

SOCKET rtpSocket;
SOCKET firstFrameSocket;

LINKED_BLOCKING_QUEUE packetQueue;

PLT_THREAD udpPingThread;
PLT_THREAD receiveThread;
PLT_THREAD depacketizerThread;
PLT_THREAD decoderThread;

void initializeVideoStream(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig, PDECODER_RENDERER_CALLBACKS drCallbacks) {
	callbacks = drCallbacks;
	configuration = streamConfig;
	remoteHost = host;

	LbqInitializeLinkedBlockingQueue(&packetQueue, 30);

	initializeVideoDepacketizer();
}

void destroyVideoStream(void) {
	PLINKED_BLOCKING_QUEUE_ENTRY entry, nextEntry;

	destroyVideoDepacketizer();
	
	entry = LbqDestroyLinkedBlockingQueue(&packetQueue);

	while (entry != NULL) {
		nextEntry = entry->next;
		free(entry->data);
		free(entry);
		entry = nextEntry;
	}
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
			free(buffer);
			return;
		}
		
		memcpy(buffer, &err, sizeof(err));

		err = LbqOfferQueueItem(&packetQueue, buffer);
		if (err != LBQ_SUCCESS) {
			free(buffer);
		}

		if (err == LBQ_BOUND_EXCEEDED) {
			Limelog("Video packet queue overflow\n");
		}
		else if (err == LBQ_INTERRUPTED) {
			Limelog("Receive thread terminating #2\n");
			return;
		}
	}
}

static void DepacketizerThreadProc(void* context) {
	int length;
	int err;
	char *data;

	for (;;) {
		err = LbqWaitForQueueElement(&packetQueue, (void**)&data);
		if (err != LBQ_SUCCESS) {
			Limelog("Depacketizer thread terminating\n");
			return;
		}

		memcpy(&length, data, sizeof(int));
		queueRtpPacket((PRTP_PACKET) &data[sizeof(int)], length);

		free(data);
	}
}

static void DecoderThreadProc(void* context) {
	PDECODE_UNIT du;
	for (;;) {
		if (!getNextDecodeUnit(&du)) {
			printf("Decoder thread terminating\n");
			return;
		}

		callbacks->submitDecodeUnit(du);

		freeDecodeUnit(du);
	}
}

int readFirstFrame(void) {
	char firstFrame[1000];
	int err;
	int offset = 0;

	firstFrameSocket = connectTcpSocket(remoteHost, 47996);
	if (firstFrameSocket == INVALID_SOCKET) {
		return LastSocketError();
	}

	Limelog("Waiting for first frame\n");
	for (;;) {
		err = recv(firstFrameSocket, &firstFrame[offset], sizeof(firstFrame) - offset, 0);
		if (err <= 0) {
			break;
		}

		offset += err;
	}
	Limelog("Read %d bytes\n", offset);

	processRtpPayload((PNV_VIDEO_PACKET) firstFrame, offset);

	return 0;
}

void stopVideoStream(void) {
	if (udpPingThread != NULL) {
		PltInterruptThread(&udpPingThread);
	}
	if (receiveThread != NULL) {
		PltInterruptThread(&receiveThread);
	}
	if (depacketizerThread != NULL) {
		PltInterruptThread(&depacketizerThread);
	}
	if (decoderThread != NULL) {
		PltInterruptThread(&decoderThread);
	}

	if (firstFrameSocket != INVALID_SOCKET) {
		closesocket(firstFrameSocket);
		firstFrameSocket = INVALID_SOCKET;
	}
	if (rtpSocket != INVALID_SOCKET) {
		closesocket(rtpSocket);
		rtpSocket = INVALID_SOCKET;
	}

	if (udpPingThread != NULL) {
		PltJoinThread(&udpPingThread);
	}
	if (receiveThread != NULL) {
		PltJoinThread(&receiveThread);
	}
	if (depacketizerThread != NULL) {
		PltJoinThread(&depacketizerThread);
	}
	if (decoderThread != NULL) {
		PltJoinThread(&decoderThread);
	}

	if (udpPingThread != NULL) {
		PltCloseThread(&udpPingThread);
	}
	if (receiveThread != NULL) {
		PltCloseThread(&receiveThread);
	}
	if (depacketizerThread != NULL) {
		PltCloseThread(&depacketizerThread);
	}
	if (decoderThread != NULL) {
		PltCloseThread(&decoderThread);
	}
}

int startVideoStream(void* rendererContext, int drFlags) {
	int err;

	if (callbacks != NULL) {
		callbacks->setup(configuration->width,
			configuration->height, 60, rendererContext, drFlags);
	}

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

