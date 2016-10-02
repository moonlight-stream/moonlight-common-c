#include "Limelight-internal.h"

#define MAX_OPTION_NAME_LEN 128

#define MAX_SDP_HEADER_LEN 128
#define MAX_SDP_TAIL_LEN 128

#define CHANNEL_COUNT_STEREO 2
#define CHANNEL_COUNT_51_SURROUND 6

#define CHANNEL_MASK_STEREO 0x3
#define CHANNEL_MASK_51_SURROUND 0xFC

typedef struct _SDP_OPTION {
    char name[MAX_OPTION_NAME_LEN + 1];
    void* payload;
    int payloadLen;
    struct _SDP_OPTION* next;
} SDP_OPTION, *PSDP_OPTION;

// Cleanup the attribute list
static void freeAttributeList(PSDP_OPTION head) {
    PSDP_OPTION next;
    while (head != NULL) {
        next = head->next;
        free(head);
        head = next;
    }
}

// Get the size of the attribute list
static int getSerializedAttributeListSize(PSDP_OPTION head) {
    PSDP_OPTION currentEntry = head;
    size_t size = 0;
    while (currentEntry != NULL) {
        size += strlen("a=");
        size += strlen(currentEntry->name);
        size += strlen(":");
        size += currentEntry->payloadLen;
        size += strlen(" \r\n");

        currentEntry = currentEntry->next;
    }
    return (int)size;
}

// Populate the serialized attribute list into a string
static int fillSerializedAttributeList(char* buffer, PSDP_OPTION head) {
    PSDP_OPTION currentEntry = head;
    int offset = 0;
    while (currentEntry != NULL) {
        offset += sprintf(&buffer[offset], "a=%s:", currentEntry->name);
        memcpy(&buffer[offset], currentEntry->payload, currentEntry->payloadLen);
        offset += currentEntry->payloadLen;
        offset += sprintf(&buffer[offset], " \r\n");

        currentEntry = currentEntry->next;
    }
    return offset;
}

// Add an attribute
static int addAttributeBinary(PSDP_OPTION* head, char* name, const void* payload, int payloadLen) {
    PSDP_OPTION option, currentOption;

    option = malloc(sizeof(*option) + payloadLen);
    if (option == NULL) {
        return -1;
    }

    option->next = NULL;
    option->payloadLen = payloadLen;
    strcpy(option->name, name);
    option->payload = (void*)(option + 1);
    memcpy(option->payload, payload, payloadLen);

    if (*head == NULL) {
        *head = option;
    }
    else {
        currentOption = *head;
        while (currentOption->next != NULL) {
            currentOption = currentOption->next;
        }
        currentOption->next = option;
    }

    return 0;
}

// Add an attribute string
static int addAttributeString(PSDP_OPTION* head, char* name, const char* payload) {
    // We purposefully omit the null terminating character
    return addAttributeBinary(head, name, payload, (int)strlen(payload));
}

static int addGen3Options(PSDP_OPTION* head, char* addrStr) {
    int payloadInt;
    int err = 0;

    err |= addAttributeString(head, "x-nv-general.serverAddress", addrStr);

    payloadInt = htonl(0x42774141);
    err |= addAttributeBinary(head,
        "x-nv-general.featureFlags", &payloadInt, sizeof(payloadInt));

    payloadInt = htonl(0x41514141);
    err |= addAttributeBinary(head,
        "x-nv-video[0].transferProtocol", &payloadInt, sizeof(payloadInt));
    err |= addAttributeBinary(head,
        "x-nv-video[1].transferProtocol", &payloadInt, sizeof(payloadInt));
    err |= addAttributeBinary(head,
        "x-nv-video[2].transferProtocol", &payloadInt, sizeof(payloadInt));
    err |= addAttributeBinary(head,
        "x-nv-video[3].transferProtocol", &payloadInt, sizeof(payloadInt));

    payloadInt = htonl(0x42414141);
    err |= addAttributeBinary(head,
        "x-nv-video[0].rateControlMode", &payloadInt, sizeof(payloadInt));
    payloadInt = htonl(0x42514141);
    err |= addAttributeBinary(head,
        "x-nv-video[1].rateControlMode", &payloadInt, sizeof(payloadInt));
    err |= addAttributeBinary(head,
        "x-nv-video[2].rateControlMode", &payloadInt, sizeof(payloadInt));
    err |= addAttributeBinary(head,
        "x-nv-video[3].rateControlMode", &payloadInt, sizeof(payloadInt));

    err |= addAttributeString(head, "x-nv-vqos[0].bw.flags", "14083");

    err |= addAttributeString(head, "x-nv-vqos[0].videoQosMaxConsecutiveDrops", "0");
    err |= addAttributeString(head, "x-nv-vqos[1].videoQosMaxConsecutiveDrops", "0");
    err |= addAttributeString(head, "x-nv-vqos[2].videoQosMaxConsecutiveDrops", "0");
    err |= addAttributeString(head, "x-nv-vqos[3].videoQosMaxConsecutiveDrops", "0");

    return err;
}

static int addGen4Options(PSDP_OPTION* head, char* addrStr) {
    char payloadStr[92];
    int err = 0;

    sprintf(payloadStr, "rtsp://%s:48010", addrStr);
    err |= addAttributeString(head, "x-nv-general.serverAddress", payloadStr);

    return err;
}

static int addGen5Options(PSDP_OPTION* head) {
    int err = 0;

    // We want to use the new ENet connections for control and input
    err |= addAttributeString(head, "x-nv-general.useReliableUdp", "1");
    err |= addAttributeString(head, "x-nv-ri.useControlChannel", "1");
    
    // Disable dynamic resolution switching
    err |= addAttributeString(head, "x-nv-vqos[0].drc.enable", "0");
    
    return err;
}

static PSDP_OPTION getAttributesList(char*urlSafeAddr) {
    PSDP_OPTION optionHead;
    char payloadStr[92];
    int audioChannelCount;
    int audioChannelMask;
    int err;

    optionHead = NULL;
    err = 0;

    sprintf(payloadStr, "%d", StreamConfig.width);
    err |= addAttributeString(&optionHead, "x-nv-video[0].clientViewportWd", payloadStr);
    sprintf(payloadStr, "%d", StreamConfig.height);
    err |= addAttributeString(&optionHead, "x-nv-video[0].clientViewportHt", payloadStr);

    sprintf(payloadStr, "%d", StreamConfig.fps);
    err |= addAttributeString(&optionHead, "x-nv-video[0].maxFPS", payloadStr);

    sprintf(payloadStr, "%d", StreamConfig.packetSize);
    err |= addAttributeString(&optionHead, "x-nv-video[0].packetSize", payloadStr);

    err |= addAttributeString(&optionHead, "x-nv-video[0].rateControlMode", "4");

    err |= addAttributeString(&optionHead, "x-nv-video[0].timeoutLengthMs", "7000");
    err |= addAttributeString(&optionHead, "x-nv-video[0].framesWithInvalidRefThreshold", "0");

    sprintf(payloadStr, "%d", StreamConfig.bitrate);
    if (ServerMajorVersion >= 5) {
        err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.minimumBitrateKbps", payloadStr);
        err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.maximumBitrateKbps", payloadStr);
    }
    else {
        if (StreamConfig.streamingRemotely) {
            err |= addAttributeString(&optionHead, "x-nv-video[0].averageBitrate", "4");
            err |= addAttributeString(&optionHead, "x-nv-video[0].peakBitrate", "4");
        }
        // We don't support dynamic bitrate scaling properly (it tends to bounce between min and max and never
        // settle on the optimal bitrate if it's somewhere in the middle), so we'll just latch the bitrate
        // to the requested value.

        err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.minimumBitrate", payloadStr);
        err |= addAttributeString(&optionHead, "x-nv-vqos[0].bw.maximumBitrate", payloadStr);
    }

    // Using FEC turns padding on which makes us have to take the slow path
    // in the depacketizer, not to mention exposing some ambiguous cases with
    // distinguishing padding from valid sequences. Since we can only perform
    // execute an FEC recovery on a 1 packet frame, we'll just turn it off completely.
    err |= addAttributeString(&optionHead, "x-nv-vqos[0].fec.enable", "0");

    err |= addAttributeString(&optionHead, "x-nv-vqos[0].videoQualityScoreUpdateTime", "5000");

    if (StreamConfig.streamingRemotely) {
        err |= addAttributeString(&optionHead, "x-nv-vqos[0].qosTrafficType", "0");
        err |= addAttributeString(&optionHead, "x-nv-aqos.qosTrafficType", "0");
    }
    else {
        err |= addAttributeString(&optionHead, "x-nv-vqos[0].qosTrafficType", "5");
        err |= addAttributeString(&optionHead, "x-nv-aqos.qosTrafficType", "4");
    }

    if (ServerMajorVersion == 3) {
        err |= addGen3Options(&optionHead, urlSafeAddr);
    }
    else if (ServerMajorVersion == 4) {
        err |= addGen4Options(&optionHead, urlSafeAddr);
    }
    else {
        err |= addGen5Options(&optionHead);
    }

    if (ServerMajorVersion >= 4) {
        if (NegotiatedVideoFormat == VIDEO_FORMAT_H265) {
            err |= addAttributeString(&optionHead, "x-nv-clientSupportHevc", "1");
            err |= addAttributeString(&optionHead, "x-nv-vqos[0].bitStreamFormat", "1");
            
            // Disable slicing on HEVC
            err |= addAttributeString(&optionHead, "x-nv-video[0].videoEncoderSlicesPerFrame", "1");
        }
        else {
            unsigned char slicesPerFrame;
            
            err |= addAttributeString(&optionHead, "x-nv-clientSupportHevc", "0");
            err |= addAttributeString(&optionHead, "x-nv-vqos[0].bitStreamFormat", "0");
            
            // Use slicing for increased performance on some decoders
            slicesPerFrame = (unsigned char)(VideoCallbacks.capabilities >> 24);
            if (slicesPerFrame == 0) {
                // If not using slicing, we request 1 slice per frame
                slicesPerFrame = 1;
            }
            sprintf(payloadStr, "%d", slicesPerFrame);
            err |= addAttributeString(&optionHead, "x-nv-video[0].videoEncoderSlicesPerFrame", payloadStr);
        }
        
        if (StreamConfig.audioConfiguration == AUDIO_CONFIGURATION_51_SURROUND) {
            audioChannelCount = CHANNEL_COUNT_51_SURROUND;
            audioChannelMask = CHANNEL_MASK_51_SURROUND;
        }
        else {
            audioChannelCount = CHANNEL_COUNT_STEREO;
            audioChannelMask = CHANNEL_MASK_STEREO;
        }

        sprintf(payloadStr, "%d", audioChannelCount);
        err |= addAttributeString(&optionHead, "x-nv-audio.surround.numChannels", payloadStr);
        sprintf(payloadStr, "%d", audioChannelMask);
        err |= addAttributeString(&optionHead, "x-nv-audio.surround.channelMask", payloadStr);
        if (audioChannelCount > 2) {
            err |= addAttributeString(&optionHead, "x-nv-audio.surround.enable", "1");
        }
        else {
            err |= addAttributeString(&optionHead, "x-nv-audio.surround.enable", "0");
        }
    }

    if (err == 0) {
        return optionHead;
    }

    freeAttributeList(optionHead);
    return NULL;
}

// Populate the SDP header with required information
static int fillSdpHeader(char* buffer, int rtspClientVersion, char*urlSafeAddr) {
    return sprintf(buffer,
        "v=0\r\n"
        "o=android 0 %d IN %s %s\r\n"
        "s=NVIDIA Streaming Client\r\n",
        rtspClientVersion,
        RemoteAddr.ss_family == AF_INET ? "IPv4" : "IPv6",
        urlSafeAddr);
}

// Populate the SDP tail with required information
static int fillSdpTail(char* buffer) {
    return sprintf(buffer,
        "t=0 0\r\n"
        "m=video %d  \r\n",
        ServerMajorVersion < 4 ? 47996 : 47998);
}

// Get the SDP attributes for the stream config
char* getSdpPayloadForStreamConfig(int rtspClientVersion, int* length) {
    PSDP_OPTION attributeList;
    int offset;
    char* payload;
    char urlSafeAddr[URLSAFESTRING_LEN];

    addrToUrlSafeString(&RemoteAddr, urlSafeAddr);

    attributeList = getAttributesList(urlSafeAddr);
    if (attributeList == NULL) {
        return NULL;
    }

    payload = malloc(MAX_SDP_HEADER_LEN + MAX_SDP_TAIL_LEN +
        getSerializedAttributeListSize(attributeList));
    if (payload == NULL) {
        freeAttributeList(attributeList);
        return NULL;
    }

    offset = fillSdpHeader(payload, rtspClientVersion, urlSafeAddr);
    offset += fillSerializedAttributeList(&payload[offset], attributeList);
    offset += fillSdpTail(&payload[offset]);

    freeAttributeList(attributeList);
    *length = offset;
    return payload;
}
