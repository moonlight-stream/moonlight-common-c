/**
 * @file VideoFormats.h
 * @brief Internal codec negotiation and GameStream wire-format helpers.
 */
#pragma once

#include "Limelight.h"

/**
 * @brief Choose a mutually supported codec without giving legacy codecs precedence.
 *
 * @param supportedVideoFormats Client decoder capability mask.
 * @param serverCodecModeSupport Host codec capability mask; zero permits baseline profiles on older hosts.
 * @param sdpPayload RTSP DESCRIBE payload advertising the host codecs.
 * @return One VIDEO_FORMAT value, or zero when no supported codec is available.
 */
int chooseVideoFormat(int supportedVideoFormats, int serverCodecModeSupport, const char* sdpPayload);

/**
 * @brief Map a negotiated codec to its GameStream bitStreamFormat value.
 *
 * @param videoFormat One negotiated VIDEO_FORMAT value.
 * @return Wire value from zero through four, or -1 for an invalid format.
 */
int getVideoBitStreamFormat(int videoFormat);
