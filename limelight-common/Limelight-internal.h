#pragma once

#include "Limelight.h"
#include "Platform.h"
#include "PlatformSockets.h"
#include "Video.h"

#include <stdio.h>
#define Limelog printf

char* allocateConfigDataForStreamConfig(PSTREAM_CONFIGURATION streamConfig);
int getConfigDataSize(PSTREAM_CONFIGURATION streamConfig);

int initializeControlStream(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig);
int startControlStream(void);
int stopControlStream(void);
void destroyControlStream(void);
void requestIdrFrame(void);

int performHandshake(IP_ADDRESS host);
void terminateHandshake(void);

void initializeVideoDepacketizer(void);
void destroyVideoDepacketizer(void);
void processRtpPayload(PNV_VIDEO_PACKET videoPacket, int length);
int getNextDecodeUnit(PDECODE_UNIT *du);
void freeDecodeUnit(PDECODE_UNIT decodeUnit);
void queueRtpPacket(PRTP_PACKET rtpPacket, int length);

void initializeVideoStream(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig, PDECODER_RENDERER_CALLBACKS drCallbacks);
void destroyVideoStream(void);
int startVideoStream(void* rendererContext, int drFlags);
void stopVideoStream(void);