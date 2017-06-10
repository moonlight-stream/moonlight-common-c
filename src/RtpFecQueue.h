#pragma once

#include "Video.h"

typedef struct _RTPFEC_QUEUE_ENTRY {
    PRTP_PACKET packet;
    int length;
    int isParity;
    unsigned long long receiveTimeMs;

    struct _RTPFEC_QUEUE_ENTRY* next;
    struct _RTPFEC_QUEUE_ENTRY* prev;
} RTPFEC_QUEUE_ENTRY, *PRTPFEC_QUEUE_ENTRY;

typedef struct _RTP_FEC_QUEUE {
    PRTPFEC_QUEUE_ENTRY queueHead;
    PRTPFEC_QUEUE_ENTRY queueTail;
    int queueSize;

    PRTPFEC_QUEUE_ENTRY bufferHead;
    PRTPFEC_QUEUE_ENTRY bufferTail;
    int bufferSize;
    int bufferLowestSequenceNumber;
    int bufferHighestSequenceNumber;
    int bufferFirstParitySequenceNumber;
    int bufferDataPackets;
    int receivedBufferDataPackets;
    int fecPercentage;

    int currentFrameNumber;
    unsigned int nextRtpSequenceNumber;
} RTP_FEC_QUEUE, *PRTP_FEC_QUEUE;

#define RTPF_RET_QUEUED_NOTHING_READY 0
#define RTPF_RET_QUEUED_PACKETS_READY 1
#define RTPF_RET_REJECTED             2

void RtpfInitializeQueue(PRTP_FEC_QUEUE queue);
void RtpfCleanupQueue(PRTP_FEC_QUEUE queue);
int RtpfAddPacket(PRTP_FEC_QUEUE queue, PRTP_PACKET packet, int length, PRTPFEC_QUEUE_ENTRY packetEntry);
PRTPFEC_QUEUE_ENTRY RtpfGetQueuedPacket(PRTP_FEC_QUEUE queue);
