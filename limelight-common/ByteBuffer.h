#pragma once

#include "Platform.h"

#define BYTE_ORDER_LITTLE 1
#define BYTE_ORDER_BIG 2

typedef struct _BYTE_BUFFER {
	char* buffer;
	unsigned int offset;
	unsigned int length;
	unsigned int position;
	unsigned int byteOrder;
} BYTE_BUFFER, *PBYTE_BUFFER;

void BbInitializeWrappedBuffer(PBYTE_BUFFER buff, char* data, int offset, int length, int byteOrder);

int BbGet(PBYTE_BUFFER buff, char *c);
int BbGetShort(PBYTE_BUFFER buff, short *s);
int BbGetInt(PBYTE_BUFFER buff, int *i);

int BbPutInt(PBYTE_BUFFER buff, int i);
int BbPutShort(PBYTE_BUFFER buff, short s);
int BbPut(PBYTE_BUFFER buff, char c);