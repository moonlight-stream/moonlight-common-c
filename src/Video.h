#pragma once

#include "LinkedBlockingQueue.h"

typedef struct _QUEUED_DECODE_UNIT {
    DECODE_UNIT decodeUnit;
    LINKED_BLOCKING_QUEUE_ENTRY entry;
} QUEUED_DECODE_UNIT, *PQUEUED_DECODE_UNIT;

void completeQueuedDecodeUnit(PQUEUED_DECODE_UNIT qdu, int drStatus);
int getNextQueuedDecodeUnit(PQUEUED_DECODE_UNIT* qdu);

#pragma pack(push, 1)

#define FLAG_CONTAINS_PIC_DATA 0x1
#define FLAG_EOF 0x2
#define FLAG_SOF 0x4

typedef struct _NV_VIDEO_PACKET {
    unsigned int streamPacketIndex;
    unsigned int frameIndex;
    char flags;
    char reserved[3];
    int fecInfo;
} NV_VIDEO_PACKET, *PNV_VIDEO_PACKET;

#define FLAG_EXTENSION 0x10

#define FIXED_RTP_HEADER_SIZE 12
#define MAX_RTP_HEADER_SIZE 16

typedef struct _RTP_PACKET {
    char header;
    char packetType;
    unsigned short sequenceNumber;
    unsigned int timestamp;
    unsigned int ssrc;
} RTP_PACKET, *PRTP_PACKET;

#pragma pack(pop)
