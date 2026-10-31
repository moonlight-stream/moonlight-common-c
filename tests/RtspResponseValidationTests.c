// Always run assertions, including in a Release (-DNDEBUG) build.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include "../src/RtspResponseValidation.h"

int main(void) {
    assert(isFreshEncryptedRtspSequence(0, 1));
    assert(isFreshEncryptedRtspSequence(1, 2));
    assert(isFreshEncryptedRtspSequence(2, 1000));
    assert(!isFreshEncryptedRtspSequence(0, 0));
    assert(!isFreshEncryptedRtspSequence(1, 1));
    assert(!isFreshEncryptedRtspSequence(3, 2));
    assert(!isFreshEncryptedRtspSequence(UINT32_MAX, 1));
    assert(isFreshEncryptedRtspSequence(UINT32_MAX - 1, UINT32_MAX));

    assert(isMatchingRtspResponseCseq(6, 6));
    assert(isMatchingRtspResponseCseq(6, -1)); // absent header
    assert(!isMatchingRtspResponseCseq(6, 5));
    assert(!isMatchingRtspResponseCseq(6, 7));
    assert(!isMatchingRtspResponseCseq(-1, 6));
    return 0;
}
