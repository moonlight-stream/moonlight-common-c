#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "LinkedBlockingQueue.h"

#define FIRST_FRAME_MAX 1500

#define RTP_PORT 47998
#define FIRST_FRAME_PORT 47996

static DECODER_RENDERER_CALLBACKS callbacks;
static STREAM_CONFIGURATION configuration;
static IP_ADDRESS remoteHost;
static PCONNECTION_LISTENER_CALLBACKS listenerCallbacks;

static SOCKET rtpSocket = INVALID_SOCKET;
static SOCKET firstFrameSocket = INVALID_SOCKET;

static LINKED_BLOCKING_QUEUE packetQueue;

static PLT_THREAD udpPingThread;
static PLT_THREAD receiveThread;
static PLT_THREAD depacketizerThread;
static PLT_THREAD decoderThread;

void initializeVideoStream(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig, PDECODER_RENDERER_CALLBACKS drCallbacks,
	PCONNECTION_LISTENER_CALLBACKS clCallbacks) {
	memcpy(&callbacks, drCallbacks, sizeof(callbacks));
	memcpy(&configuration, streamConfig, sizeof(configuration));
	remoteHost = host;
	listenerCallbacks = clCallbacks;

	LbqInitializeLinkedBlockingQueue(&packetQueue, 30);

	initializeVideoDepacketizer();
}

void destroyVideoStream(void) {
	PLINKED_BLOCKING_QUEUE_ENTRY entry, nextEntry;

	callbacks.release();

	destroyVideoDepacketizer();
	
	entry = LbqDestroyLinkedBlockingQueue(&packetQueue);

	while (entry != NULL) {
		nextEntry = entry->flink;
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
	saddr.sin_port = htons(RTP_PORT);
	memcpy(&saddr.sin_addr, &remoteHost, sizeof(remoteHost));

	while (!PltIsThreadInterrupted(&udpPingThread)) {
		err = sendto(rtpSocket, pingData, sizeof(pingData), 0, (struct sockaddr*)&saddr, sizeof(saddr));
		if (err != sizeof(pingData)) {
			Limelog("UDP ping thread terminating #1\n");
			listenerCallbacks->connectionTerminated(err);
			return;
		}

		PltSleepMs(100);
	}
}

static void ReceiveThreadProc(void* context) {
	int err;

	while (!PltIsThreadInterrupted(&receiveThread)) {
		char* buffer = (char*) malloc(1500 + sizeof(int));
		if (buffer == NULL) {
			Limelog("Receive thread terminating\n");
			listenerCallbacks->connectionTerminated(-1);
			return;
		}

		err = recv(rtpSocket, &buffer[sizeof(int)], 1500, 0);
		if (err <= 0) {
			Limelog("Receive thread terminating #2\n");
			free(buffer);
			listenerCallbacks->connectionTerminated(err);
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

	while (!PltIsThreadInterrupted(&depacketizerThread)) {
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
	while (!PltIsThreadInterrupted(&decoderThread)) {
		if (!getNextDecodeUnit(&du)) {
			printf("Decoder thread terminating\n");
			return;
		}

		callbacks.submitDecodeUnit(du);

		freeDecodeUnit(du);
	}
}

int readFirstFrame(void) {
	char* firstFrame;
	int err;
	int offset = 0;

	firstFrameSocket = connectTcpSocket(remoteHost, FIRST_FRAME_PORT);
	if (firstFrameSocket == INVALID_SOCKET) {
		return LastSocketError();
	}

	firstFrame = (char*) malloc(FIRST_FRAME_MAX);
	if (firstFrame == NULL) {
		return -1;
	}

	Limelog("Waiting for first frame\n");
	for (;;) {
		err = recv(firstFrameSocket, &firstFrame[offset], FIRST_FRAME_MAX - offset, 0);
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
	callbacks.stop();

	PltInterruptThread(&udpPingThread);
	PltInterruptThread(&receiveThread);
	PltInterruptThread(&depacketizerThread);
	PltInterruptThread(&decoderThread);

	if (firstFrameSocket != INVALID_SOCKET) {
		closesocket(firstFrameSocket);
		firstFrameSocket = INVALID_SOCKET;
	}
	if (rtpSocket != INVALID_SOCKET) {
		closesocket(rtpSocket);
		rtpSocket = INVALID_SOCKET;
	}

	PltJoinThread(&udpPingThread);
	PltJoinThread(&receiveThread);
	PltJoinThread(&depacketizerThread);
	PltJoinThread(&decoderThread);

	PltCloseThread(&udpPingThread);
	PltCloseThread(&receiveThread);
	PltCloseThread(&depacketizerThread);
	PltCloseThread(&decoderThread);
}

int startVideoStream(void* rendererContext, int drFlags) {
	int err;

	callbacks.setup(configuration.width,
		configuration.height, 60, rendererContext, drFlags);

	rtpSocket = bindUdpSocket(RTP_PORT);

	err = PltCreateThread(UdpPingThreadProc, NULL, &udpPingThread);
	if (err != 0) {
		return err;
	}

	err = readFirstFrame();
	if (err != 0) {
		return err;
	}

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

	callbacks.start();

	return 0;
}
