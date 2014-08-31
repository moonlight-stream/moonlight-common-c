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

#define MAX_PACKET_SIZE 100

/* Initialize the audio stream */
void initializeAudioStream(IP_ADDRESS host, PAUDIO_RENDERER_CALLBACKS arCallbacks, PCONNECTION_LISTENER_CALLBACKS clCallbacks) {
	memcpy(&callbacks, arCallbacks, sizeof(callbacks));
	remoteHost = host;
	listenerCallbacks = clCallbacks;

	LbqInitializeLinkedBlockingQueue(&packetQueue, 30);
}

static void freePacketList(PLINKED_BLOCKING_QUEUE_ENTRY entry) {
	PLINKED_BLOCKING_QUEUE_ENTRY nextEntry;

	while (entry != NULL) {
		nextEntry = entry->flink;
		free(entry->data);
		free(entry);
		entry = nextEntry;
	}
}

/* Tear down the audio stream once we're done with it */
void destroyAudioStream(void) {
	callbacks.release();

	freePacketList(LbqDestroyLinkedBlockingQueue(&packetQueue));
}



static void UdpPingThreadProc(void *context) {
	/* Ping in ASCII */
	char pingData[] = { 0x50, 0x49, 0x4E, 0x47 };
	struct sockaddr_in saddr;
	int err;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(RTP_PORT);
	memcpy(&saddr.sin_addr, &remoteHost, sizeof(remoteHost));

	/* Send PING every 100 milliseconds */
	while (!PltIsThreadInterrupted(&udpPingThread)) {
		err = sendto(rtpSocket, pingData, sizeof(pingData), 0, (struct sockaddr*)&saddr, sizeof(saddr));
		if (err != sizeof(pingData)) {
			Limelog("UDP ping thread terminating #1\n");
			listenerCallbacks->connectionTerminated(LastSocketError());
			return;
		}

		PltSleepMs(100);
	}
}

static void ReceiveThreadProc(void* context) {
	int err;
	PRTP_PACKET rtp;
    char* buffer = NULL;

	while (!PltIsThreadInterrupted(&receiveThread)) {
		if (buffer == NULL) {
			buffer = (char*) malloc(MAX_PACKET_SIZE + sizeof(int));
			if (buffer == NULL) {
				Limelog("Receive thread terminating\n");
				listenerCallbacks->connectionTerminated(-1);
				return;
			}
		}

		err = recv(rtpSocket, &buffer[sizeof(int)], MAX_PACKET_SIZE, 0);
		if (err <= 0) {
			Limelog("Receive thread terminating #2\n");
			free(buffer);
			listenerCallbacks->connectionTerminated(LastSocketError());
			return;
		}

		if (err < sizeof(RTP_PACKET)) {
			// Runt packet
			continue;
		}

		rtp = (PRTP_PACKET) &buffer[sizeof(int)];
		if (rtp->packetType != 97) {
			// Not audio
			continue;
		}

		memcpy(buffer, &err, sizeof(err));

		err = LbqOfferQueueItem(&packetQueue, buffer);
		if (err == LBQ_SUCCESS) {
			// The queue owns the buffer now
			buffer = NULL;
		}

		if (err == LBQ_BOUND_EXCEEDED) {
			Limelog("Audio packet queue overflow\n");
			freePacketList(LbqFlushQueueItems(&packetQueue));
		}
		else if (err == LBQ_INTERRUPTED) {
			Limelog("Receive thread terminating #2\n");
			free(buffer);
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
	if (rtpSocket == INVALID_SOCKET) {
		return LastSocketError();
	}

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