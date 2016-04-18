#pragma once

#include "Limelight.h"
#include "Platform.h"
#include "PlatformSockets.h"
#include "PlatformThreads.h"
#include "Video.h"

#include <enet/enet.h>

// Common globals
extern char* RemoteAddrString;
extern struct sockaddr_storage RemoteAddr;
extern SOCKADDR_LEN RemoteAddrLen;
extern int ServerMajorVersion;
extern STREAM_CONFIGURATION StreamConfig;
extern CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
extern DECODER_RENDERER_CALLBACKS VideoCallbacks;
extern AUDIO_RENDERER_CALLBACKS AudioCallbacks;
extern int NegotiatedVideoFormat;

int isBeforeSignedInt(int numA, int numB, int ambiguousCase);
int serviceEnetHost(ENetHost* client, ENetEvent* event, enet_uint32 timeoutMs);

void fixupMissingCallbacks(PDECODER_RENDERER_CALLBACKS* drCallbacks, PAUDIO_RENDERER_CALLBACKS* arCallbacks,
    PCONNECTION_LISTENER_CALLBACKS* clCallbacks);

char* getSdpPayloadForStreamConfig(int rtspClientVersion, int* length);

int initializeControlStream(void);
int startControlStream(void);
int stopControlStream(void);
void destroyControlStream(void);
void requestIdrOnDemand(void);
void connectionSinkTooSlow(int startFrame, int endFrame);
void connectionDetectedFrameLoss(int startFrame, int endFrame);
void connectionReceivedCompleteFrame(int frameIndex);
void connectionSawFrame(int frameIndex);
void connectionLostPackets(int lastReceivedPacket, int nextReceivedPacket);
int sendInputPacketOnControlStream(unsigned char* data, int length);

int performRtspHandshake(void);
void terminateRtspHandshake(void);

void initializeVideoDepacketizer(int pktSize);
void destroyVideoDepacketizer(void);
void processRtpPayload(PNV_VIDEO_PACKET videoPacket, int length);
void queueRtpPacket(PRTP_PACKET rtpPacket, int length);
void stopVideoDepacketizer(void);
void requestDecoderRefresh(void);

void initializeVideoStream(void);
void destroyVideoStream(void);
int startVideoStream(void* rendererContext, int drFlags);
void stopVideoStream(void);

void initializeAudioStream(void);
void destroyAudioStream(void);
int startAudioStream(void);
void stopAudioStream(void);

int initializeInputStream(void);
void destroyInputStream(void);
int startInputStream(void);
int stopInputStream(void);
