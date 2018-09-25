#pragma once

#include "Video.h"

#define RTPQ_DEFAULT_MAX_SIZE   16
#define RTPQ_DEFAULT_QUEUE_TIME 40

typedef struct _RTP_QUEUE_ENTRY {
    PRTP_PACKET packet;

    uint64_t queueTimeMs;

    struct _RTP_QUEUE_ENTRY* next;
    struct _RTP_QUEUE_ENTRY* prev;
} RTP_QUEUE_ENTRY, *PRTP_QUEUE_ENTRY;

typedef struct _RTP_REORDER_QUEUE {
    int maxSize;
    int maxQueueTimeMs;

    PRTP_QUEUE_ENTRY queueHead;
    PRTP_QUEUE_ENTRY queueTail;
    int queueSize;

    unsigned short nextRtpSequenceNumber;

    uint64_t oldestQueuedTimeMs;
} RTP_REORDER_QUEUE, *PRTP_REORDER_QUEUE;

#define RTPQ_RET_PACKET_CONSUMED 0x1
#define RTPQ_RET_PACKET_READY    0x2
#define RTPQ_RET_HANDLE_NOW      0x4

#define RTPQ_PACKET_CONSUMED(x) ((x) & RTPQ_RET_PACKET_CONSUMED)
#define RTPQ_PACKET_READY(x)    ((x) & RTPQ_RET_PACKET_READY)
#define RTPQ_HANDLE_NOW(x)      ((x) == RTPQ_RET_HANDLE_NOW)

void RtpqInitializeQueue(PRTP_REORDER_QUEUE queue, int maxSize, int maxQueueTimeMs);
void RtpqCleanupQueue(PRTP_REORDER_QUEUE queue);
int RtpqAddPacket(PRTP_REORDER_QUEUE queue, PRTP_PACKET packet, PRTP_QUEUE_ENTRY packetEntry);
PRTP_PACKET RtpqGetQueuedPacket(PRTP_REORDER_QUEUE queue);