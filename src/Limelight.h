//
// This header exposes the public streaming API for client usage
//

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Enable this definition during debugging to enable assertions
//#define LC_DEBUG

// Values for the 'streamingRemotely' field below
#define STREAM_CFG_LOCAL   0
#define STREAM_CFG_REMOTE  1
#define STREAM_CFG_AUTO    2

// Values for the 'colorSpace' field below.
// Rec. 2020 is not supported with H.264 video streams on GFE hosts.
#define COLORSPACE_REC_601  0
#define COLORSPACE_REC_709  1
#define COLORSPACE_REC_2020 2

// Values for the 'colorRange' field below
#define COLOR_RANGE_LIMITED  0
#define COLOR_RANGE_FULL     1

// Values for 'encryptionFlags' field below
#define ENCFLG_NONE  0x00000000
#define ENCFLG_AUDIO 0x00000001
#define ENCFLG_VIDEO 0x00000002
#define ENCFLG_ALL   0xFFFFFFFF

// This function returns a string that you SHOULD append to the /launch and /resume
// query parameter string. This is used to enable certain extended functionality
// with Sunshine hosts. The returned string is owned by moonlight-common-c and
// should not be freed by the caller.
const char* LiGetLaunchUrlQueryParameters(void);

typedef struct _STREAM_CONFIGURATION {
    // Dimensions in pixels of the desired video stream
    int width;
    int height;

    // FPS of the desired video stream
    int fps;

    // Bitrate of the desired video stream (audio adds another ~1 Mbps). This
    // includes error correction data, so the actual encoder bitrate will be
    // about 20% lower when using the standard 20% FEC configuration.
    int bitrate;

    // Max video packet size in bytes (use 1024 if unsure). If STREAM_CFG_AUTO
    // determines the stream is remote (see below), it will cap this value at
    // 1024 to avoid MTU-related issues like packet loss and fragmentation.
    int packetSize;

    // Determines whether to enable remote (over the Internet)
    // streaming optimizations. If unsure, set to STREAM_CFG_AUTO.
    // STREAM_CFG_AUTO uses a heuristic (whether the target address is
    // in the RFC 1918 address blocks) to decide whether the stream
    // is remote or not.
    int streamingRemotely;

    // Specifies the channel configuration of the audio stream.
    // See AUDIO_CONFIGURATION constants and MAKE_AUDIO_CONFIGURATION() below.
    int audioConfiguration;

    // Specifies the mask of supported video formats.
    // See VIDEO_FORMAT constants below.
    int supportedVideoFormats;

    // If specified, the client's display refresh rate x 100. For example,
    // 59.94 Hz would be specified as 5994. This is used by recent versions
    // of GFE for enhanced frame pacing.
    int clientRefreshRateX100;

    // If specified, sets the encoder colorspace to the provided COLORSPACE_*
    // option (listed above). If not set, the encoder will default to Rec 601.
    int colorSpace;

    // If specified, sets the encoder color range to the provided COLOR_RANGE_*
    // option (listed above). If not set, the encoder will default to Limited.
    int colorRange;

    // Specifies the data streams where encryption may be enabled if supported
    // by the host PC. Ideally, you would pass ENCFLG_ALL to encrypt everything
    // that we support encrypting. However, lower performance hardware may not
    // be able to support encrypting heavy stuff like video or audio data, so
    // that encryption may be disabled here. Remote input encryption is always
    // enabled.
    int encryptionFlags;

    // AES encryption data for the remote input stream. This must be
    // the same as what was passed as rikey and rikeyid
    // in /launch and /resume requests.
    char remoteInputAesKey[16];
    char remoteInputAesIv[16];
} STREAM_CONFIGURATION, *PSTREAM_CONFIGURATION;

// Use this function to zero the stream configuration when allocated on the stack or heap
void LiInitializeStreamConfiguration(PSTREAM_CONFIGURATION streamConfig);

// These identify codec configuration data in the buffer lists
// of frames identified as IDR frames for H.264 and HEVC formats.
// For other codecs, all data is marked as BUFFER_TYPE_PICDATA.
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

    // Buffer type (listed above, only set for H.264 and HEVC formats)
    int bufferType;
} LENTRY, *PLENTRY;

// This is a standard frame which references the IDR frame and
// previous P-frames.
#define FRAME_TYPE_PFRAME 0x00

// This is a key frame.
//
// For H.264 and HEVC, this means the frame contains SPS, PPS, and VPS (HEVC only) NALUs
// as the first buffers in the list. The I-frame data follows immediately
// after the codec configuration NALUs.
//
// For other codecs, any configuration data is not split into separate buffers.
#define FRAME_TYPE_IDR    0x01

// A decode unit describes a buffer chain of video data from multiple packets
typedef struct _DECODE_UNIT {
    // Frame number
    int frameNumber;

    // Frame type
    int frameType;

    // Optional host processing latency of the frame, in 1/10 ms units.
    // Zero when the host doesn't provide the latency data
    // or frame processing latency is not applicable to the current frame
    // (happens when the frame is repeated).
    uint16_t frameHostProcessingLatency;

    // Receive time of first buffer. This value uses an implementation-defined epoch,
    // but the same epoch as enqueueTimeMs and LiGetMillis().
    uint64_t receiveTimeMs;

    // Time the frame was fully assembled and queued for the video decoder to process.
    // This is also approximately the same time as the final packet was received, so
    // enqueueTimeMs - receiveTimeMs is the time taken to receive the frame. At the
    // time the decode unit is passed to submitDecodeUnit(), the total queue delay
    // can be calculated by LiGetMillis() - enqueueTimeMs.
    uint64_t enqueueTimeMs;

    // Presentation time in milliseconds with the epoch at the first captured frame.
    // This can be used to aid frame pacing or to drop old frames that were queued too
    // long prior to display.
    unsigned int presentationTimeMs;

    // Length of the entire buffer chain in bytes
    int fullLength;

    // Head of the buffer chain (never NULL)
    PLENTRY bufferList;

    // Determines if this frame is SDR or HDR
    //
    // Note: This is not currently parsed from the actual bitstream, so if your
    // client has access to a bitstream parser, prefer that over this field.
    bool hdrActive;

    // Provides the colorspace of this frame (see COLORSPACE_* defines above)
    //
    // Note: This is not currently parsed from the actual bitstream, so if your
    // client has access to a bitstream parser, prefer that over this field.
    uint8_t colorspace;
} DECODE_UNIT, *PDECODE_UNIT;

// Specifies that the audio stream should be encoded in stereo (default)
#define AUDIO_CONFIGURATION_STEREO MAKE_AUDIO_CONFIGURATION(2, 0x3)

// Specifies that the audio stream should be in 5.1 surround sound if the PC is able
#define AUDIO_CONFIGURATION_51_SURROUND MAKE_AUDIO_CONFIGURATION(6, 0x3F)

// Specifies that the audio stream should be in 7.1 surround sound if the PC is able
#define AUDIO_CONFIGURATION_71_SURROUND MAKE_AUDIO_CONFIGURATION(8, 0x63F)

// Specifies an audio configuration by channel count and channel mask
// See https://docs.microsoft.com/en-us/windows-hardware/drivers/audio/channel-mask for channelMask values
// NOTE: Not all combinations are supported by GFE and/or this library.
#define MAKE_AUDIO_CONFIGURATION(channelCount, channelMask) \
    (((channelMask) << 16) | (channelCount << 8) | 0xCA)

// Helper macros for retreiving channel count and channel mask from the audio configuration
#define CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(x) (((x) >> 8) & 0xFF)
#define CHANNEL_MASK_FROM_AUDIO_CONFIGURATION(x) (((x) >> 16) & 0xFFFF)

// Helper macro to retreive the surroundAudioInfo parameter value that must be passed in
// the /launch and /resume HTTPS requests when starting the session.
#define SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(x) \
    (CHANNEL_MASK_FROM_AUDIO_CONFIGURATION(x) << 16 | CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(x))

// The maximum number of channels supported
#define AUDIO_CONFIGURATION_MAX_CHANNEL_COUNT 8

// Passed in StreamConfiguration.supportedVideoFormats to specify supported codecs
// and to DecoderRendererSetup() to specify selected codec.
#define VIDEO_FORMAT_H264            0x0001 // H.264 High Profile
#define VIDEO_FORMAT_H264_HIGH8_444  0x0004 // H.264 High 4:4:4 8-bit Profile
#define VIDEO_FORMAT_H265            0x0100 // HEVC Main Profile
#define VIDEO_FORMAT_H265_MAIN10     0x0200 // HEVC Main10 Profile
#define VIDEO_FORMAT_H265_REXT8_444  0x0400 // HEVC RExt 4:4:4 8-bit Profile
#define VIDEO_FORMAT_H265_REXT10_444 0x0800 // HEVC RExt 4:4:4 10-bit Profile
#define VIDEO_FORMAT_AV1_MAIN8       0x1000 // AV1 Main 8-bit profile
#define VIDEO_FORMAT_AV1_MAIN10      0x2000 // AV1 Main 10-bit profile
#define VIDEO_FORMAT_AV1_HIGH8_444   0x4000 // AV1 High 4:4:4 8-bit profile
#define VIDEO_FORMAT_AV1_HIGH10_444  0x8000 // AV1 High 4:4:4 10-bit profile

// Masks for clients to use to match video codecs without profile-specific details.
#define VIDEO_FORMAT_MASK_H264   0x000F
#define VIDEO_FORMAT_MASK_H265   0x0F00
#define VIDEO_FORMAT_MASK_AV1    0xF000
#define VIDEO_FORMAT_MASK_10BIT  0xAA00
#define VIDEO_FORMAT_MASK_YUV444 0xCC04

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

// If set in the audio renderer capabilities field, this flag will cause the RTSP negotiation
// to never request the "high quality" audio preset. If unset, high quality audio will be
// used with video streams above 15 Mbps.
#define CAPABILITY_SLOW_OPUS_DECODER 0x8

// If set in the audio renderer capabilities field, this indicates that audio packets
// may contain more or less than 5 ms of audio. This requires that audio renderers read the
// samplesPerFrame field in OPUS_MULTISTREAM_CONFIGURATION to calculate the correct decoded
// buffer size rather than just assuming it will always be 240.
#define CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION 0x10

// This flag opts the renderer into a pull-based model rather than the default push-based
// callback model. The renderer must invoke the new functions (LiWaitForNextVideoFrame(),
// LiCompleteVideoFrame(), and similar) to receive A/V data. Setting this capability while
// also providing a sample callback is not allowed.
#define CAPABILITY_PULL_RENDERER 0x20

// If set in the video renderer capabilities field, this flag specifies that the renderer
// supports reference frame invalidation for AV1 streams. This flag is only valid on video renderers.
#define CAPABILITY_REFERENCE_FRAME_INVALIDATION_AV1 0x40

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
// 4 - Back Left
// 5 - Back Right
// 6 - Side Left
// 7 - Side Right
//
// If the mapping order does not match the channel order of the audio renderer, you may swap
// the values in the mismatched indices until the mapping array matches the desired channel order.
typedef struct _OPUS_MULTISTREAM_CONFIGURATION {
    int sampleRate;
    int channelCount;
    int streams;
    int coupledStreams;
    int samplesPerFrame;
    unsigned char mapping[AUDIO_CONFIGURATION_MAX_CHANNEL_COUNT];
} OPUS_MULTISTREAM_CONFIGURATION, *POPUS_MULTISTREAM_CONFIGURATION;

// This callback initializes the audio renderer. The audio configuration parameter
// provides the negotiated audio configuration. This may differ from the one
// specified in the stream configuration. Returns 0 on success, non-zero on failure.
typedef int(*AudioRendererInit)(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* context, int arFlags);

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
#define STAGE_AUDIO_STREAM_INIT 3
#define STAGE_RTSP_HANDSHAKE 4
#define STAGE_CONTROL_STREAM_INIT 5
#define STAGE_VIDEO_STREAM_INIT 6
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

// This callback is invoked to indicate that a stage of initialization has failed.
// ConnListenerConnectionTerminated() will not be invoked because the connection was
// not yet fully established. LiInterruptConnection() and LiStopConnection() may
// result in this callback being invoked, but it is not guaranteed.
typedef void(*ConnListenerStageFailed)(int stage, int errorCode);

// This callback is invoked after the connection is successfully established
typedef void(*ConnListenerConnectionStarted)(void);

// This callback is invoked when a connection is terminated after establishment.
// The errorCode will be 0 if the termination was reported to be intentional
// from the server (for example, the user closed the game). If errorCode is
// non-zero, it means the termination was probably unexpected (loss of network,
// crash, or similar conditions). This will not be invoked as a result of a call
// to LiStopConnection() or LiInterruptConnection().
typedef void(*ConnListenerConnectionTerminated)(int errorCode);

// This error code is passed to ConnListenerConnectionTerminated() when the stream
// is being gracefully terminated by the host. It usually means the app on the host
// PC has exited.
#define ML_ERROR_GRACEFUL_TERMINATION 0

// This error is passed to ConnListenerConnectionTerminated() if no video data
// was ever received for this connection after waiting several seconds. It likely
// indicates a problem with traffic on UDP 47998 due to missing or incorrect
// firewall or port forwarding rules.
#define ML_ERROR_NO_VIDEO_TRAFFIC -100

// This error is passed to ConnListenerConnectionTerminated() if a fully formed
// frame could not be received after waiting several seconds. It likely indicates
// an extremely unstable connection or a bitrate that is far too high.
#define ML_ERROR_NO_VIDEO_FRAME -101

// This error is passed to ConnListenerConnectionTerminated() if the stream ends
// very soon after starting due to a graceful termination from the host. Usually
// this seems to happen if DRM protected content is on-screen (pre-GFE 3.22), or
// another issue that prevents the encoder from being able to capture video successfully.
#define ML_ERROR_UNEXPECTED_EARLY_TERMINATION -102

// This error is passed to ConnListenerConnectionTerminated() if the stream ends
// due to a protected content error from the host. This value is supported on GFE 3.22+.
#define ML_ERROR_PROTECTED_CONTENT -103

// This error is passed to ConnListenerConnectionTerminated() if the stream ends
// due a frame conversion error. This is most commonly due to an incompatible
// desktop resolution and streaming resolution with HDR enabled. This value is
// supported on GFE 3.22+.
#define ML_ERROR_FRAME_CONVERSION -104

// This callback is invoked to log debug message
typedef void(*ConnListenerLogMessage)(const char* format, ...);

// This callback is invoked to rumble a gamepad. The rumble effect values
// set in this callback are expected to persist until a future call sets a
// different haptic effect or turns off the motors by passing 0 for both
// motors. It is possible to receive rumble events for gamepads that aren't
// physically present, so your callback should handle this possibility.
typedef void(*ConnListenerRumble)(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

// This callback is used to notify the client of a connection status change.
// Consider displaying an overlay for the user to notify them why their stream
// is not performing as expected.
#define CONN_STATUS_OKAY    0
#define CONN_STATUS_POOR    1
typedef void(*ConnListenerConnectionStatusUpdate)(int connectionStatus);

// This callback is invoked to notify the client of a change in HDR mode on
// the host. The client will probably want to update the local display mode
// to match the state of HDR on the host. This callback may be invoked even
// if the stream is not using an HDR-capable codec.
typedef void(*ConnListenerSetHdrMode)(bool hdrEnabled);

// This callback is invoked to rumble a gamepad's triggers. For more details,
// see the comment above on ConnListenerRumble().
typedef void(*ConnListenerRumbleTriggers)(uint16_t controllerNumber, uint16_t leftTriggerMotor, uint16_t rightTriggerMotor);

// This callback is invoked to notify the client that the host would like motion
// sensor reports for the specified gamepad (see LiSendControllerMotionEvent())
// at the specified reporting rate (or as close as possible).
//
// If reportRateHz is 0, the host is asking for motion event reporting to stop.
typedef void(*ConnListenerSetMotionEventState)(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz);

// This callback is invoked to notify the client of a change in the dualsense
// adaptive trigger configuration.
#define DS_EFFECT_PAYLOAD_SIZE 10
#define DS_EFFECT_RIGHT_TRIGGER 0x04
#define DS_EFFECT_LEFT_TRIGGER 0x08
typedef void(*ConnListenerSetAdaptiveTriggers)(uint16_t controllerNumber, uint8_t eventFlags, uint8_t typeLeft, uint8_t typeRight, uint8_t *left, uint8_t *right);

// This callback is invoked to set a controller's RGB LED (if present).
typedef void(*ConnListenerSetControllerLED)(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b);

typedef struct _CONNECTION_LISTENER_CALLBACKS {
    ConnListenerStageStarting stageStarting;
    ConnListenerStageComplete stageComplete;
    ConnListenerStageFailed stageFailed;
    ConnListenerConnectionStarted connectionStarted;
    ConnListenerConnectionTerminated connectionTerminated;
    ConnListenerLogMessage logMessage;
    ConnListenerRumble rumble;
    ConnListenerConnectionStatusUpdate connectionStatusUpdate;
    ConnListenerSetHdrMode setHdrMode;
    ConnListenerRumbleTriggers rumbleTriggers;
    ConnListenerSetMotionEventState setMotionEventState;
    ConnListenerSetControllerLED setControllerLED;
    ConnListenerSetAdaptiveTriggers setAdaptiveTriggers;
} CONNECTION_LISTENER_CALLBACKS, *PCONNECTION_LISTENER_CALLBACKS;

// Use this function to zero the connection callbacks when allocated on the stack or heap
void LiInitializeConnectionCallbacks(PCONNECTION_LISTENER_CALLBACKS clCallbacks);

// ServerCodecModeSupport values
#define SCM_H264            0x00000001
#define SCM_HEVC            0x00000100
#define SCM_HEVC_MAIN10     0x00000200
#define SCM_AV1_MAIN8       0x00010000 // Sunshine extension
#define SCM_AV1_MAIN10      0x00020000 // Sunshine extension
#define SCM_H264_HIGH8_444  0x00040000 // Sunshine extension
#define SCM_HEVC_REXT8_444  0x00080000 // Sunshine extension
#define SCM_HEVC_REXT10_444 0x00100000 // Sunshine extension
#define SCM_AV1_HIGH8_444   0x00200000 // Sunshine extension
#define SCM_AV1_HIGH10_444  0x00400000 // Sunshine extension

// SCM masks to identify various codec capabilities
#define SCM_MASK_H264   (SCM_H264 | SCM_H264_HIGH8_444)
#define SCM_MASK_HEVC   (SCM_HEVC | SCM_HEVC_MAIN10 | SCM_HEVC_REXT8_444 | SCM_HEVC_REXT10_444)
#define SCM_MASK_AV1    (SCM_AV1_MAIN8 | SCM_AV1_MAIN10 | SCM_AV1_HIGH8_444 | SCM_AV1_HIGH10_444)
#define SCM_MASK_10BIT  (SCM_HEVC_MAIN10 | SCM_HEVC_REXT10_444 | SCM_AV1_MAIN10 | SCM_AV1_HIGH10_444)
#define SCM_MASK_YUV444 (SCM_H264_HIGH8_444 | SCM_HEVC_REXT8_444 | SCM_HEVC_REXT10_444 | SCM_AV1_HIGH8_444 | SCM_AV1_HIGH10_444)

typedef struct _SERVER_INFORMATION {
    // Server host name or IP address in text form
    const char* address;

    // Text inside 'appversion' tag in /serverinfo
    const char* serverInfoAppVersion;

    // Text inside 'GfeVersion' tag in /serverinfo (if present)
    const char* serverInfoGfeVersion;

    // Text inside 'sessionUrl0' tag in /resume and /launch (if present)
    const char* rtspSessionUrl;

    // Specifies the 'ServerCodecModeSupport' from the /serverinfo response.
    int serverCodecModeSupport;
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

// This function returns an estimate of the current RTT to the host PC obtained via ENet
// protocol statistics. This function will fail if the current GFE version does not use
// ENet for the control stream (very old versions), or if the ENet peer is not connected.
// This function may only be called between LiStartConnection() and LiStopConnection().
bool LiGetEstimatedRttInfo(uint32_t* estimatedRtt, uint32_t* estimatedRttVariance);

// This function queues a relative mouse move event to be sent to the remote server.
int LiSendMouseMoveEvent(short deltaX, short deltaY);

// This function queues a mouse position update event to be sent to the remote server.
// This functionality is only reliably supported on GFE 3.20 or later. Earlier versions
// may not position the mouse correctly.
//
// Absolute mouse motion doesn't work in many games, so this mode should not be the default
// for mice when streaming. It may be desirable as the default touchscreen behavior when
// LiSendTouchEvent() is not supported and the touchscreen is not the primary input method.
// In the latter case, a touchscreen-as-trackpad mode using LiSendMouseMoveEvent() is likely
// to be better for gaming use cases.
//
// The x and y values are transformed to host coordinates as if they are from a plane which
// is referenceWidth by referenceHeight in size. This allows you to provide coordinates that
// are relative to an arbitrary plane, such as a window, screen, or scaled video view.
//
// For example, if you wanted to directly pass window coordinates as x and y, you would set
// referenceWidth and referenceHeight to your window width and height.
int LiSendMousePositionEvent(short x, short y, short referenceWidth, short referenceHeight);

// This function queues a mouse position update event to be sent to the remote server, so
// all of the limitations of LiSendMousePositionEvent() mentioned above apply here too!
//
// This function behaves like a combination of LiSendMouseMoveEvent() and LiSendMousePositionEvent()
// in that it sends a relative motion event, however it sends this data as an absolute position
// based on the computed position of a virtual client cursor which is "moved" any time that
// LiSendMousePositionEvent() or LiSendMouseMoveAsMousePositionEvent() is called. As a result
// of this internal virtual cursor state, callers must ensure LiSendMousePositionEvent() and
// LiSendMouseMoveAsMousePositionEvent() are not called concurrently!
//
// The big advantage of this function is that it allows callers to avoid mouse acceleration that
// would otherwise affect motion when using LiSendMouseMoveEvent(). The downside is that it has the
// same game compatibility issues as LiSendMousePositionEvent().
//
// This function can be useful when mouse capture is the only feasible way to receive mouse input,
// like on Android or iOS, and the OS cannot provide raw unaccelerated mouse motion when capturing.
// Using this function avoids double-acceleration in cases when the client motion is also accelerated.
int LiSendMouseMoveAsMousePositionEvent(short deltaX, short deltaY, short referenceWidth, short referenceHeight);

// Error return value to indicate that the requested functionality is not supported by the host
#define LI_ERR_UNSUPPORTED -5501

// This function allows multi-touch input to be sent directly to Sunshine hosts. The x and y values
// are normalized device coordinates stretching top-left corner (0.0, 0.0) to bottom-right corner
// (1.0, 1.0) of the video area.
//
// Pointer ID is an opaque ID that must uniquely identify each active touch on screen. It must
// remain constant through any down/up/move/cancel events involved in a single touch interaction.
//
// Rotation is in degrees from vertical in Y dimension (parallel to screen, 0..360). If rotation is
// unknown, pass LI_ROT_UNKNOWN.
//
// Pressure is a 0.0 to 1.0 range value from min to max pressure. Sending a down/move event with
// a pressure of 0.0 indicates the actual pressure is unknown.
//
// For hover events, the pressure value is treated as a 1.0 to 0.0 range of distance from the touch
// surface where 1.0 is the farthest measurable distance and 0.0 is actually touching the display
// (which is invalid for a hover event). Reporting distance 0.0 for a hover event indicates the
// actual distance is unknown.
//
// Contact area is modelled as an ellipse with major and minor axis values in normalized device
// coordinates. If contact area is unknown, report 0.0 for both contact area axis parameters.
// For circular contact areas or if a minor axis value is not available, pass the same value
// for major and minor axes. For APIs or devices, that don't report contact area as an ellipse,
// approximations can be used such as: https://docs.kernel.org/input/multi-touch-protocol.html#event-computation
//
// For hover events, the "contact area" is the size of the hovering finger/tool. If unavailable,
// pass 0.0 for both contact area parameters.
//
// Touches can be cancelled using LI_TOUCH_EVENT_CANCEL or LI_TOUCH_EVENT_CANCEL_ALL. When using
// LI_TOUCH_EVENT_CANCEL, only the pointerId parameter is valid. All other parameters are ignored.
// To cancel all active touches (on focus loss, for example), use LI_TOUCH_EVENT_CANCEL_ALL.
//
// If unsupported by the host, this will return LI_ERR_UNSUPPORTED and the caller should consider
// falling back to other functions to send this input (such as LiSendMousePositionEvent()).
//
// To determine if LiSendTouchEvent() is supported without calling it, call LiGetHostFeatureFlags()
// and check for the LI_FF_PEN_TOUCH_EVENTS flag.
#define LI_TOUCH_EVENT_HOVER       0x00
#define LI_TOUCH_EVENT_DOWN        0x01
#define LI_TOUCH_EVENT_UP          0x02
#define LI_TOUCH_EVENT_MOVE        0x03
#define LI_TOUCH_EVENT_CANCEL      0x04
#define LI_TOUCH_EVENT_BUTTON_ONLY 0x05
#define LI_TOUCH_EVENT_HOVER_LEAVE 0x06
#define LI_TOUCH_EVENT_CANCEL_ALL  0x07
#define LI_ROT_UNKNOWN 0xFFFF
int LiSendTouchEvent(uint8_t eventType, uint32_t pointerId, float x, float y, float pressureOrDistance,
                     float contactAreaMajor, float contactAreaMinor, uint16_t rotation);

// This function is similar to LiSendTouchEvent() but allows additional parameters relevant for pen
// input, including tilt and buttons. Tilt is in degrees from vertical in Z dimension (perpendicular
// to screen, 0..90). See LiSendTouchEvent() for detailed documentation on other parameters.
//
// x, y, pressure, rotation, contact area, and tilt are ignored for LI_TOUCH_EVENT_BUTTON_ONLY events.
// If one of those changes, send LI_TOUCH_EVENT_MOVE or LI_TOUCH_EVENT_HOVER instead.
//
// To determine if LiSendPenEvent() is supported without calling it, call LiGetHostFeatureFlags()
// and check for the LI_FF_PEN_TOUCH_EVENTS flag.
#define LI_TOOL_TYPE_UNKNOWN 0x00
#define LI_TOOL_TYPE_PEN     0x01
#define LI_TOOL_TYPE_ERASER  0x02
#define LI_PEN_BUTTON_PRIMARY   0x01
#define LI_PEN_BUTTON_SECONDARY 0x02
#define LI_PEN_BUTTON_TERTIARY  0x04
#define LI_TILT_UNKNOWN 0xFF
int LiSendPenEvent(uint8_t eventType, uint8_t toolType, uint8_t penButtons,
                   float x, float y, float pressureOrDistance,
                   float contactAreaMajor, float contactAreaMinor,
                   uint16_t rotation, uint8_t tilt);

// This function queues a mouse button event to be sent to the remote server.
#define BUTTON_ACTION_PRESS 0x07
#define BUTTON_ACTION_RELEASE 0x08
#define BUTTON_LEFT 0x01
#define BUTTON_MIDDLE 0x02
#define BUTTON_RIGHT 0x03
#define BUTTON_X1 0x04
#define BUTTON_X2 0x05
int LiSendMouseButtonEvent(char action, int button);

// This function queues a keyboard event to be sent to the remote server.
// Key codes are Win32 Virtual Key (VK) codes and interpreted as keys on
// a US English layout.
#define KEY_ACTION_DOWN 0x03
#define KEY_ACTION_UP 0x04
#define MODIFIER_SHIFT 0x01
#define MODIFIER_CTRL 0x02
#define MODIFIER_ALT 0x04
#define MODIFIER_META 0x08
int LiSendKeyboardEvent(short keyCode, char keyAction, char modifiers);

// Similar to LiSendKeyboardEvent() but allows the client to inform the host that
// the keycode was not mapped to a standard US English scancode and should be
// interpreted as-is. This is a Sunshine protocol extension.
#define SS_KBE_FLAG_NON_NORMALIZED 0x01
int LiSendKeyboardEvent2(short keyCode, char keyAction, char modifiers, char flags);

// This function queues an UTF-8 encoded text to be sent to the remote server.
int LiSendUtf8TextEvent(const char *text, unsigned int length);

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

// Extended buttons (Sunshine only)
#define PADDLE1_FLAG  0x010000
#define PADDLE2_FLAG  0x020000
#define PADDLE3_FLAG  0x040000
#define PADDLE4_FLAG  0x080000
#define TOUCHPAD_FLAG 0x100000 // Touchpad buttons on Sony controllers
#define MISC_FLAG     0x200000 // Share/Mic/Capture/Mute buttons on various controllers

// This function queues a controller event to be sent to the remote server. It will
// be seen by the computer as the first controller.
int LiSendControllerEvent(int buttonFlags, unsigned char leftTrigger, unsigned char rightTrigger,
    short leftStickX, short leftStickY, short rightStickX, short rightStickY);

// This function queues a controller event to be sent to the remote server. The controllerNumber
// parameter is a zero-based index of which controller this event corresponds to. The largest legal
// controller number is 3 for GFE hosts and 15 for Sunshine hosts. On generation 3 servers (GFE 2.1.x),
// these will be sent as controller 0 regardless of the controllerNumber parameter.
//
// The activeGamepadMask parameter is a bitfield with bits set for each controller present.
// On GFE, activeGamepadMask is limited to a maximum of 4 bits (0xF).
// On Sunshine, it is limited to 16 bits (0xFFFF).
//
// To indicate arrival of a gamepad, you may send an empty event with the controller number
// set to the new controller and the bit of the new controller set in the active gamepad mask.
// However, you should prefer LiSendControllerArrivalEvent() instead of this function for
// that purpose, because it allows the host to make a better choice of emulated controller.
//
// To indicate removal of a gamepad, send an empty event with the controller number set to the
// removed controller and the bit of the removed controller cleared in the active gamepad mask.
int LiSendMultiControllerEvent(short controllerNumber, short activeGamepadMask,
    int buttonFlags, unsigned char leftTrigger, unsigned char rightTrigger,
    short leftStickX, short leftStickY, short rightStickX, short rightStickY);

// This function provides a method of informing the host of the available buttons and capabilities
// on a new controller. This is the recommended approach for indicating the arrival of a new controller.
//
// This can allow the host to make better decisions about what type of controller to emulate and what
// capabilities to advertise to the OS on the virtual controller.
//
// If controller arrival events are unsupported by the host, this will fall back to indicating
// arrival via LiSendMultiControllerEvent().
#define LI_CTYPE_UNKNOWN  0x00
#define LI_CTYPE_XBOX     0x01
#define LI_CTYPE_PS       0x02
#define LI_CTYPE_NINTENDO 0x03
#define LI_CCAP_ANALOG_TRIGGERS 0x01 // Reports values between 0x00 and 0xFF for trigger axes
#define LI_CCAP_RUMBLE          0x02 // Can rumble in response to ConnListenerRumble() callback
#define LI_CCAP_TRIGGER_RUMBLE  0x04 // Can rumble triggers in response to ConnListenerRumbleTriggers() callback
#define LI_CCAP_TOUCHPAD        0x08 // Reports touchpad events via LiSendControllerTouchEvent()
#define LI_CCAP_ACCEL           0x10 // Can report accelerometer events via LiSendControllerMotionEvent()
#define LI_CCAP_GYRO            0x20 // Can report gyroscope events via LiSendControllerMotionEvent()
#define LI_CCAP_BATTERY_STATE   0x40 // Reports battery state via LiSendControllerBatteryEvent()
#define LI_CCAP_RGB_LED         0x80 // Can set RGB LED state via ConnListenerSetControllerLED()
int LiSendControllerArrivalEvent(uint8_t controllerNumber, uint16_t activeGamepadMask, uint8_t type,
                                 uint32_t supportedButtonFlags, uint16_t capabilities);

// This function is similar to LiSendTouchEvent(), but the touch events are associated with a
// touchpad device present on a game controller instead of a touchscreen.
//
// If unsupported by the host, this will return LI_ERR_UNSUPPORTED and the caller should consider
// using this touch input to simulate trackpad input.
//
// To determine if LiSendControllerTouchEvent() is supported without calling it, call LiGetHostFeatureFlags()
// and check for the LI_FF_CONTROLLER_TOUCH_EVENTS flag.
int LiSendControllerTouchEvent(uint8_t controllerNumber, uint8_t eventType, uint32_t pointerId, float x, float y, float pressure);

// This function allows clients to send controller-associated motion events to a supported host.
//
// For power and performance reasons, motion sensors should not be enabled unless the host has
// explicitly asked for motion event reports via ConnListenerSetMotionEventState().
//
// LI_MOTION_TYPE_ACCEL should report data in m/s^2 (inclusive of gravitational acceleration).
// LI_MOTION_TYPE_GYRO should report data in deg/s.
//
// The x/y/z axis assignments follow SDL's convention documented here:
// https://github.com/libsdl-org/SDL/blob/96720f335002bef62115e39327940df454d78f6c/include/SDL3/SDL_sensor.h#L80-L124
#define LI_MOTION_TYPE_ACCEL 0x01
#define LI_MOTION_TYPE_GYRO  0x02
int LiSendControllerMotionEvent(uint8_t controllerNumber, uint8_t motionType, float x, float y, float z);

// This function allows clients to send controller battery state to a supported host. If the
// host can adjust battery state on the emulated controller, it can use this information to
// make the virtual controller match the physical controller on the client.
#define LI_BATTERY_STATE_UNKNOWN      0x00
#define LI_BATTERY_STATE_NOT_PRESENT  0x01
#define LI_BATTERY_STATE_DISCHARGING  0x02
#define LI_BATTERY_STATE_CHARGING     0x03
#define LI_BATTERY_STATE_NOT_CHARGING 0x04 // Connected to power but not charging
#define LI_BATTERY_STATE_FULL         0x05
#define LI_BATTERY_PERCENTAGE_UNKNOWN 0xFF
int LiSendControllerBatteryEvent(uint8_t controllerNumber, uint8_t batteryState, uint8_t batteryPercentage);

// This function queues a vertical scroll event to the remote server.
// The number of "clicks" is multiplied by WHEEL_DELTA (120) before
// being sent to the PC.
int LiSendScrollEvent(signed char scrollClicks);

// This function queues a vertical scroll event to the remote server.
// Unlike LiSendScrollEvent(), this function can send wheel events
// smaller than 120 units for devices that support "high resolution"
// scrolling (Apple Trackpads, Microsoft Precision Touchpads, etc.).
int LiSendHighResScrollEvent(short scrollAmount);

// These functions send horizontal scroll events to the host which are
// analogous to LiSendScrollEvent() and LiSendHighResScrollEvent().
// This is a Sunshine protocol extension.
int LiSendHScrollEvent(signed char scrollClicks);
int LiSendHighResHScrollEvent(short scrollAmount);

// This function returns a time in milliseconds with an implementation-defined epoch.
uint64_t LiGetMillis(void);

// This is a simplistic STUN function that can assist clients in getting the WAN address
// for machines they find using mDNS over IPv4. This can be used to pre-populate the external
// address for streaming after GFE stopped sending it a while back. wanAddr is returned in
// network byte order.
int LiFindExternalAddressIP4(const char* stunServer, unsigned short stunPort, unsigned int* wanAddr);

// Returns the number of queued video frames ready for delivery. Only relevant
// if CAPABILITY_DIRECT_SUBMIT is not set for the video renderer.
int LiGetPendingVideoFrames(void);

// Returns the number of queued audio frames ready for delivery. Only relevant
// if CAPABILITY_DIRECT_SUBMIT is not set for the audio renderer. For most uses,
// LiGetPendingAudioDuration() is probably a better option than this function.
int LiGetPendingAudioFrames(void);

// Similar to LiGetPendingAudioFrames() except it returns the pending audio in
// milliseconds rather than frames, which allows callers to be agnostic of the
// negotiated audio frame duration.
int LiGetPendingAudioDuration(void);

typedef struct _RTP_VIDEO_STATS {
    uint32_t packetCountVideo; // total video packets
    uint32_t packetCountFec;   // total packets of type FEC
} RTP_VIDEO_STATS, *PRTP_VIDEO_STATS;

// Returns a pointer to a struct containing data used to compute stats about the RTP video stream.
// The data should be considered read-only and must not be modified.
const RTP_VIDEO_STATS* LiGetRTPVideoStats(void);

// Port index flags for use with LiGetPortFromPortFlagIndex() and LiGetProtocolFromPortFlagIndex()
#define ML_PORT_INDEX_TCP_47984 0
#define ML_PORT_INDEX_TCP_47989 1
#define ML_PORT_INDEX_TCP_48010 2
#define ML_PORT_INDEX_UDP_47998 8
#define ML_PORT_INDEX_UDP_47999 9
#define ML_PORT_INDEX_UDP_48000 10
#define ML_PORT_INDEX_UDP_48010 11

// Port flags for use with LiTestClientConnectivity()
#define ML_PORT_FLAG_ALL       0xFFFFFFFF
#define ML_PORT_FLAG_TCP_47984 0x0001
#define ML_PORT_FLAG_TCP_47989 0x0002
#define ML_PORT_FLAG_TCP_48010 0x0004
#define ML_PORT_FLAG_UDP_47998 0x0100
#define ML_PORT_FLAG_UDP_47999 0x0200
#define ML_PORT_FLAG_UDP_48000 0x0400
#define ML_PORT_FLAG_UDP_48010 0x0800

// Returns the port flags that correspond to ports involved in a failing connection stage, or
// connection termination error.
//
// These may be used to specifically test the ports that could have caused the connection failure.
// If no ports are likely involved with a given failure, this function returns 0.
unsigned int LiGetPortFlagsFromStage(int stage);
unsigned int LiGetPortFlagsFromTerminationErrorCode(int errorCode);

// Returns the IPPROTO_* value for the specified port index
int LiGetProtocolFromPortFlagIndex(int portFlagIndex);

// Returns the port number for the specified port index
unsigned short LiGetPortFromPortFlagIndex(int portFlagIndex);

// Populates the output buffer with a stringified list of the port flags set in the input argument.
// The second and subsequent entries will be prepended by 'separator' (if provided).
// If the output buffer is too small, the output will be truncated to fit the provided buffer.
void LiStringifyPortFlags(unsigned int portFlags, const char* separator, char* outputBuffer, int outputBufferLength);

// This function may be used to test if the local network is blocking Moonlight's ports. It requires
// a test server running on an Internet-reachable host. To perform a test, pass in the DNS hostname
// of the test server, a reference TCP port to ensure the test host is reachable at all (something
// very unlikely to blocked, like 80 or 443), and a set of ML_PORT_FLAG_* values corresponding to
// the ports you'd like to test. On return, it returns ML_TEST_RESULT_INCONCLUSIVE on catastrophic error,
// or the set of port flags that failed to validate. If all ports validate successfully, it returns 0.
//
// It's encouraged to not use the port flags explicitly (because GameStream ports may change in the future),
// but to instead use ML_PORT_FLAG_ALL or LiGetPortFlagsFromStage() on connection failure.
//
// The test server is available at https://github.com/cgutman/gfe-loopback
#define ML_TEST_RESULT_INCONCLUSIVE 0xFFFFFFFF
unsigned int LiTestClientConnectivity(const char* testServer, unsigned short referencePort, unsigned int testPortFlags);

// This family of functions can be used for pull-based video renderers that opt to manage a decoding/rendering
// thread themselves. After successfully calling the WaitFor/Poll variants that dequeue the video frame, you
// must call LiCompleteVideoFrame() to notify that processing is completed. The same DR_* status values
// from drSubmitDecodeUnit() must be passed to LiCompleteVideoFrame() as the drStatus argument.
//
// In order to safely use these functions, you must set CAPABILITY_PULL_RENDERER on the video decoder.
typedef void* VIDEO_FRAME_HANDLE;
bool LiWaitForNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit);
bool LiPollNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit);
bool LiPeekNextVideoFrame(PDECODE_UNIT* decodeUnit);
void LiWakeWaitForVideoFrame(void);
void LiCompleteVideoFrame(VIDEO_FRAME_HANDLE handle, int drStatus);

// This function returns the last reported HDR mode from the host PC.
// See ConnListenerSetHdrMode() for more details.
bool LiGetCurrentHostDisplayHdrMode(void);

typedef struct _SS_HDR_METADATA {
    // RGB order
    struct {
        uint16_t x; // Normalized to 50,000
        uint16_t y; // Normalized to 50,000
    } displayPrimaries[3];

    struct {
        uint16_t x; // Normalized to 50,000
        uint16_t y; // Normalized to 50,000
    } whitePoint;

    uint16_t maxDisplayLuminance; // Nits
    uint16_t minDisplayLuminance; // 1/10000th of a nit

    // These are content-specific values which may not be available for all hosts.
    uint16_t maxContentLightLevel; // Nits
    uint16_t maxFrameAverageLightLevel; // Nits

    // These are display-specific values which may not be available for all hosts.
    uint16_t maxFullFrameLuminance; // Nits
} SS_HDR_METADATA, *PSS_HDR_METADATA;

// This function populates the provided mastering metadata struct with the HDR metadata
// from the host PC's monitor and content (if available). It is only valid to call this
// function when HDR mode is active on the host. This is a Sunshine protocol extension.
bool LiGetHdrMetadata(PSS_HDR_METADATA metadata);

// This function requests an IDR frame from the host. Typically this is done using DR_NEED_IDR, but clients
// processing frames asynchronously may need to reset their decoder state even after returning DR_OK for
// the prior frame. Rather than wait for a new frame and return DR_NEED_IDR for that one, they can just
// call this API instead. Note that this function does not guarantee that the *next* frame will be an IDR
// frame, just that an IDR frame will arrive soon.
void LiRequestIdrFrame(void);

// This function returns any extended feature flags supported by the host.
#define LI_FF_PEN_TOUCH_EVENTS        0x01 // LiSendTouchEvent()/LiSendPenEvent() supported
#define LI_FF_CONTROLLER_TOUCH_EVENTS 0x02 // LiSendControllerTouchEvent() supported
uint32_t LiGetHostFeatureFlags(void);

#ifdef __cplusplus
}
#endif
