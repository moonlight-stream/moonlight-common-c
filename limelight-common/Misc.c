#include "Limelight-internal.h"

int isBeforeSignedInt(int numA, int numB, int ambiguousCase) {
	// This should be the common case for most callers
	if (numA == numB) {
		return 0;
	}

	// If numA and numB have the same signs,
	// we can just do a regular comparison.
	if ((numA < 0 && numB < 0) || (numA >= 0 && numB >= 0)) {
		return numA < numB;
	}
	else {
		// The sign switch is ambiguous
		return ambiguousCase;
	}
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
