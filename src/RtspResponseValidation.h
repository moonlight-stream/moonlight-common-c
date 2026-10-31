#pragma once

#include <stdbool.h>
#include <stdint.h>

// Zero is not used by Sunshine's encrypted RTSP response counter.
// Since the counter must never repeat an AES-GCM IV with the same key, a
// 32-bit wrap is treated as a session failure rather than as a new cycle.
static inline bool isFreshEncryptedRtspSequence(uint32_t lastAccepted,
                                                uint32_t received) {
    return received != 0 && received > lastAccepted;
}

// Some hosts omit CSeq on specific responses. If it is present in an
// encrypted response, it must match the request we just issued.
// Call only after the response has passed authenticated decryption.
static inline bool isMatchingRtspResponseCseq(int expected, int received) {
    return expected >= 0 && (received == -1 || received == expected);
}
