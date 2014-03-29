#pragma once

#define FLAG_EOF 0x2
#define FLAG_SOF 0x4

typedef struct _NV_VIDEO_PACKET {
	int frameIndex;
	int packetIndex;
	int totalPackets;
	int flags;
	int payloadLength;
	char reserved2[36];
} NV_VIDEO_PACKET, *PNV_VIDEO_PACKET;

typedef struct _RTP_PACKET {
	char flags;
	char packetType;
	unsigned short sequenceNumber;
	char reserved[8];
} RTP_PACKET, *PRTP_PACKET;