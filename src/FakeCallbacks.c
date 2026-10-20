#include "Limelight-internal.h"

static int fakeDrSetup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags) { return 0; }
static void fakeDrStart(void) {}
static void fakeDrStop(void) {}
static void fakeDrCleanup(void) {}
static int fakeDrSubmitDecodeUnit(PDECODE_UNIT decodeUnit) { return DR_OK; }

static DECODER_RENDERER_CALLBACKS fakeDrCallbacks = {
    .setup = fakeDrSetup,
    .start = fakeDrStart,
    .stop = fakeDrStop,
    .cleanup = fakeDrCleanup,
    .submitDecodeUnit = fakeDrSubmitDecodeUnit,
};

static int fakeArInit(int audioConfiguration, POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* context, int arFlags) { return 0; }
static void fakeArStart(void) {}
static void fakeArStop(void) {}
static void fakeArCleanup(void) {}
static void fakeArDecodeAndPlaySample(char* sampleData, int sampleLength) {}

AUDIO_RENDERER_CALLBACKS fakeArCallbacks = {
    .init = fakeArInit,
    .start = fakeArStart,
    .stop = fakeArStop,
    .cleanup = fakeArCleanup,
    .decodeAndPlaySample = fakeArDecodeAndPlaySample,
};

static int fakeAcInit(int audioConfiguration, POPUS_ENCODER_CONFIGURATION opusConfig, void* context, int arFlags) { return 0; }
static void fakeAcStart(void) {}
static void fakeAcStop(void) {}
static void fakeAcCleanup(void) {}
static bool fakeAcCaptureMic(void* outFrame, uint32_t* len) {
    return false;
}
static void fakeAcEncode(void* inFrame, int inMaxPayloadSize, void* outEncodedFrame, int* outSize){}
static void fakeAcTestPlay(void* data, int len){}
static bool fakeAcIsMuted(void){return true;}

AUDIO_CAPTURE_CALLBACKS fakeAcCallbacks = {
    .init = fakeAcInit,
    .start = fakeAcStart,
    .stop = fakeAcStop,
    .cleanup = fakeAcCleanup,
    .captureMic = fakeAcCaptureMic,
    .encode = fakeAcEncode,
    .testPlay = fakeAcTestPlay,
    .isMuted = fakeAcIsMuted
};

static void fakeClStageStarting(int stage) {}
static void fakeClStageComplete(int stage) {}
static void fakeClStageFailed(int stage, int errorCode) {}
static void fakeClConnectionStarted(void) {}
static void fakeClConnectionTerminated(int errorCode) {}
static void fakeClLogMessage(const char* format, ...) {}
static void fakeClRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor) {}
static void fakeClConnectionStatusUpdate(int connectionStatus) {}
static void fakeClSetHdrMode(bool enabled) {}
static void fakeClRumbleTriggers(uint16_t controllerNumber, uint16_t leftTriggerMotor, uint16_t rightTriggerMotor) {}
static void fakeClSetMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz) {}
static void fakeClSetControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b) {}
static void fakeClSetVirtualKeyboard(uint8_t visible, uint8_t inputHint, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {}
static void fakeClOpenUrl(const char* url) {}
static void fakeClTerminateStream(const char* reason) {}

static CONNECTION_LISTENER_CALLBACKS fakeClCallbacks = {
    .stageStarting = fakeClStageStarting,
    .stageComplete = fakeClStageComplete,
    .stageFailed = fakeClStageFailed,
    .connectionStarted = fakeClConnectionStarted,
    .connectionTerminated = fakeClConnectionTerminated,
    .logMessage = fakeClLogMessage,
    .rumble = fakeClRumble,
    .connectionStatusUpdate = fakeClConnectionStatusUpdate,
    .setHdrMode = fakeClSetHdrMode,
    .rumbleTriggers = fakeClRumbleTriggers,
    .setMotionEventState = fakeClSetMotionEventState,
    .setControllerLED = fakeClSetControllerLED,
    .setVirtualKeyboard = fakeClSetVirtualKeyboard,
    .openUrl = fakeClOpenUrl,
    .terminateStream = fakeClTerminateStream,
};

void fixupMissingCallbacks(PDECODER_RENDERER_CALLBACKS* drCallbacks, PAUDIO_RENDERER_CALLBACKS* arCallbacks,
    PAUDIO_CAPTURE_CALLBACKS* acCallbacks, PCONNECTION_LISTENER_CALLBACKS* clCallbacks)
{
    if (*drCallbacks == NULL) {
        *drCallbacks = &fakeDrCallbacks;
    }
    else {
        if ((*drCallbacks)->setup == NULL) {
            (*drCallbacks)->setup = fakeDrSetup;
        }
        if ((*drCallbacks)->start == NULL) {
            (*drCallbacks)->start = fakeDrStart;
        }
        if ((*drCallbacks)->stop == NULL) {
            (*drCallbacks)->stop = fakeDrStop;
        }
        if ((*drCallbacks)->cleanup == NULL) {
            (*drCallbacks)->cleanup = fakeDrCleanup;
        }
        if ((*drCallbacks)->submitDecodeUnit == NULL) {
            (*drCallbacks)->submitDecodeUnit = fakeDrSubmitDecodeUnit;
        }
    }

    if (*arCallbacks == NULL) {
        *arCallbacks = &fakeArCallbacks;
    }
    else {
        if ((*arCallbacks)->init == NULL) {
            (*arCallbacks)->init = fakeArInit;
        }
        if ((*arCallbacks)->start == NULL) {
            (*arCallbacks)->start = fakeArStart;
        }
        if ((*arCallbacks)->stop == NULL) {
            (*arCallbacks)->stop = fakeArStop;
        }
        if ((*arCallbacks)->cleanup == NULL) {
            (*arCallbacks)->cleanup = fakeArCleanup;
        }
        if ((*arCallbacks)->decodeAndPlaySample == NULL) {
            (*arCallbacks)->decodeAndPlaySample = fakeArDecodeAndPlaySample;
        }
    }

    if (*acCallbacks == NULL) {
        *acCallbacks = &fakeAcCallbacks;
    }
    else {
        if ((*acCallbacks)->init == NULL) {
            (*acCallbacks)->init = fakeAcInit;
        }
        if ((*acCallbacks)->start == NULL) {
            (*acCallbacks)->start = fakeAcStart;
        }
        if ((*acCallbacks)->stop == NULL) {
            (*acCallbacks)->stop = fakeAcStop;
        }
        if ((*acCallbacks)->cleanup == NULL) {
            (*acCallbacks)->cleanup = fakeAcCleanup;
        }
        if((*acCallbacks)->captureMic == NULL){
            (*acCallbacks)->captureMic = fakeAcCaptureMic;
        }
        if ((*acCallbacks)->encode == NULL) {
            (*acCallbacks)->encode = fakeAcEncode;
        }
        if((*acCallbacks)->testPlay == NULL){
            (*acCallbacks)->testPlay = fakeAcTestPlay;
        }
        if((*acCallbacks)->isMuted == NULL){
            (*acCallbacks)->isMuted = fakeAcIsMuted;
        }
    }

    if (*clCallbacks == NULL) {
        *clCallbacks = &fakeClCallbacks;
    }
    else {
        if ((*clCallbacks)->stageStarting == NULL) {
            (*clCallbacks)->stageStarting = fakeClStageStarting;
        }
        if ((*clCallbacks)->stageComplete == NULL) {
            (*clCallbacks)->stageComplete = fakeClStageComplete;
        }
        if ((*clCallbacks)->stageFailed == NULL) {
            (*clCallbacks)->stageFailed = fakeClStageFailed;
        }
        if ((*clCallbacks)->connectionStarted == NULL) {
            (*clCallbacks)->connectionStarted = fakeClConnectionStarted;
        }
        if ((*clCallbacks)->connectionTerminated == NULL) {
            (*clCallbacks)->connectionTerminated = fakeClConnectionTerminated;
        }
        if ((*clCallbacks)->logMessage == NULL) {
            (*clCallbacks)->logMessage = fakeClLogMessage;
        }
        if ((*clCallbacks)->rumble == NULL) {
            (*clCallbacks)->rumble = fakeClRumble;
        }
        if ((*clCallbacks)->connectionStatusUpdate == NULL) {
            (*clCallbacks)->connectionStatusUpdate = fakeClConnectionStatusUpdate;
        }
        if ((*clCallbacks)->setHdrMode == NULL) {
            (*clCallbacks)->setHdrMode = fakeClSetHdrMode;
        }
        if ((*clCallbacks)->rumbleTriggers == NULL) {
            (*clCallbacks)->rumbleTriggers = fakeClRumbleTriggers;
        }
        if ((*clCallbacks)->setMotionEventState == NULL) {
            (*clCallbacks)->setMotionEventState = fakeClSetMotionEventState;
        }
        // A client that has not implemented this - every client, until it does - gets a
        // no-op rather than a null call. This is what lets the host ship the message
        // before any client handles it.
        if ((*clCallbacks)->setVirtualKeyboard == NULL) {
            (*clCallbacks)->setVirtualKeyboard = fakeClSetVirtualKeyboard;
        }
        if ((*clCallbacks)->openUrl == NULL) {
            (*clCallbacks)->openUrl = fakeClOpenUrl;
        }
        if ((*clCallbacks)->terminateStream == NULL) {
            (*clCallbacks)->terminateStream = fakeClTerminateStream;
        }
        if ((*clCallbacks)->setControllerLED == NULL) {
            (*clCallbacks)->setControllerLED = fakeClSetControllerLED;
        }
    }
}
