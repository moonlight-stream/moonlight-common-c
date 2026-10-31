#pragma once

#include <stdbool.h>
#include <stdint.h>

// A conservative cap on one decoded compressed frame, independent of the
// negotiated bitrate. Keep below INT_MAX because DECODE_UNIT.fullLength is int.
// This is a bound on a single NAL chain, not a cap on the queued decode units.
#define MAX_VIDEO_REASSEMBLED_FRAME_SIZE (32 * 1024 * 1024)

static inline bool canAppendVideoFrameFragment(int currentLength, int fragmentLength) {
    return currentLength >= 0 &&
           currentLength <= MAX_VIDEO_REASSEMBLED_FRAME_SIZE &&
           fragmentLength >= 0 &&
           fragmentLength <= MAX_VIDEO_REASSEMBLED_FRAME_SIZE - currentLength;
}

// Continuation packets must belong to a frame whose SOF was already seen.
// A new SOF while a prior frame is unfinished is likewise invalid.
static inline bool isValidVideoFramePacket(bool firstPacket, bool decodingFrame,
                                           uint32_t frameIndex, uint32_t nextFrameNumber) {
    return firstPacket ? !decodingFrame
                       : decodingFrame && frameIndex == nextFrameNumber;
}
