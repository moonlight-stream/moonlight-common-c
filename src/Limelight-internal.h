#pragma once

#include "Limelight.h"
#include "Platform.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "Video.h"
#include "RtpFecQueue.h"

#include <enet/enet.h>

// Common globals
extern char* RemoteAddrString;
extern struct sockaddr_storage RemoteAddr;
extern SOCKADDR_LEN RemoteAddrLen;
extern int AppVersionQuad[4];
extern STREAM_CONFIGURATION StreamConfig;
extern CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
extern DECODER_RENDERER_CALLBACKS VideoCallbacks;
extern AUDIO_RENDERER_CALLBACKS AudioCallbacks;
extern int NegotiatedVideoFormat;
extern volatile int ConnectionInterrupted;
extern int HighQualitySurroundEnabled;

#ifndef UINT24_MAX
#define UINT24_MAX 0xFFFFFF
#endif

#define U16(x) ((unsigned short) ((x) & UINT16_MAX))
#define U24(x) ((unsigned int) ((x) & UINT24_MAX))
#define U32(x) ((unsigned int) ((x) & UINT32_MAX))

#define isBefore16(x, y) (U16((x) - (y)) > (UINT16_MAX/2))
#define isBefore24(x, y) (U24((x) - (y)) > (UINT24_MAX/2))
#define isBefore32(x, y) (U32((x) - (y)) > (UINT32_MAX/2))

int serviceEnetHost(ENetHost* client, ENetEvent* event, enet_uint32 timeoutMs);
int extractVersionQuadFromString(const char* string, int* quad);
int isReferenceFrameInvalidationEnabled(void);

void fixupMissingCallbacks(PDECODER_RENDERER_CALLBACKS* drCallbacks, PAUDIO_RENDERER_CALLBACKS* arCallbacks,
    PCONNECTION_LISTENER_CALLBACKS* clCallbacks);

char* getSdpPayloadForStreamConfig(int rtspClientVersion, int* length);

int initializeControlStream(void);
int startControlStream(void);
int stopControlStream(void);
void destroyControlStream(void);
void requestIdrOnDemand(void);
void connectionDetectedFrameLoss(int startFrame, int endFrame);
void connectionReceivedCompleteFrame(int frameIndex);
void connectionSawFrame(int frameIndex);
void connectionLostPackets(int lastReceivedPacket, int nextReceivedPacket);
int sendInputPacketOnControlStream(unsigned char* data, int length);

int performRtspHandshake(void);

void initializeVideoDepacketizer(int pktSize);
void destroyVideoDepacketizer(void);
void processRtpPayload(PNV_VIDEO_PACKET videoPacket, int length, unsigned long long receiveTimeMs);
void queueRtpPacket(PRTPFEC_QUEUE_ENTRY queueEntry);
void stopVideoDepacketizer(void);
void requestDecoderRefresh(void);

void initializeVideoStream(void);
void destroyVideoStream(void);
int startVideoStream(void* rendererContext, int drFlags);
void stopVideoStream(void);

void initializeAudioStream(void);
void destroyAudioStream(void);
int startAudioStream(void* audioContext, int arFlags);
void stopAudioStream(void);

int initializeInputStream(void);
void destroyInputStream(void);
int startInputStream(void);
int stopInputStream(void);
