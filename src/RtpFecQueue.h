#pragma once

#include "Video.h"

typedef struct _RTPFEC_QUEUE_ENTRY {
    PRTP_PACKET packet;
    int length;
    bool isParity;
    uint64_t receiveTimeMs;
    uint32_t presentationTimeMs;

    struct _RTPFEC_QUEUE_ENTRY* next;
    struct _RTPFEC_QUEUE_ENTRY* prev;
} RTPFEC_QUEUE_ENTRY, *PRTPFEC_QUEUE_ENTRY;

typedef struct _RTPFEC_QUEUE_LIST {
    PRTPFEC_QUEUE_ENTRY head;
    PRTPFEC_QUEUE_ENTRY tail;
    uint32_t count;
} RTPFEC_QUEUE_LIST, *PRTPFEC_QUEUE_LIST;

typedef struct _RTP_FEC_QUEUE {
    RTPFEC_QUEUE_LIST pendingFecBlockList;

    uint64_t bufferFirstRecvTimeMs;
    uint32_t bufferLowestSequenceNumber;
    uint32_t bufferHighestSequenceNumber;
    uint32_t bufferFirstParitySequenceNumber;
    uint32_t bufferDataPackets;
    uint32_t bufferParityPackets;
    uint32_t receivedBufferDataPackets;
    uint32_t fecPercentage;
    uint32_t nextContiguousSequenceNumber;

    uint32_t currentFrameNumber;
} RTP_FEC_QUEUE, *PRTP_FEC_QUEUE;

#define RTPF_RET_QUEUED    0
#define RTPF_RET_REJECTED  1

void RtpfInitializeQueue(PRTP_FEC_QUEUE queue);
void RtpfCleanupQueue(PRTP_FEC_QUEUE queue);
int RtpfAddPacket(PRTP_FEC_QUEUE queue, PRTP_PACKET packet, int length, PRTPFEC_QUEUE_ENTRY packetEntry);
void RtpfSubmitQueuedPackets(PRTP_FEC_QUEUE queue);
