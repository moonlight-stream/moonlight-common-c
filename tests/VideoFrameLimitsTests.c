#include "../src/VideoFrameLimits.h"
#include <assert.h>
#include <limits.h>

int main(void) {
    const int cap = MAX_VIDEO_REASSEMBLED_FRAME_SIZE;

    // Legitimate fragments, exact limit, and zero-length fragments.
    assert(canAppendVideoFrameFragment(0, 1200));
    assert(canAppendVideoFrameFragment(1200, cap - 1200));
    assert(canAppendVideoFrameFragment(cap, 0));

    // Oversized first fragment, overflow-at-boundary, bad signed lengths.
    assert(!canAppendVideoFrameFragment(0, cap + 1));
    assert(!canAppendVideoFrameFragment(cap, 1));
    assert(!canAppendVideoFrameFragment(cap - 1, 2));
    assert(!canAppendVideoFrameFragment(-1, 1));
    assert(!canAppendVideoFrameFragment(0, -1));
    assert(!canAppendVideoFrameFragment(INT_MAX, 1));
    assert(!canAppendVideoFrameFragment(1, INT_MAX));

    // Started frame may receive ordered continuation packets of same frame.
    assert(isValidVideoFramePacket(true, false, 1, 1));
    assert(isValidVideoFramePacket(false, true, 1, 1));
    // Missing SOF, repeated SOF, and a continuation from another frame.
    assert(!isValidVideoFramePacket(false, false, 1, 1));
    assert(!isValidVideoFramePacket(true, true, 2, 1));
    assert(!isValidVideoFramePacket(false, true, 2, 1));
    return 0;
}
