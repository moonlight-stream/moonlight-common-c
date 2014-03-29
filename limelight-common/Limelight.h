#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define IP_ADDRESS unsigned int

typedef struct _STREAM_CONFIGURATION {
	int width;
	int height;
	int fps;
} STREAM_CONFIGURATION, *PSTREAM_CONFIGURATION;

typedef struct _LENTRY {
	struct _LENTRY *next;
	char* data;
	int length;
} LENTRY, *PLENTRY;

typedef struct _DECODE_UNIT {
	int fullLength;
	PLENTRY bufferList;
} DECODE_UNIT, *PDECODE_UNIT;

typedef void(*DecoderRendererSetup)(int width, int height, int redrawRate, void* context, int drFlags);
typedef void(*DecoderRendererStart)(void);
typedef void(*DecoderRendererStop)(void);
typedef void(*DecoderRendererRelease)(void);
typedef void(*DecoderRendererSubmitDecodeUnit)(PDECODE_UNIT decodeUnit);

typedef struct _DECODER_RENDERER_CALLBACKS {
	DecoderRendererSetup setup;
	DecoderRendererStart start;
	DecoderRendererStop stop;
	DecoderRendererRelease release;
	DecoderRendererSubmitDecodeUnit submitDecodeUnit;
} DECODER_RENDERER_CALLBACKS, *PDECODER_RENDERER_CALLBACKS;

typedef void(*AudioRendererInit)(void);
typedef void(*AudioRendererStart)(void);
typedef void(*AudioRendererStop)(void);
typedef void(*AudioRendererRelease)(void);
typedef void(*AudioRendererDecodeAndPlaySample)(char* sampleData, int sampleLength);

typedef struct _AUDIO_RENDERER_CALLBACKS {
	AudioRendererInit init;
	AudioRendererStart start;
	AudioRendererStop stop;
	AudioRendererRelease release;
	AudioRendererDecodeAndPlaySample decodeAndPlaySample;
} AUDIO_RENDERER_CALLBACKS, *PAUDIO_RENDERER_CALLBACKS;

// Subject to change in future releases
// Use LiGetStageName() for stable stage names
#define STAGE_NONE 0
#define STAGE_PLATFORM_INIT 1
#define STAGE_HANDSHAKE 2
#define STAGE_CONTROL_STREAM_INIT 3
#define STAGE_VIDEO_STREAM_INIT 4
#define STAGE_AUDIO_STREAM_INIT 5
#define STAGE_CONTROL_STREAM_START 6
#define STAGE_VIDEO_STREAM_START 7
#define STAGE_AUDIO_STREAM_START 8

typedef void(*ConnListenerStageStarting)(int stage);
typedef void(*ConnListenerStageComplete)(int stage);
typedef void(*ConnListenerStageFailed)(int stage);

typedef void(*ConnListenerConnectionStarted)(void);
typedef void(*ConnListenerConnectionTerminated)(int errorCode);

typedef void(*ConnListenerDisplayMessage)(char* message);
typedef void(*ConnListenerDisplayTransientMessage)(char* message);

typedef struct _CONNECTION_LISTENER_CALLBACKS {
	ConnListenerStageStarting stageStarting;
	ConnListenerStageComplete stageComplete;
	ConnListenerStageFailed stageFailed;
	ConnListenerConnectionStarted connectionStarted;
	ConnListenerConnectionTerminated connectionTerminated;
	ConnListenerDisplayMessage displayMessage;
	ConnListenerDisplayTransientMessage displayTransientMessage;
} CONNECTION_LISTENER_CALLBACKS, *PCONNECTION_LISTENER_CALLBACKS;

int LiStartConnection(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig, PCONNECTION_LISTENER_CALLBACKS clCallbacks,
	PDECODER_RENDERER_CALLBACKS drCallbacks, PAUDIO_RENDERER_CALLBACKS arCallbacks, void* renderContext, int drFlags);
void LiStopConnection(void);
const char* LiGetStageName(int stage);

#ifdef __cplusplus
}
#endif