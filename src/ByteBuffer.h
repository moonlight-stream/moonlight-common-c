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
bool BbAdvanceBuffer(PBYTE_BUFFER buff, int offset);

bool BbGet8(PBYTE_BUFFER buff, uint8_t* c);
bool BbGet16(PBYTE_BUFFER buff, uint16_t* s);
bool BbGet32(PBYTE_BUFFER buff, uint32_t* i);
bool BbGet64(PBYTE_BUFFER buff, uint64_t* l);

bool BbPut8(PBYTE_BUFFER buff, uint8_t c);
bool BbPut16(PBYTE_BUFFER buff, uint16_t s);
bool BbPut32(PBYTE_BUFFER buff, uint32_t i);
bool BbPut64(PBYTE_BUFFER buff, uint64_t l);
