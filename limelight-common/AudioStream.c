#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "LinkedBlockingQueue.h"

static AUDIO_RENDERER_CALLBACKS callbacks;
static PCONNECTION_LISTENER_CALLBACKS listenerCallbacks;
static IP_ADDRESS remoteHost;

static SOCKET rtpSocket = INVALID_SOCKET;

static LINKED_BLOCKING_QUEUE packetQueue;

static PLT_THREAD udpPingThread;
static PLT_THREAD receiveThread;
static PLT_THREAD decoderThread;

#define RTP_PORT 48000

void initializeAudioStream(IP_ADDRESS host, PAUDIO_RENDERER_CALLBACKS arCallbacks, PCONNECTION_LISTENER_CALLBACKS clCallbacks) {
	memcpy(&callbacks, arCallbacks, sizeof(callbacks));
	remoteHost = host;
	listenerCallbacks = clCallbacks;

	LbqInitializeLinkedBlockingQueue(&packetQueue, 30);
}

void destroyAudioStream(void) {
	PLINKED_BLOCKING_QUEUE_ENTRY entry, nextEntry;

	callbacks.release();

	entry = LbqDestroyLinkedBlockingQueue(&packetQueue);

	while (entry != NULL) {
		nextEntry = entry->next;
		free(entry->data);
		free(entry);
		entry = nextEntry;
	}
}

static void UdpPingThreadProc(void *context) {
	char pingData[] = { 0x50, 0x49, 0x4E, 0x47 };
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
			listenerCallbacks->connectionTerminated(ERROR_OUTOFMEMORY);
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
			Limelog("Audio packet queue overflow\n");
		}
		else if (err == LBQ_INTERRUPTED) {
			Limelog("Receive thread terminating #2\n");
			return;
		}
	}
}

static void DecoderThreadProc(void* context) {
	PRTP_PACKET rtp;
	int length;
	int err;
	char *data;
	unsigned short lastSeq = 0;

	while (!PltIsThreadInterrupted(&decoderThread)) {
		err = LbqWaitForQueueElement(&packetQueue, (void**) &data);
		if (err != LBQ_SUCCESS) {
			Limelog("Decoder thread terminating\n");
			return;
		}

		memcpy(&length, data, sizeof(int));
		rtp = (PRTP_PACKET) &data[sizeof(int)];

		if (length < sizeof(RTP_PACKET)) {
			// Runt packet
			goto freeandcontinue;
		}

		if (rtp->packetType != 97) {
			// Not audio
			goto freeandcontinue;
		}

		rtp->sequenceNumber = htons(rtp->sequenceNumber);

		if (lastSeq != 0 && (unsigned short) (lastSeq + 1) != rtp->sequenceNumber) {
			Limelog("Received OOS audio data (expected %d, but got %d)\n", lastSeq + 1, rtp->sequenceNumber);

			callbacks.decodeAndPlaySample(NULL, 0);
		}

		lastSeq = rtp->sequenceNumber;

		callbacks.decodeAndPlaySample((char *) (rtp + 1), length - sizeof(*rtp));

	freeandcontinue:
		free(data);
	}
}

void stopAudioStream(void) {
	callbacks.stop();

	PltInterruptThread(&udpPingThread);
	PltInterruptThread(&receiveThread);
	PltInterruptThread(&decoderThread);

	if (rtpSocket != INVALID_SOCKET) {
		closesocket(rtpSocket);
		rtpSocket = INVALID_SOCKET;
	}

	PltJoinThread(&udpPingThread);
	PltJoinThread(&receiveThread);
	PltJoinThread(&decoderThread);

	PltCloseThread(&udpPingThread);
	PltCloseThread(&receiveThread);
	PltCloseThread(&decoderThread);
}

int startAudioStream(void) {
	int err;
    
    callbacks.init();

	rtpSocket = bindUdpSocket(RTP_PORT);

	err = PltCreateThread(UdpPingThreadProc, NULL, &udpPingThread);
	if (err != 0) {
		return err;
	}

	err = PltCreateThread(ReceiveThreadProc, NULL, &receiveThread);
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