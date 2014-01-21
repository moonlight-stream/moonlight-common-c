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

int LiStartConnection(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig, PDECODER_RENDERER_CALLBACKS drCallbacks, void* renderContext, int drFlags);
void LiStopConnection(void);

#ifdef __cplusplus
}
#endif