#pragma once

typedef struct _NV_VIDEO_PACKET {
	int frameIndex;
	int packetIndex;
	int totalPackets;
	int reserved1;
	int payloadLength;
	char reserved2[36];
} NV_VIDEO_PACKET, *PNV_VIDEO_PACKET;

typedef struct _RTP_PACKET {
	char flags;
	char packetType;
	unsigned short sequenceNumber;
	char reserved[8];
} RTP_PACKET, *PRTP_PACKET;