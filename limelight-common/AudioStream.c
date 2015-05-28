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

typedef struct _QUEUED_AUDIO_PACKET {
	char data[MAX_PACKET_SIZE];
	int size;
	LINKED_BLOCKING_QUEUE_ENTRY entry;
} QUEUED_AUDIO_PACKET, *PQUEUED_AUDIO_PACKET;

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

		// The entry is stored within the data allocation
		free(entry->data);

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
	SOCK_RET err;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(RTP_PORT);
	memcpy(&saddr.sin_addr, &remoteHost, sizeof(remoteHost));

	/* Send PING every 500 milliseconds */
	while (!PltIsThreadInterrupted(&udpPingThread)) {
		err = sendto(rtpSocket, pingData, sizeof(pingData), 0, (struct sockaddr*)&saddr, sizeof(saddr));
		if (err != sizeof(pingData)) {
			Limelog("UDP ping thread terminating #1\n");
			listenerCallbacks->connectionTerminated(LastSocketError());
			return;
		}

		PltSleepMs(500);
	}
}

static void ReceiveThreadProc(void* context) {
	PRTP_PACKET rtp;
	PQUEUED_AUDIO_PACKET packet;
	int err;

	packet = NULL;

	while (!PltIsThreadInterrupted(&receiveThread)) {
		if (packet == NULL) {
			packet = (PQUEUED_AUDIO_PACKET) malloc(sizeof(*packet));
			if (packet == NULL) {
				Limelog("Receive thread terminating\n");
				listenerCallbacks->connectionTerminated(-1);
				return;
			}
		}

		packet->size = (int) recv(rtpSocket, &packet->data[0], MAX_PACKET_SIZE, 0);
		if (packet->size <= 0) {
			Limelog("Receive thread terminating #2\n");
			free(packet);
			listenerCallbacks->connectionTerminated(LastSocketError());
			return;
		}

		if (packet->size < sizeof(RTP_PACKET)) {
			// Runt packet
			continue;
		}

		rtp = (PRTP_PACKET) &packet->data[0];
		if (rtp->packetType != 97) {
			// Not audio
			continue;
		}

		err = LbqOfferQueueItem(&packetQueue, packet, &packet->entry);
		if (err == LBQ_SUCCESS) {
			// The queue owns the buffer now
			packet = NULL;
		}

		if (err == LBQ_BOUND_EXCEEDED) {
			Limelog("Audio packet queue overflow\n");
			freePacketList(LbqFlushQueueItems(&packetQueue));
		}
		else if (err == LBQ_INTERRUPTED) {
			Limelog("Receive thread terminating #2\n");
			free(packet);
			return;
		}
	}
}

static void DecoderThreadProc(void* context) {
	PRTP_PACKET rtp;
	int err;
	PQUEUED_AUDIO_PACKET packet;
	unsigned short lastSeq = 0;

	while (!PltIsThreadInterrupted(&decoderThread)) {
		err = LbqWaitForQueueElement(&packetQueue, (void**) &packet);
		if (err != LBQ_SUCCESS) {
			Limelog("Decoder thread terminating\n");
			return;
		}

		rtp = (PRTP_PACKET) &packet->data[0];

		rtp->sequenceNumber = htons(rtp->sequenceNumber);

		if (lastSeq != 0 && (unsigned short) (lastSeq + 1) != rtp->sequenceNumber) {
			Limelog("Received OOS audio data (expected %d, but got %d)\n", lastSeq + 1, rtp->sequenceNumber);

			callbacks.decodeAndPlaySample(NULL, 0);
		}

		lastSeq = rtp->sequenceNumber;

		callbacks.decodeAndPlaySample((char *) (rtp + 1), packet->size - sizeof(*rtp));

		free(packet);
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

	rtpSocket = bindUdpSocket();
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