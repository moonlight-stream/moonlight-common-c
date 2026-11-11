#pragma once

#include "Limelight.h"

#ifdef __cplusplus
extern "C" {
#endif

// Clipboard sync is a Sunshine protocol extension. The client sends these bodies
// in input packets (see Input.h) and the host sends them as control messages.
//
// Bodies are little-endian:
// Offer:   u32 sequence, u16 count, then count x (u16 length, MIME type)
// Request: u32 sequence, u16 length, MIME type
// Data:    u32 sequence, u16 chunk index, u16 chunk count, u16 length, MIME type, chunk bytes
//
// Data larger than SS_CLIPBOARD_CHUNK_MAX is split into chunks, which are sent
// in order on one reliable channel.

#define SS_CLIPBOARD_OFFER_PTYPE   0x5505
#define SS_CLIPBOARD_REQUEST_PTYPE 0x5506
#define SS_CLIPBOARD_DATA_PTYPE    0x5507

#define SS_CLIPBOARD_CHUNK_MAX (4 * 1024 * 1024)
#define SS_CLIPBOARD_CHUNKS_MAX (SS_CLIPBOARD_DATA_MAX / SS_CLIPBOARD_CHUNK_MAX)
#define SS_CLIPBOARD_DATA_HEADER_MAX (10 + SS_CLIPBOARD_MIME_MAX)
#define SS_CLIPBOARD_OFFER_MAX (6 + SS_CLIPBOARD_MIME_TYPES_MAX * (2 + SS_CLIPBOARD_MIME_MAX))
#define SS_CLIPBOARD_REQUEST_MAX (6 + SS_CLIPBOARD_MIME_MAX)

typedef struct _CLIPBOARD_OFFER {
    uint32_t sequence;
    int mimeTypeCount;
    char mimeTypes[SS_CLIPBOARD_MIME_TYPES_MAX][SS_CLIPBOARD_MIME_MAX + 1];
} CLIPBOARD_OFFER, *PCLIPBOARD_OFFER;

typedef struct _CLIPBOARD_REQUEST {
    uint32_t sequence;
    char mimeType[SS_CLIPBOARD_MIME_MAX + 1];
} CLIPBOARD_REQUEST, *PCLIPBOARD_REQUEST;

typedef struct _CLIPBOARD_CHUNK {
    uint32_t sequence;
    uint16_t index;
    uint16_t count;
    char mimeType[SS_CLIPBOARD_MIME_MAX + 1];
    const uint8_t* data; // Points into the decoded body
    uint32_t length;
} CLIPBOARD_CHUNK, *PCLIPBOARD_CHUNK;

// Joins the chunks of one data message. Zero-initialize it before first use.
typedef struct _CLIPBOARD_ASSEMBLER {
    uint32_t sequence;
    uint16_t received;
    uint16_t count;
    char mimeType[SS_CLIPBOARD_MIME_MAX + 1];
    uint8_t* data; // malloc()ed, NULL when length is 0
    uint32_t length;
} CLIPBOARD_ASSEMBLER, *PCLIPBOARD_ASSEMBLER;

// Returns the number of chunks needed for this many bytes of data.
uint16_t ClipboardChunkCount(uint32_t length);

// Each encoder returns the body length, or -1 if the input is invalid or does not fit.
int ClipboardEncodeOffer(uint8_t* buffer, int bufferLength, const char* const* mimeTypes, int mimeTypeCount, uint32_t sequence);
int ClipboardEncodeRequest(uint8_t* buffer, int bufferLength, const char* mimeType, uint32_t sequence);
int ClipboardEncodeChunk(uint8_t* buffer, int bufferLength, const char* mimeType, uint32_t sequence,
                         uint16_t index, uint16_t count, const void* data, uint32_t length);

// Each decoder returns false if the body is malformed. An offer with more than
// SS_CLIPBOARD_MIME_TYPES_MAX types keeps the first ones.
bool ClipboardDecodeOffer(const uint8_t* body, int length, PCLIPBOARD_OFFER offer);
bool ClipboardDecodeRequest(const uint8_t* body, int length, PCLIPBOARD_REQUEST request);
bool ClipboardDecodeChunk(const uint8_t* body, int length, PCLIPBOARD_CHUNK chunk);

// Adds the next chunk. Returns 1 when the data is complete, 0 when more chunks
// are needed, or -1 if the chunk was out of order or too large. A chunk with
// index 0 always starts new data. After 1, the caller takes assembler->data
// and should set it to NULL; otherwise ClipboardAssemblerReset() frees it.
int ClipboardAssemblerAdd(PCLIPBOARD_ASSEMBLER assembler, const CLIPBOARD_CHUNK* chunk);
void ClipboardAssemblerReset(PCLIPBOARD_ASSEMBLER assembler);

#ifdef __cplusplus
}
#endif
