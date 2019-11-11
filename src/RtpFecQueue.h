#pragma once

#include "Video.h"

typedef struct _RTPFEC_QUEUE_ENTRY {
    PRTP_PACKET packet;
    int length;
    int isParity;
    unsigned long long receiveTimeMs;
    unsigned int presentationTimeMs;

    struct _RTPFEC_QUEUE_ENTRY* next;
    struct _RTPFEC_QUEUE_ENTRY* prev;
} RTPFEC_QUEUE_ENTRY, *PRTPFEC_QUEUE_ENTRY;

typedef struct _RTP_FEC_QUEUE {
    PRTPFEC_QUEUE_ENTRY bufferHead;
    PRTPFEC_QUEUE_ENTRY bufferTail;
    unsigned long long bufferFirstRecvTimeMs;
    int bufferSize;
    int bufferLowestSequenceNumber;
    int bufferHighestSequenceNumber;
    int bufferFirstParitySequenceNumber;
    int bufferDataPackets;
    int bufferParityPackets;
    int receivedBufferDataPackets;
    int fecPercentage;
    int nextContiguousSequenceNumber;

    int currentFrameNumber;
} RTP_FEC_QUEUE, *PRTP_FEC_QUEUE;

#define RTPF_RET_QUEUED    0
#define RTPF_RET_REJECTED  1

void RtpfInitializeQueue(PRTP_FEC_QUEUE queue);
void RtpfCleanupQueue(PRTP_FEC_QUEUE queue);
int RtpfAddPacket(PRTP_FEC_QUEUE queue, PRTP_PACKET packet, int length, PRTPFEC_QUEUE_ENTRY packetEntry);
void RtpfSubmitQueuedPackets(PRTP_FEC_QUEUE queue);
