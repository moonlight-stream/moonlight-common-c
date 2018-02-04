//
// This header exposes the public streaming API for client usage
//

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Enable this definition during debugging to enable assertions
//#define LC_DEBUG

typedef struct _STREAM_CONFIGURATION {
    // Dimensions in pixels of the desired video stream
    int width;
    int height;

    // FPS of the desired video stream
    int fps;

    // Bitrate of the desired video stream (audio adds another ~1 Mbps)
    int bitrate;

    // Max video packet size in bytes (use 1024 if unsure)
    int packetSize;

    // Set to non-zero value to enable remote (over the Internet)
    // streaming optimizations. If unsure, set to 0.
    int streamingRemotely;

    // Specifies the channel configuration of the audio stream.
    // See AUDIO_CONFIGURATION_XXX constants below.
    int audioConfiguration;
    
    // Specifies that the client can accept an H.265 video stream
    // if the server is able to provide one.
    int supportsHevc;

    // Specifies that the client is requesting an HDR H.265 video stream.
    //
    // This should only be set if:
    // 1) The client decoder supports HEVC Main10 profile (supportsHevc must be set too)
    // 2) The server has support for HDR as indicated by ServerCodecModeSupport in /serverinfo
    // 3) The app supports HDR as indicated by IsHdrSupported in /applist
    int enableHdr;

    // Specifies the percentage that the specified bitrate will be adjusted
    // when an HEVC stream will be delivered. This allows clients to opt to
    // reduce bandwidth when HEVC is chosen as the video codec rather than
    // (or in addition to) improving image quality.
    int hevcBitratePercentageMultiplier;

    // If specified, the client's display refresh rate x 100. For example,
    // 59.94 Hz would be specified as 5994. This is used by recent versions
    // of GFE for enhanced frame pacing.
    int clientRefreshRateX100;

    // AES encryption data for the remote input stream. This must be
    // the same as what was passed as rikey and rikeyid
    // in /launch and /resume requests.
    char remoteInputAesKey[16];
    char remoteInputAesIv[16];
} STREAM_CONFIGURATION, *PSTREAM_CONFIGURATION;

// Use this function to zero the stream configuration when allocated on the stack or heap
void LiInitializeStreamConfiguration(PSTREAM_CONFIGURATION streamConfig);

// These identify codec configuration data in the buffer lists
// of frames identified as IDR frames.
#define BUFFER_TYPE_PICDATA  0x00
#define BUFFER_TYPE_SPS      0x01
#define BUFFER_TYPE_PPS      0x02
#define BUFFER_TYPE_VPS      0x03

typedef struct _LENTRY {
    // Pointer to the next entry or NULL if this is the last entry
    struct _LENTRY* next;

    // Pointer to data (never NULL)
    char* data;

    // Size of data in bytes (never <= 0)
    int length;

    // Buffer type (listed above)
    int bufferType;
} LENTRY, *PLENTRY;

// This is a standard frame which references the IDR frame and
// previous P-frames.
#define FRAME_TYPE_PFRAME 0x00

// Indicates this frame contains SPS, PPS, and VPS (if applicable)
// as the first buffers in the list. Each NALU will appear as a separate
// buffer in the buffer list. The I-frame data follows immediately
// after the codec configuration NALUs.
#define FRAME_TYPE_IDR    0x01

// A decode unit describes a buffer chain of video data from multiple packets
typedef struct _DECODE_UNIT {
    // Frame number
    int frameNumber;

    // Frame type
    int frameType;

    // Receive time of first buffer
    // NOTE: This will be populated from gettimeofday() if !HAVE_CLOCK_GETTIME and
    // populated from clock_gettime(CLOCK_MONOTONIC) if HAVE_CLOCK_GETTIME
    unsigned long long receiveTimeMs;

    // Length of the entire buffer chain in bytes
    int fullLength;

    // Head of the buffer chain (never NULL)
    PLENTRY bufferList;
} DECODE_UNIT, *PDECODE_UNIT;

// Specifies that the audio stream should be encoded in stereo (default)
#define AUDIO_CONFIGURATION_STEREO 0

// Specifies that the audio stream should be in 5.1 surround sound if the PC is able
#define AUDIO_CONFIGURATION_51_SURROUND 1

// Passed to DecoderRendererSetup to indicate that the following video stream will be
// in H.264 High Profile.
#define VIDEO_FORMAT_H264 0x0001

// Passed to DecoderRendererSetup to indicate that the following video stream will be
// in H.265 Main profile. This will only be passed if supportsHevc is true.
#define VIDEO_FORMAT_H265 0x0100

// Passed to DecoderRendererSetup to indicate that the following video stream will be
// in H.265 Main10 (HDR10) profile. This will only be passed if enableHdr is true.
#define VIDEO_FORMAT_H265_MAIN10 0x0200

// Masks for clients to use to match video codecs without profile-specific details.
#define VIDEO_FORMAT_MASK_H264 0x00FF
#define VIDEO_FORMAT_MASK_H265 0xFF00

// If set in the renderer capabilities field, this flag will cause audio/video data to
// be submitted directly from the receive thread. This should only be specified if the
// renderer is non-blocking. This flag is valid on both audio and video renderers.
#define CAPABILITY_DIRECT_SUBMIT 0x1

// If set in the video renderer capabilities field, this flag specifies that the renderer
// supports reference frame invalidation for AVC/H.264 streams. This flag is only valid on video renderers.
// If using this feature, the bitstream may not be patched (changing num_ref_frames or max_dec_frame_buffering)
// to avoid video corruption on packet loss.
#define CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC 0x2

// If set in the video renderer capabilities field, this flag specifies that the renderer
// supports reference frame invalidation for HEVC/H.265 streams. This flag is only valid on video renderers.
#define CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC 0x4

// If set in the video renderer capabilities field, this macro specifies that the renderer
// supports slicing to increase decoding performance. The parameter specifies the desired
// number of slices per frame. This capability is only valid on video renderers.
#define CAPABILITY_SLICES_PER_FRAME(x) (((unsigned char)(x)) << 24)

// This callback is invoked to provide details about the video stream and allow configuration of the decoder.
// Returns 0 on success, non-zero on failure.
typedef int(*DecoderRendererSetup)(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags);

// This callback notifies the decoder that the stream is starting. No frames can be submitted before this callback returns.
typedef void(*DecoderRendererStart)(void);

// This callback notifies the decoder that the stream is stopping. Frames may still be submitted but they may be safely discarded.
typedef void(*DecoderRendererStop)(void);

// This callback performs the teardown of the video decoder. No more frames will be submitted when this callback is invoked.
typedef void(*DecoderRendererCleanup)(void);


// This callback provides Annex B formatted elementary stream data to the
// decoder. If the decoder is unable to process the submitted data for some reason,
// it must return DR_NEED_IDR to generate a keyframe.
#define DR_OK 0
#define DR_NEED_IDR -1
typedef int(*DecoderRendererSubmitDecodeUnit)(PDECODE_UNIT decodeUnit);

typedef struct _DECODER_RENDERER_CALLBACKS {
    DecoderRendererSetup setup;
    DecoderRendererStart start;
    DecoderRendererStop stop;
    DecoderRendererCleanup cleanup;
    DecoderRendererSubmitDecodeUnit submitDecodeUnit;
    int capabilities;
} DECODER_RENDERER_CALLBACKS, *PDECODER_RENDERER_CALLBACKS;

// Use this function to zero the video callbacks when allocated on the stack or heap
void LiInitializeVideoCallbacks(PDECODER_RENDERER_CALLBACKS drCallbacks);

// This structure provides the Opus multistream decoder parameters required to successfully
// decode the audio stream being sent from the computer. See opus_multistream_decoder_init docs
// for details about these fields.
//
// The supplied mapping array is indexed according to the following output channel order:
// 0 - Front Left
// 1 - Front Right
// 2 - Center
// 3 - LFE
// 4 - Surround Left
// 5 - Surround Right
//
// If the mapping order does not match the channel order of the audio renderer, you may swap
// the values in the mismatched indices until the mapping array matches the desired channel order.
typedef struct _OPUS_MULTISTREAM_CONFIGURATION {
    int sampleRate;
    int channelCount;
    int streams;
    int coupledStreams;
    const unsigned char mapping[6];
} OPUS_MULTISTREAM_CONFIGURATION, *POPUS_MULTISTREAM_CONFIGURATION;

// This callback initializes the audio renderer. The audio configuration parameter
// provides the negotiated audio configuration. This may differ from the one
// specified in the stream configuration. Returns 0 on success, non-zero on failure.
typedef int(*AudioRendererInit)(int audioConfiguration, POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* context, int arFlags);

// This callback notifies the decoder that the stream is starting. No audio can be submitted before this callback returns.
typedef void(*AudioRendererStart)(void);

// This callback notifies the decoder that the stream is stopping. Audio samples may still be submitted but they may be safely discarded.
typedef void(*AudioRendererStop)(void);

// This callback performs the final teardown of the audio decoder. No additional audio will be submitted when this callback is invoked.
typedef void(*AudioRendererCleanup)(void);

// This callback provides Opus audio data to be decoded and played. sampleLength is in bytes.
typedef void(*AudioRendererDecodeAndPlaySample)(char* sampleData, int sampleLength);

typedef struct _AUDIO_RENDERER_CALLBACKS {
    AudioRendererInit init;
    AudioRendererStart start;
    AudioRendererStop stop;
    AudioRendererCleanup cleanup;
    AudioRendererDecodeAndPlaySample decodeAndPlaySample;
    int capabilities;
} AUDIO_RENDERER_CALLBACKS, *PAUDIO_RENDERER_CALLBACKS;

// Use this function to zero the audio callbacks when allocated on the stack or heap
void LiInitializeAudioCallbacks(PAUDIO_RENDERER_CALLBACKS arCallbacks);

// Subject to change in future releases
// Use LiGetStageName() for stable stage names
#define STAGE_NONE 0
#define STAGE_PLATFORM_INIT 1
#define STAGE_NAME_RESOLUTION 2
#define STAGE_RTSP_HANDSHAKE 3
#define STAGE_CONTROL_STREAM_INIT 4
#define STAGE_VIDEO_STREAM_INIT 5
#define STAGE_AUDIO_STREAM_INIT 6
#define STAGE_INPUT_STREAM_INIT 7
#define STAGE_CONTROL_STREAM_START 8
#define STAGE_VIDEO_STREAM_START 9
#define STAGE_AUDIO_STREAM_START 10
#define STAGE_INPUT_STREAM_START 11
#define STAGE_MAX 12

// This callback is invoked to indicate that a stage of initialization is about to begin
typedef void(*ConnListenerStageStarting)(int stage);

// This callback is invoked to indicate that a stage of initialization has completed
typedef void(*ConnListenerStageComplete)(int stage);

// This callback is invoked to indicate that a stage of initialization has failed
typedef void(*ConnListenerStageFailed)(int stage, long errorCode);

// This callback is invoked after initialization has finished
typedef void(*ConnListenerConnectionStarted)(void);

// This callback is invoked when a connection failure occurs. It will not
// occur as a result of a call to LiStopConnection()
typedef void(*ConnListenerConnectionTerminated)(long errorCode);

// This callback is invoked to display a dialog-type message to the user
typedef void(*ConnListenerDisplayMessage)(const char* message);

// This callback is invoked to display a transient message for the user
// while streaming
typedef void(*ConnListenerDisplayTransientMessage)(const char* message);

// This callback is invoked to log debug message
typedef void(*ConnListenerLogMessage)(const char* format, ...);

typedef struct _CONNECTION_LISTENER_CALLBACKS {
    ConnListenerStageStarting stageStarting;
    ConnListenerStageComplete stageComplete;
    ConnListenerStageFailed stageFailed;
    ConnListenerConnectionStarted connectionStarted;
    ConnListenerConnectionTerminated connectionTerminated;
    ConnListenerDisplayMessage displayMessage;
    ConnListenerDisplayTransientMessage displayTransientMessage;
    ConnListenerLogMessage logMessage;
} CONNECTION_LISTENER_CALLBACKS, *PCONNECTION_LISTENER_CALLBACKS;

// Use this function to zero the connection callbacks when allocated on the stack or heap
void LiInitializeConnectionCallbacks(PCONNECTION_LISTENER_CALLBACKS clCallbacks);


typedef struct _SERVER_INFORMATION {
    // Server host name or IP address in text form
    const char* address;
    
    // Text inside 'appversion' tag in /serverinfo
    const char* serverInfoAppVersion;
    
    // Text inside 'GfeVersion' tag in /serverinfo (if present)
    const char* serverInfoGfeVersion;
} SERVER_INFORMATION, *PSERVER_INFORMATION;

// Use this function to zero the server information when allocated on the stack or heap
void LiInitializeServerInformation(PSERVER_INFORMATION serverInfo);

// This function begins streaming.
//
// Callbacks are all optional. Pass NULL for individual callbacks within each struct or pass NULL for the entire struct
// to use the defaults for all callbacks.
//
// This function is not thread-safe.
//
int LiStartConnection(PSERVER_INFORMATION serverInfo, PSTREAM_CONFIGURATION streamConfig, PCONNECTION_LISTENER_CALLBACKS clCallbacks,
    PDECODER_RENDERER_CALLBACKS drCallbacks, PAUDIO_RENDERER_CALLBACKS arCallbacks, void* renderContext, int drFlags,
    void* audioContext, int arFlags);

// This function stops streaming. This function is not thread-safe.
void LiStopConnection(void);

// This function interrupts a pending LiStartConnection() call. This interruption happens asynchronously
// so it is not safe to start another connection before the first LiStartConnection() call returns.
void LiInterruptConnection(void);

// Use to get a user-visible string to display initialization progress
// from the integer passed to the ConnListenerStageXXX callbacks
const char* LiGetStageName(int stage);

// This function queues a mouse move event to be sent to the remote server.
int LiSendMouseMoveEvent(short deltaX, short deltaY);

// This function queues a mouse button event to be sent to the remote server.
#define BUTTON_ACTION_PRESS 0x07
#define BUTTON_ACTION_RELEASE 0x08
#define BUTTON_LEFT 0x01
#define BUTTON_MIDDLE 0x02
#define BUTTON_RIGHT 0x03
int LiSendMouseButtonEvent(char action, int button);

// This function queues a keyboard event to be sent to the remote server.
#define KEY_ACTION_DOWN 0x03
#define KEY_ACTION_UP 0x04
#define MODIFIER_SHIFT 0x01
#define MODIFIER_CTRL 0x02
#define MODIFIER_ALT 0x04
int LiSendKeyboardEvent(short keyCode, char keyAction, char modifiers);

// Button flags
#define A_FLAG     0x1000
#define B_FLAG     0x2000
#define X_FLAG     0x4000
#define Y_FLAG     0x8000
#define UP_FLAG    0x0001
#define DOWN_FLAG  0x0002
#define LEFT_FLAG  0x0004
#define RIGHT_FLAG 0x0008
#define LB_FLAG    0x0100
#define RB_FLAG    0x0200
#define PLAY_FLAG  0x0010
#define BACK_FLAG  0x0020
#define LS_CLK_FLAG  0x0040
#define RS_CLK_FLAG  0x0080
#define SPECIAL_FLAG 0x0400

// This function queues a controller event to be sent to the remote server. It will
// be seen by the computer as the first controller.
int LiSendControllerEvent(short buttonFlags, unsigned char leftTrigger, unsigned char rightTrigger,
    short leftStickX, short leftStickY, short rightStickX, short rightStickY);

// This function queues a controller event to be sent to the remote server. The controllerNumber
// parameter is a zero-based index of which controller this event corresponds to. The largest legal
// controller number is 3 (for a total of 4 controllers, the Xinput maximum). On generation 3 servers (GFE 2.1.x),
// these will be sent as controller 0 regardless of the controllerNumber parameter. The activeGamepadMask
// parameter is a bitfield with bits set for each controller present up to a maximum of 4 (0xF).
int LiSendMultiControllerEvent(short controllerNumber, short activeGamepadMask,
    short buttonFlags, unsigned char leftTrigger, unsigned char rightTrigger,
    short leftStickX, short leftStickY, short rightStickX, short rightStickY);

// This function queues a vertical scroll event to the remote server.
int LiSendScrollEvent(signed char scrollClicks);

#ifdef __cplusplus
}
#endif
