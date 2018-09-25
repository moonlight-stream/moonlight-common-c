#include "Limelight-internal.h"

#define ENET_INTERNAL_TIMEOUT_MS 100

// This function wraps enet_host_service() and hides the fact that it must be called
// multiple times for retransmissions to work correctly. It is meant to be a drop-in
// replacement for enet_host_service(). It also handles cancellation of the connection
// attempt during the wait.
int serviceEnetHost(ENetHost* client, ENetEvent* event, enet_uint32 timeoutMs) {
    int ret;

    // We need to call enet_host_service() multiple times to make sure retransmissions happen
    for (;;) {
        int selectedTimeout = timeoutMs < ENET_INTERNAL_TIMEOUT_MS ? timeoutMs : ENET_INTERNAL_TIMEOUT_MS;

        // We want to report an interrupt event if we are able to read data
        if (ConnectionInterrupted) {
            Limelog("ENet wait interrupted\n");
            ret = -1;
            break;
        }

        ret = enet_host_service(client, event, selectedTimeout);
        if (ret != 0 || timeoutMs == 0) {
            break;
        }

        timeoutMs -= selectedTimeout;
    }
    
    return ret;
}

int extractVersionQuadFromString(const char* string, int* quad) {
    char versionString[128];
    char* nextDot;
    char* nextNumber;
    int i;
    
    strcpy(versionString, string);
    nextNumber = versionString;
    
    for (i = 0; i < 4; i++) {
        if (i == 3) {
            nextDot = strchr(nextNumber, '\0');
        }
        else {
            nextDot = strchr(nextNumber, '.');
        }
        if (nextDot == NULL) {
            return -1;
        }
        
        // Cut the string off at the next dot
        *nextDot = '\0';
        
        quad[i] = atoi(nextNumber);
        
        // Move on to the next segment
        nextNumber = nextDot + 1;
    }
    
    return 0;
}

int isReferenceFrameInvalidationEnabled(void) {
    LC_ASSERT(NegotiatedVideoFormat != 0);
    return ((NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H264) && (VideoCallbacks.capabilities & CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC)) ||
           ((NegotiatedVideoFormat & VIDEO_FORMAT_MASK_H265) && (VideoCallbacks.capabilities & CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC));
}

void LiInitializeStreamConfiguration(PSTREAM_CONFIGURATION streamConfig) {
    memset(streamConfig, 0, sizeof(*streamConfig));
}

void LiInitializeVideoCallbacks(PDECODER_RENDERER_CALLBACKS drCallbacks) {
    memset(drCallbacks, 0, sizeof(*drCallbacks));
}

void LiInitializeAudioCallbacks(PAUDIO_RENDERER_CALLBACKS arCallbacks) {
    memset(arCallbacks, 0, sizeof(*arCallbacks));
}

void LiInitializeConnectionCallbacks(PCONNECTION_LISTENER_CALLBACKS clCallbacks) {
    memset(clCallbacks, 0, sizeof(*clCallbacks));
}

void LiInitializeServerInformation(PSERVER_INFORMATION serverInfo) {
    memset(serverInfo, 0, sizeof(*serverInfo));
}

uint64_t LiGetMillis(void) {
    return PltGetMillis();
}
