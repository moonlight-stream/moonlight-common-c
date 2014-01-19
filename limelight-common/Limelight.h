#include "Platform.h"
#include "PlatformSockets.h"

typedef struct _STREAM_CONFIGURATION {
	int width;
	int height;
	int fps;
} STREAM_CONFIGURATION, *PSTREAM_CONFIGURATION;

typedef struct _LENTRY {
	struct _LENTRY *next;
} LENTRY, *PLENTRY;

#include <stdio.h>
#define Limelog printf

char* allocateConfigDataForStreamConfig(PSTREAM_CONFIGURATION streamConfig);
int getConfigDataSize(PSTREAM_CONFIGURATION streamConfig);

void* initializeControlStream(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig);
int startControlStream(void* context);
int stopControlStream(void* context);