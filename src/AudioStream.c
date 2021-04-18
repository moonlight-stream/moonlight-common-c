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

static bool receivedDataFromPeer;

#define RTP_PORT 48000

#define MAX_PACKET_SIZE 1400

// This is much larger than we should typically have buffered, but
// it needs to be. We need a cushion in case our thread gets blocked
// for longer than normal.
#define RTP_RECV_BUFFER (64 * 1024)

typedef struct _QUEUED_AUDIO_PACKET {
    // data must remain at the front
    char data[MAX_PACKET_SIZE];

    int size;
    union {
        RTP_QUEUE_ENTRY rentry;
        LINKED_BLOCKING_QUEUE_ENTRY lentry;
    } q;
} QUEUED_AUDIO_PACKET, *PQUEUED_AUDIO_PACKET;

static void UdpPingThreadProc(void* context) {
    // Ping in ASCII
    char pingData[] = { 0x50, 0x49, 0x4E, 0x47 };
    LC_SOCKADDR saddr;
    SOCK_RET err;

    memcpy(&saddr, &RemoteAddr, sizeof(saddr));
    SET_PORT(&saddr, RTP_PORT);

    // Send PING every second until we get data back then every 5 seconds after that.
    while (!PltIsThreadInterrupted(&udpPingThread)) {
        err = sendto(rtpSocket, pingData, sizeof(pingData), 0, (struct sockaddr*)&saddr, RemoteAddrLen);
        if (err != sizeof(pingData)) {
            Limelog("Audio Ping: sendto() failed: %d\n", (int)LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketFail());
            return;
        }

        PltSleepMsInterruptible(&udpPingThread, 500);
    }
}

// Initialize the audio stream and start
int initializeAudioStream(void) {
    LbqInitializeLinkedBlockingQueue(&packetQueue, 30);
    RtpqInitializeQueue(&rtpReorderQueue, RTPQ_DEFAULT_MAX_SIZE, RTPQ_DEFAULT_QUEUE_TIME);
    lastSeq = 0;
    receivedDataFromPeer = false;

    // For GFE 3.22 compatibility, we must start the audio ping thread before the RTSP handshake.
    // It will not reply to our RTSP PLAY request until the audio ping has been received.
    rtpSocket = bindUdpSocket(RemoteAddr.ss_family, RTP_RECV_BUFFER);
    if (rtpSocket == INVALID_SOCKET) {
        return LastSocketFail();
    }

    // We may receive audio before our threads are started, but that's okay. We'll
    // drop the first 1 second of audio packets to catch up with the backlog.
    int err = PltCreateThread("AudioPing", UdpPingThreadProc, NULL, &udpPingThread);
    if (err != 0) {
        closeSocket(rtpSocket);
        rtpSocket = INVALID_SOCKET;
        return err;
    }

    return 0;
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
    if (rtpSocket != INVALID_SOCKET) {
        PltInterruptThread(&udpPingThread);
        PltJoinThread(&udpPingThread);
        PltCloseThread(&udpPingThread);

        closeSocket(rtpSocket);
        rtpSocket = INVALID_SOCKET;
    }

    freePacketList(LbqDestroyLinkedBlockingQueue(&packetQueue));
    RtpqCleanupQueue(&rtpReorderQueue);
}

static bool queuePacketToLbq(PQUEUED_AUDIO_PACKET* packet) {
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
        return false;
    }

    return true;
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
    bool useSelect;
    int packetsToDrop = 1000 / AudioPacketDuration;
    int waitingForAudioMs;

    packet = NULL;

    if (setNonFatalRecvTimeoutMs(rtpSocket, UDP_RECV_POLL_TIMEOUT_MS) < 0) {
        // SO_RCVTIMEO failed, so use select() to wait
        useSelect = true;
    }
    else {
        // SO_RCVTIMEO timeout set for recv()
        useSelect = false;
    }

    waitingForAudioMs = 0;
    while (!PltIsThreadInterrupted(&receiveThread)) {
        if (packet == NULL) {
            packet = (PQUEUED_AUDIO_PACKET)malloc(sizeof(*packet));
            if (packet == NULL) {
                Limelog("Audio Receive: malloc() failed\n");
                ListenerCallbacks.connectionTerminated(-1);
                break;
            }
        }

        packet->size = recvUdpSocket(rtpSocket, &packet->data[0], MAX_PACKET_SIZE, useSelect);
        if (packet->size < 0) {
            Limelog("Audio Receive: recvUdpSocket() failed: %d\n", (int)LastSocketError());
            ListenerCallbacks.connectionTerminated(LastSocketFail());
            break;
        }
        else if (packet->size == 0) {
            // Receive timed out; try again
            
            if (!receivedDataFromPeer) {
                waitingForAudioMs += UDP_RECV_POLL_TIMEOUT_MS;
            }

            // If we hit this path, there are no queued audio packets on the host PC,
            // so we don't need to drop anything.
            packetsToDrop = 0;
            continue;
        }

        if (packet->size < (int)sizeof(RTP_PACKET)) {
            // Runt packet
            continue;
        }

        rtp = (PRTP_PACKET)&packet->data[0];
        if (rtp->packetType != 97) {
            // Not audio
            continue;
        }

        if (!receivedDataFromPeer) {
            receivedDataFromPeer = true;
            Limelog("Received first audio packet after %d ms\n", waitingForAudioMs);
        }

        // GFE accumulates audio samples before we are ready to receive them, so
        // we will drop the first 1 second of packets to avoid accumulating latency
        // by sending audio frames to the player faster than they can be played.
        if (packetsToDrop > 0) {
            packetsToDrop--;
            continue;
        }

        // Convert fields to host byte-order
        rtp->sequenceNumber = BE16(rtp->sequenceNumber);
        rtp->timestamp = BE32(rtp->timestamp);
        rtp->ssrc = BE32(rtp->ssrc);

        queueStatus = RtpqAddPacket(&rtpReorderQueue, (PRTP_PACKET)packet, &packet->q.rentry);
        if (RTPQ_HANDLE_NOW(queueStatus)) {
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
            if (RTPQ_PACKET_CONSUMED(queueStatus)) {
                // The queue consumed our packet, so we must allocate a new one
                packet = NULL;
            }

            if (RTPQ_PACKET_READY(queueStatus)) {
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
    if (!receivedDataFromPeer) {
        Limelog("No audio traffic was ever received from the host!\n");
    }

    AudioCallbacks.stop();

    PltInterruptThread(&receiveThread);
    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {        
        // Signal threads waiting on the LBQ
        LbqSignalQueueShutdown(&packetQueue);
        PltInterruptThread(&decoderThread);
    }
    
    PltJoinThread(&receiveThread);
    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
        PltJoinThread(&decoderThread);
    }

    PltCloseThread(&receiveThread);
    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
        PltCloseThread(&decoderThread);
    }

    AudioCallbacks.cleanup();
}

int startAudioStream(void* audioContext, int arFlags) {
    int err;
    OPUS_MULTISTREAM_CONFIGURATION chosenConfig;

    if (HighQualitySurroundEnabled) {
        LC_ASSERT(HighQualitySurroundSupported);
        LC_ASSERT(HighQualityOpusConfig.channelCount != 0);
        LC_ASSERT(HighQualityOpusConfig.streams != 0);
        chosenConfig = HighQualityOpusConfig;
    }
    else {
        LC_ASSERT(NormalQualityOpusConfig.channelCount != 0);
        LC_ASSERT(NormalQualityOpusConfig.streams != 0);
        chosenConfig = NormalQualityOpusConfig;
    }

    chosenConfig.samplesPerFrame = 48 * AudioPacketDuration;

    err = AudioCallbacks.init(StreamConfig.audioConfiguration, &chosenConfig, audioContext, arFlags);
    if (err != 0) {
        return err;
    }

    AudioCallbacks.start();

    err = PltCreateThread("AudioRecv", ReceiveThreadProc, NULL, &receiveThread);
    if (err != 0) {
        AudioCallbacks.stop();
        closeSocket(rtpSocket);
        AudioCallbacks.cleanup();
        return err;
    }

    if ((AudioCallbacks.capabilities & CAPABILITY_DIRECT_SUBMIT) == 0) {
        err = PltCreateThread("AudioDec", DecoderThreadProc, NULL, &decoderThread);
        if (err != 0) {
            AudioCallbacks.stop();
            PltInterruptThread(&receiveThread);
            PltJoinThread(&receiveThread);
            PltCloseThread(&receiveThread);
            closeSocket(rtpSocket);
            AudioCallbacks.cleanup();
            return err;
        }
    }

    return 0;
}

int LiGetPendingAudioFrames(void) {
    return LbqGetItemCount(&packetQueue);
}

int LiGetPendingAudioDuration(void) {
    return LiGetPendingAudioFrames() * AudioPacketDuration;
}
