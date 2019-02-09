#pragma once

#include "Platform.h"

#define BYTE_ORDER_LITTLE 1
#define BYTE_ORDER_BIG 2

#ifndef HTONLL
#define HTONLL(x) \
    ((((x) & 0xff00000000000000ull) >> 56)                    \
    | (((x) & 0x00ff000000000000ull) >> 40)                   \
    | (((x) & 0x0000ff0000000000ull) >> 24)                   \
    | (((x) & 0x000000ff00000000ull) >> 8)                    \
    | (((x) & 0x00000000ff000000ull) << 8)                    \
    | (((x) & 0x0000000000ff0000ull) << 24)                   \
    | (((x) & 0x000000000000ff00ull) << 40)                   \
    | (((x) & 0x00000000000000ffull) << 56))
#endif

typedef struct _BYTE_BUFFER {
    char* buffer;
    unsigned int length;
    unsigned int position;
    unsigned int byteOrder;
} BYTE_BUFFER, *PBYTE_BUFFER;

void BbInitializeWrappedBuffer(PBYTE_BUFFER buff, char* data, int offset, int length, int byteOrder);
int BbAdvanceBuffer(PBYTE_BUFFER buff, int offset);

int BbGet(PBYTE_BUFFER buff, char* c);
int BbGetShort(PBYTE_BUFFER buff, short* s);
int BbGetInt(PBYTE_BUFFER buff, int* i);
int BbGetLong(PBYTE_BUFFER buff, long long* l);

int BbPutInt(PBYTE_BUFFER buff, int i);
int BbPutShort(PBYTE_BUFFER buff, short s);
int BbPut(PBYTE_BUFFER buff, char c);
int BbPutLong(PBYTE_BUFFER buff, long long l);
