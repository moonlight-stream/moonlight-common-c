#pragma once

#define FLAG_CONTAINS_PIC_DATA 0x1
#define FLAG_EOF 0x2
#define FLAG_SOF 0x4

typedef struct _NV_VIDEO_PACKET {
	int streamPacketIndex;
	int frameIndex;
	char flags;
	char reserved[3];
	int reserved2;
} NV_VIDEO_PACKET, *PNV_VIDEO_PACKET;

#define FLAG_EXTENSION 0x10

#define FIXED_RTP_HEADER_SIZE 12
#define MAX_RTP_HEADER_SIZE 16

typedef struct _RTP_PACKET {
	char header;
	char packetType;
	unsigned short sequenceNumber;
	char reserved[8];
} RTP_PACKET, *PRTP_PACKET;