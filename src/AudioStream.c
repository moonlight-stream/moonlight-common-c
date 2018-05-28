#include "Limelight-internal.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "LinkedBlockingQueue.h"
#include "RtpReorderQueue.h"

static SOCKET rtpSocket = INVALID_SOCKET;

static LINKED_BLOCKING_QUEUE packetQueue;
static RTP_REORDER_QUEUE rtpReorderQueue;

static PLT_THREAD udpPingThread;
static PLT_THREAD receiveThread;
static PLT_THREAD decoderThread;

static unsigned short lastSeq;

#define RTP_PORT 48000

#define MAX_PACKET_SIZE 1400

// This is much larger than we should typically have buffered, but
// it needs to be. We need a cushion in case our thread gets blocked
// for longer than normal.
#define RTP_RECV_BUFFER (64 * 1024)

#define SAMPLE_RATE 48000

static OPUS_MULTISTREAM_CONFIGURATION opusStereoConfig = {
    .sampleRate = SAMPLE_RATE,
    .channelCount = 2,
    .streams = 1,
    .coupledStreams = 1,
    .mapping = {0, 1}
};

static OPUS_MULTISTREAM_CONFIGURATION opus51SurroundConfig = {
    .sampleRate = SAMPLE_RATE,
    .channelCount = 6,
    .streams = 4,
    .coupledStreams = 2,
    .mapping = {0, 4, 1, 5, 2, 3}
};

static OPUS_MULTISTREAM_CONFIGURATION opus51HighSurroundConfig = {
        .sampleRate = SAMPLE_RATE,
        .channelCount = 6,
        .streams = 6,
        .coupledStreams = 0,
        .mapping = {0, 1, 2, 3, 4, 5}
};

typedef struct _QUEUED_AUDIO_PACKET {
    // data must remain at the front
    char data[MAX_PACKET_SIZE];

    int size;
    union {
        RTP_QUEUE_ENTRY rentry;
        LINKED_BLOCKING_QUEUE_ENTRY lentry;
    } q;
} QUEUED_AUDIO_PACKET, *PQUEUED_AUDIO_PACKET;

// Initialize the audio stream
void initializeAudioStream(void) {
    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
        LbqInitializeLinkedBlockingQueue(&packetQueue, 30);
    }
    RtpqInitializeQueue(&rtpReorderQueue, RTPQ_DEFAULT_MAX_SIZE, RTPQ_DEFAULT_QUEUE_TIME);
    lastSeq = 0;
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

// Tear down the audio stream once we're done with it
void destroyAudioStream(void) {
    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
        freePacketList(LbqDestroyLinkedBlockingQueue(&packetQueue));
    }
    RtpqCleanupQueue(&rtpReorderQueue);
}

static void UdpPingThreadProc(void* context) {
    // Ping in ASCII
    char pingData[] = { 0x50, 0x49, 0x4E, 0x47 };
    struct sockaddr_in6 saddr;
    SOCK_RET err;

    memcpy(&saddr, &RemoteAddr, sizeof(saddr));
    saddr.sin6_port = htons(RTP_PORT);

    // Send PING every 500 milliseconds
    while (!PltIsThreadInterrupted(&udpPingThread)) {
        err = sendto(rtpSocket, pingData, sizeof(pingData), 0, (struct sockaddr*)&saddr, RemoteAddrLen);
        if (err != sizeof(pingData)) {
            Limelog("Audio Ping: sendto() failed: %d\n", (int)LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketError());
            return;
        }

        PltSleepMs(500);
    }
}

static int queuePacketToLbq(PQUEUED_AUDIO_PACKET* packet) {
    int err;

    err = LbqOfferQueueItem(&packetQueue, *packet, &(*packet)->q.lentry);
    if (err == LBQ_SUCCESS) {
        // The LBQ owns the buffer now
        *packet = NULL;
    }
    else if (err == LBQ_BOUND_EXCEEDED) {
        Limelog("Audio packet queue overflow\n");
        freePacketList(LbqFlushQueueItems(&packetQueue));
    }
    else if (err == LBQ_INTERRUPTED) {
        return 0;
    }

    return 1;
}

static void decodeInputData(PQUEUED_AUDIO_PACKET packet) {
    PRTP_PACKET rtp;

    rtp = (PRTP_PACKET)&packet->data[0];
    if (lastSeq != 0 && (unsigned short)(lastSeq + 1) != rtp->sequenceNumber) {
        Limelog("Received OOS audio data (expected %d, but got %d)\n", lastSeq + 1, rtp->sequenceNumber);

        AudioCallbacks.decodeAndPlaySample(NULL, 0);
    }

    lastSeq = rtp->sequenceNumber;

    AudioCallbacks.decodeAndPlaySample((char*)(rtp + 1), packet->size - sizeof(*rtp));
}

static void ReceiveThreadProc(void* context) {
    PRTP_PACKET rtp;
    PQUEUED_AUDIO_PACKET packet;
    int queueStatus;

    packet = NULL;

    while (!PltIsThreadInterrupted(&receiveThread)) {
        if (packet == NULL) {
            packet = (PQUEUED_AUDIO_PACKET)malloc(sizeof(*packet));
            if (packet == NULL) {
                Limelog("Audio Receive: malloc() failed\n");
                ListenerCallbacks.connectionTerminated(-1);
                break;
            }
        }

        packet->size = recvUdpSocket(rtpSocket, &packet->data[0], MAX_PACKET_SIZE);
        if (packet->size < 0) {
            Limelog("Audio Receive: recvUdpSocket() failed: %d\n", (int)LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketError());
            break;
        }
        else if (packet->size == 0) {
            // Receive timed out; try again
            continue;
        }

        if (packet->size < sizeof(RTP_PACKET)) {
            // Runt packet
            continue;
        }

        rtp = (PRTP_PACKET)&packet->data[0];
        if (rtp->packetType != 97) {
            // Not audio
            continue;
        }

        // RTP sequence number must be in host order for the RTP queue
        rtp->sequenceNumber = htons(rtp->sequenceNumber);

        queueStatus = RtpqAddPacket(&rtpReorderQueue, (PRTP_PACKET)packet, &packet->q.rentry);
        if (queueStatus == RTPQ_RET_HANDLE_IMMEDIATELY) {
            if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
                if (!queuePacketToLbq(&packet)) {
                    // An exit signal was received
                    break;
                }
            }
            else {
                decodeInputData(packet);
            }
        }
        else {
            if (queueStatus != RTPQ_RET_REJECTED) {
                // The queue consumed our packet, so we must allocate a new one
                packet = NULL;
            }

            if (queueStatus == RTPQ_RET_QUEUED_PACKETS_READY) {
                // If packets are ready, pull them and send them to the decoder
                while ((packet = (PQUEUED_AUDIO_PACKET)RtpqGetQueuedPacket(&rtpReorderQueue)) != NULL) {
                    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
                        if (!queuePacketToLbq(&packet)) {
                            // An exit signal was received
                            break;
                        }
                    }
                    else {
                        decodeInputData(packet);
                        free(packet);
                    }
                }
                
                // Break on exit
                if (packet != NULL) {
                    break;
                }
            }
        }
    }
    
    if (packet != NULL) {
        free(packet);
    }
}

static void DecoderThreadProc(void* context) {
    int err;
    PQUEUED_AUDIO_PACKET packet;

    while (!PltIsThreadInterrupted(&decoderThread)) {
        err = LbqWaitForQueueElement(&packetQueue, (void**)&packet);
        if (err != LBQ_SUCCESS) {
            // An exit signal was received
            return;
        }

        decodeInputData(packet);

        free(packet);
    }
}

void stopAudioStream(void) {
    AudioCallbacks.stop();

    PltInterruptThread(&udpPingThread);
    PltInterruptThread(&receiveThread);
    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {        
        // Signal threads waiting on the LBQ
        LbqSignalQueueShutdown(&packetQueue);
        PltInterruptThread(&decoderThread);
    }
    
    PltJoinThread(&udpPingThread);
    PltJoinThread(&receiveThread);
    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
        PltJoinThread(&decoderThread);
    }

    PltCloseThread(&udpPingThread);
    PltCloseThread(&receiveThread);
    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
        PltCloseThread(&decoderThread);
    }
    
    if (rtpSocket != INVALID_SOCKET) {
        closeSocket(rtpSocket);
        rtpSocket = INVALID_SOCKET;
    }

    AudioCallbacks.cleanup();
}

int startAudioStream(void* audioContext, int arFlags) {
    int err;
    POPUS_MULTISTREAM_CONFIGURATION chosenConfig;

    if (StreamConfig.audioConfiguration == AUDIO_CONFIGURATION_STEREO) {
        chosenConfig = &opusStereoConfig;
    }
    else if (StreamConfig.audioConfiguration == AUDIO_CONFIGURATION_51_SURROUND) {
        if (HighQualitySurroundEnabled) {
            chosenConfig = &opus51HighSurroundConfig;
        }
        else {
            chosenConfig = &opus51SurroundConfig;
        }
    }
    else {
        Limelog("Invalid audio configuration: %d\n", StreamConfig.audioConfiguration);
        return -1;
    }

    err = AudioCallbacks.init(StreamConfig.audioConfiguration, chosenConfig, audioContext, arFlags);
    if (err != 0) {
        return err;
    }

    rtpSocket = bindUdpSocket(RemoteAddr.ss_family, RTP_RECV_BUFFER);
    if (rtpSocket == INVALID_SOCKET) {
        err = LastSocketFail();
        AudioCallbacks.cleanup();
        return err;
    }

    err = PltCreateThread(UdpPingThreadProc, NULL, &udpPingThread);
    if (err != 0) {
        AudioCallbacks.cleanup();
        closeSocket(rtpSocket);
        return err;
    }

    AudioCallbacks.start();

    err = PltCreateThread(ReceiveThreadProc, NULL, &receiveThread);
    if (err != 0) {
        AudioCallbacks.stop();
        PltInterruptThread(&udpPingThread);
        PltJoinThread(&udpPingThread);
        PltCloseThread(&udpPingThread);
        closeSocket(rtpSocket);
        AudioCallbacks.cleanup();
        return err;
    }

    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
        err = PltCreateThread(DecoderThreadProc, NULL, &decoderThread);
        if (err != 0) {
            AudioCallbacks.stop();
            PltInterruptThread(&udpPingThread);
            PltInterruptThread(&receiveThread);
            PltJoinThread(&udpPingThread);
            PltJoinThread(&receiveThread);
            PltCloseThread(&udpPingThread);
            PltCloseThread(&receiveThread);
            closeSocket(rtpSocket);
            AudioCallbacks.cleanup();
            return err;
        }
    }

    return 0;
}
