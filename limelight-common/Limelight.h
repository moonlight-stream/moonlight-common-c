#pragma once

#include "Platform.h"
#include "PlatformSockets.h"
#include "Video.h"

typedef struct _STREAM_CONFIGURATION {
	int width;
	int height;
	int fps;
} STREAM_CONFIGURATION, *PSTREAM_CONFIGURATION;

typedef void (*DecoderRendererSetup)(int width, int height, int redrawRate, void* context, int drFlags);
typedef void (*DecoderRendererStart)(void);
typedef void (*DecoderRendererStop)(void);
typedef void (*DecoderRendererRelease)(void);
typedef void (*DecoderRendererSubmitDecodeUnit)(PDECODE_UNIT decodeUnit);

typedef struct _DECODER_RENDERER_CALLBACKS {
	DecoderRendererSetup setup;
	DecoderRendererStart start;
	DecoderRendererStop stop;
	DecoderRendererRelease release;
	DecoderRendererSubmitDecodeUnit submitDecodeUnit;
} DECODER_RENDERER_CALLBACKS, *PDECODER_RENDERER_CALLBACKS;

#include <stdio.h>
#define Limelog printf

char* allocateConfigDataForStreamConfig(PSTREAM_CONFIGURATION streamConfig);
int getConfigDataSize(PSTREAM_CONFIGURATION streamConfig);

int initializeControlStream(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig);
int startControlStream(void);
int stopControlStream(void);

int performHandshake(IP_ADDRESS host);

void initializeVideoDepacketizer(void);
void processRtpPayload(PNV_VIDEO_PACKET videoPacket, int length);
PDECODE_UNIT getNextDecodeUnit(void);
void freeDecodeUnit(PDECODE_UNIT decodeUnit);
void queueRtpPacket(PRTP_PACKET rtpPacket, int length);