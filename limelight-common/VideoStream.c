#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "LinkedBlockingQueue.h"
#include "RtpReorderQueue.h"

#define FIRST_FRAME_MAX 1500

#define RTP_PORT 47998
#define FIRST_FRAME_PORT 47996

#define RTP_RECV_BUFFER (64 * 1024)

static RTP_REORDER_QUEUE rtpQueue;

static SOCKET rtpSocket = INVALID_SOCKET;
static SOCKET firstFrameSocket = INVALID_SOCKET;

static PLT_THREAD udpPingThread;
static PLT_THREAD receiveThread;
static PLT_THREAD decoderThread;

// We can't request an IDR frame until the depacketizer knows
// that a packet was lost. This timeout bounds the time that
// the RTP queue will wait for missing/reordered packets.
#define RTP_QUEUE_DELAY 10

/* Initialize the video stream */
void initializeVideoStream(void) {
	initializeVideoDepacketizer(StreamConfig.packetSize);
	RtpqInitializeQueue(&rtpQueue, RTPQ_DEFAULT_MAX_SIZE, RTP_QUEUE_DELAY);
}

/* Clean up the video stream */
void destroyVideoStream(void) {
	destroyVideoDepacketizer();
	RtpqCleanupQueue(&rtpQueue);
}

/* UDP Ping proc */
static void UdpPingThreadProc(void *context) {
	char pingData [] = { 0x50, 0x49, 0x4E, 0x47 };
	struct sockaddr_in6 saddr;
	SOCK_RET err;

	memcpy(&saddr, &RemoteAddr, sizeof(saddr));
	saddr.sin6_port = htons(RTP_PORT);

	while (!PltIsThreadInterrupted(&udpPingThread)) {
		err = sendto(rtpSocket, pingData, sizeof(pingData), 0, (struct sockaddr*)&saddr, RemoteAddrLen);
		if (err != sizeof(pingData)) {
			Limelog("Video Ping: send() failed: %d\n", (int)LastSocketError());
			ListenerCallbacks.connectionTerminated(LastSocketError());
			return;
		}

		PltSleepMs(500);
	}
}

/* Receive thread proc */
static void ReceiveThreadProc(void* context) {
	int err;
	int bufferSize, receiveSize;
	char* buffer;
	int queueStatus;

	receiveSize = StreamConfig.packetSize + MAX_RTP_HEADER_SIZE;
	bufferSize = receiveSize + sizeof(int) + sizeof(RTP_QUEUE_ENTRY);
	buffer = NULL;

	while (!PltIsThreadInterrupted(&receiveThread)) {
        PRTP_PACKET packet;
        
		if (buffer == NULL) {
			buffer = (char*) malloc(bufferSize);
			if (buffer == NULL) {
				Limelog("Video Receive: malloc() failed\n");
				ListenerCallbacks.connectionTerminated(-1);
				return;
			}
		}

		err = (int) recv(rtpSocket, buffer, receiveSize, 0);
		if (err <= 0) {
			Limelog("Video Receive: recv() failed: %d\n", (int)LastSocketError());
			ListenerCallbacks.connectionTerminated(LastSocketError());
			break;
		}

		memcpy(&buffer[receiveSize], &err, sizeof(int));
        
        // RTP sequence number must be in host order for the RTP queue
        packet = (PRTP_PACKET) &buffer[0];
        packet->sequenceNumber = htons(packet->sequenceNumber);

		queueStatus = RtpqAddPacket(&rtpQueue, packet, (PRTP_QUEUE_ENTRY) &buffer[receiveSize + sizeof(int)]);
		if (queueStatus == RTPQ_RET_HANDLE_IMMEDIATELY) {
			// queueRtpPacket() copies the data it needs to we can reuse the buffer
			queueRtpPacket(packet, err);
		}
		else if (queueStatus == RTPQ_RET_QUEUED_PACKETS_READY) {
			// The packet queue now has packets ready
			while ((buffer = (char*) RtpqGetQueuedPacket(&rtpQueue)) != NULL) {
				memcpy(&err, &buffer[receiveSize], sizeof(int));
				queueRtpPacket((PRTP_PACKET) buffer, err);
				free(buffer);
			}
		}
        else if (queueStatus == RTPQ_RET_QUEUED_NOTHING_READY) {
            // The queue owns the buffer
            buffer = NULL;
        }
	}

	if (buffer != NULL) {
		free(buffer);
	}
}

/* Decoder thread proc */
static void DecoderThreadProc(void* context) {
	PQUEUED_DECODE_UNIT qdu;
	while (!PltIsThreadInterrupted(&decoderThread)) {
		if (!getNextQueuedDecodeUnit(&qdu)) {
			return;
		}

		int ret = VideoCallbacks.submitDecodeUnit(&qdu->decodeUnit);

		freeQueuedDecodeUnit(qdu);
        
        if (ret == DR_NEED_IDR) {
            Limelog("Requesting IDR frame on behalf of DR\n");
            requestIdrOnDemand();
        }
	}
}

/* Read the first frame of the video stream */
int readFirstFrame(void) {
    // All that matters is that we close this socket.
    // This starts the flow of video on Gen 3 servers.
    
    closesocket(firstFrameSocket);
    firstFrameSocket = INVALID_SOCKET;
    
	return 0;
}

/* Terminate the video stream */
void stopVideoStream(void) {
	PltInterruptThread(&udpPingThread);
	PltInterruptThread(&receiveThread);
	if ((VideoCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
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

	PltJoinThread(&udpPingThread);
	PltJoinThread(&receiveThread);
	if ((VideoCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
		PltJoinThread(&decoderThread);
	}

	PltCloseThread(&udpPingThread);
	PltCloseThread(&receiveThread);
	if ((VideoCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
		PltCloseThread(&decoderThread);
	}

    VideoCallbacks.cleanup();
}

/* Start the video stream */
int startVideoStream(void* rendererContext, int drFlags) {
	int err;

    // This must be called before the decoder thread starts submitting
    // decode units
	VideoCallbacks.setup(StreamConfig.width,
		StreamConfig.height, StreamConfig.fps, rendererContext, drFlags);
    
	rtpSocket = bindUdpSocket(RemoteAddr.ss_family, RTP_RECV_BUFFER);
	if (rtpSocket == INVALID_SOCKET) {
		return LastSocketError();
	}

	err = PltCreateThread(ReceiveThreadProc, NULL, &receiveThread);
	if (err != 0) {
		return err;
	}

	if ((VideoCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
		err = PltCreateThread(DecoderThreadProc, NULL, &decoderThread);
		if (err != 0) {
			return err;
		}
	}
    
    if (ServerMajorVersion == 3) {
        // Connect this socket to open port 47998 for our ping thread
        firstFrameSocket = connectTcpSocket(&RemoteAddr, RemoteAddrLen, FIRST_FRAME_PORT);
        if (firstFrameSocket == INVALID_SOCKET) {
            return LastSocketError();
        }
    }
    
    // Start pinging before reading the first frame so GFE knows where
    // to send UDP data
    err = PltCreateThread(UdpPingThreadProc, NULL, &udpPingThread);
    if (err != 0) {
        return err;
    }
    
    if (ServerMajorVersion == 3) {
        // Read the first frame to start the flow of video
        err = readFirstFrame();
        if (err != 0) {
            return err;
        }
    }

	return 0;
}
