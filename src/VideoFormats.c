/**
 * @file VideoFormats.c
 * @brief Negotiate modern and legacy video codecs using shared protocol capabilities.
 */
#include "VideoFormats.h"

#include <string.h>

int chooseVideoFormat(int supportedVideoFormats, int serverCodecModeSupport, const char* sdpPayload) {
    if (sdpPayload == NULL) {
        return 0;
    }

    if (strstr(sdpPayload, "AV1/90000") != NULL) {
        if ((supportedVideoFormats & VIDEO_FORMAT_AV1_HIGH10_444) && (serverCodecModeSupport & SCM_AV1_HIGH10_444)) {
            return VIDEO_FORMAT_AV1_HIGH10_444;
        }
        if ((supportedVideoFormats & VIDEO_FORMAT_AV1_MAIN10) && (serverCodecModeSupport & SCM_AV1_MAIN10)) {
            return VIDEO_FORMAT_AV1_MAIN10;
        }
        if ((supportedVideoFormats & VIDEO_FORMAT_AV1_HIGH8_444) && (serverCodecModeSupport & SCM_AV1_HIGH8_444)) {
            return VIDEO_FORMAT_AV1_HIGH8_444;
        }
        if ((supportedVideoFormats & VIDEO_FORMAT_AV1_MAIN8) &&
                (serverCodecModeSupport == 0 || (serverCodecModeSupport & SCM_AV1_MAIN8))) {
            return VIDEO_FORMAT_AV1_MAIN8;
        }
    }

    // GameStream identifies HEVC support using the VPS prefix in this attribute.
    if (strstr(sdpPayload, "sprop-parameter-sets=AAAAAU") != NULL) {
        if ((supportedVideoFormats & VIDEO_FORMAT_H265_REXT10_444) && (serverCodecModeSupport & SCM_HEVC_REXT10_444)) {
            return VIDEO_FORMAT_H265_REXT10_444;
        }
        if ((supportedVideoFormats & VIDEO_FORMAT_H265_MAIN10) && (serverCodecModeSupport & SCM_HEVC_MAIN10)) {
            return VIDEO_FORMAT_H265_MAIN10;
        }
        if ((supportedVideoFormats & VIDEO_FORMAT_H265_REXT8_444) && (serverCodecModeSupport & SCM_HEVC_REXT8_444)) {
            return VIDEO_FORMAT_H265_REXT8_444;
        }
        if ((supportedVideoFormats & VIDEO_FORMAT_H265) &&
                (serverCodecModeSupport == 0 || (serverCodecModeSupport & SCM_HEVC))) {
            return VIDEO_FORMAT_H265;
        }
    }

    if ((supportedVideoFormats & VIDEO_FORMAT_H264_HIGH8_444) && (serverCodecModeSupport & SCM_H264_HIGH8_444)) {
        return VIDEO_FORMAT_H264_HIGH8_444;
    }
    if ((supportedVideoFormats & VIDEO_FORMAT_H264) &&
            (serverCodecModeSupport == 0 || (serverCodecModeSupport & SCM_H264))) {
        return VIDEO_FORMAT_H264;
    }

    // Legacy codecs are opt-in Sunshine protocol extensions, available to any client.
    if ((supportedVideoFormats & VIDEO_FORMAT_MPEG2) && (serverCodecModeSupport & SCM_MPEG2) &&
            (strstr(sdpPayload, "MPV/90000") != NULL || strstr(sdpPayload, "MPEG2/90000") != NULL ||
             strstr(sdpPayload, "MPEG-2/90000") != NULL)) {
        return VIDEO_FORMAT_MPEG2;
    }
    if ((supportedVideoFormats & VIDEO_FORMAT_H263P) && (serverCodecModeSupport & SCM_H263P) &&
            (strstr(sdpPayload, "H263-1998/90000") != NULL || strstr(sdpPayload, "H263-2000/90000") != NULL)) {
        return VIDEO_FORMAT_H263P;
    }

    return 0;
}

int getVideoBitStreamFormat(int videoFormat) {
    switch (videoFormat) {
        case VIDEO_FORMAT_H264:
        case VIDEO_FORMAT_H264_HIGH8_444:
            return 0;
        case VIDEO_FORMAT_H265:
        case VIDEO_FORMAT_H265_MAIN10:
        case VIDEO_FORMAT_H265_REXT8_444:
        case VIDEO_FORMAT_H265_REXT10_444:
            return 1;
        case VIDEO_FORMAT_AV1_MAIN8:
        case VIDEO_FORMAT_AV1_MAIN10:
        case VIDEO_FORMAT_AV1_HIGH8_444:
        case VIDEO_FORMAT_AV1_HIGH10_444:
            return 2;
        case VIDEO_FORMAT_MPEG2:
            return 3;
        case VIDEO_FORMAT_H263P:
            return 4;
        default:
            return -1;
    }
}
