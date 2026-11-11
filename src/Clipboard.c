#include "Clipboard.h"

#include <stdlib.h>
#include <string.h>

static void put16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t* p, uint32_t value) {
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
}

static uint16_t get16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get32(const uint8_t* p) {
    return get16(p) | ((uint32_t)get16(p + 2) << 16);
}

// Returns the length of a valid MIME type, or -1. MIME types are printable
// ASCII without spaces, up to SS_CLIPBOARD_MIME_MAX bytes.
static int mimeTypeLength(const char* mimeType) {
    int length;

    if (mimeType == NULL) {
        return -1;
    }

    for (length = 0; mimeType[length] != 0; length++) {
        if (length == SS_CLIPBOARD_MIME_MAX || mimeType[length] <= ' ' || mimeType[length] > '~') {
            return -1;
        }
    }

    return length > 0 ? length : -1;
}

// Copies a length-prefixed MIME type out of a body. Returns the bytes used, or -1.
static int readMimeType(const uint8_t* p, int remaining, char* mimeType) {
    int length;

    if (remaining < 2) {
        return -1;
    }

    length = get16(p);
    if (length > SS_CLIPBOARD_MIME_MAX || length > remaining - 2) {
        return -1;
    }

    memcpy(mimeType, p + 2, length);
    mimeType[length] = 0;
    if (mimeTypeLength(mimeType) != length) {
        return -1;
    }

    return 2 + length;
}

static int writeMimeType(uint8_t* p, int remaining, const char* mimeType) {
    int length = mimeTypeLength(mimeType);

    if (length < 0 || remaining < 2 + length) {
        return -1;
    }

    put16(p, (uint16_t)length);
    memcpy(p + 2, mimeType, length);
    return 2 + length;
}

uint16_t ClipboardChunkCount(uint32_t length) {
    if (length == 0) {
        return 1;
    }

    return (uint16_t)((length - 1) / SS_CLIPBOARD_CHUNK_MAX + 1);
}

int ClipboardEncodeOffer(uint8_t* buffer, int bufferLength, const char* const* mimeTypes, int mimeTypeCount, uint32_t sequence) {
    int offset = 6;
    int i;

    if (buffer == NULL || bufferLength < offset || mimeTypeCount < 0 || mimeTypeCount > SS_CLIPBOARD_MIME_TYPES_MAX ||
            (mimeTypeCount > 0 && mimeTypes == NULL)) {
        return -1;
    }

    put32(buffer, sequence);
    put16(buffer + 4, (uint16_t)mimeTypeCount);
    for (i = 0; i < mimeTypeCount; i++) {
        int used = writeMimeType(buffer + offset, bufferLength - offset, mimeTypes[i]);
        if (used < 0) {
            return -1;
        }
        offset += used;
    }

    return offset;
}

int ClipboardEncodeRequest(uint8_t* buffer, int bufferLength, const char* mimeType, uint32_t sequence) {
    int used;

    if (buffer == NULL || bufferLength < 4) {
        return -1;
    }

    used = writeMimeType(buffer + 4, bufferLength - 4, mimeType);
    if (used < 0) {
        return -1;
    }

    put32(buffer, sequence);
    return 4 + used;
}

int ClipboardEncodeChunk(uint8_t* buffer, int bufferLength, const char* mimeType, uint32_t sequence,
                         uint16_t index, uint16_t count, const void* data, uint32_t length) {
    int used;

    if (buffer == NULL || bufferLength < 8 || count == 0 || count > SS_CLIPBOARD_CHUNKS_MAX || index >= count ||
            length > SS_CLIPBOARD_CHUNK_MAX || (length > 0 && data == NULL)) {
        return -1;
    }

    used = writeMimeType(buffer + 8, bufferLength - 8, mimeType);
    if (used < 0 || (uint32_t)(bufferLength - 8 - used) < length) {
        return -1;
    }

    put32(buffer, sequence);
    put16(buffer + 4, index);
    put16(buffer + 6, count);
    if (length > 0) {
        memcpy(buffer + 8 + used, data, length);
    }

    return 8 + used + (int)length;
}

bool ClipboardDecodeOffer(const uint8_t* body, int length, PCLIPBOARD_OFFER offer) {
    char mimeType[SS_CLIPBOARD_MIME_MAX + 1];
    int offset = 6;
    int count;
    int i;

    if (body == NULL || length < offset) {
        return false;
    }

    offer->sequence = get32(body);
    offer->mimeTypeCount = 0;
    count = get16(body + 4);
    for (i = 0; i < count; i++) {
        int used = readMimeType(body + offset, length - offset, mimeType);
        if (used < 0) {
            return false;
        }
        if (offer->mimeTypeCount < SS_CLIPBOARD_MIME_TYPES_MAX) {
            memcpy(offer->mimeTypes[offer->mimeTypeCount++], mimeType, used - 1);
        }
        offset += used;
    }

    return offset == length;
}

bool ClipboardDecodeRequest(const uint8_t* body, int length, PCLIPBOARD_REQUEST request) {
    if (body == NULL || length < 4) {
        return false;
    }

    request->sequence = get32(body);
    return readMimeType(body + 4, length - 4, request->mimeType) == length - 4;
}

bool ClipboardDecodeChunk(const uint8_t* body, int length, PCLIPBOARD_CHUNK chunk) {
    int used;

    if (body == NULL || length < 8) {
        return false;
    }

    chunk->sequence = get32(body);
    chunk->index = get16(body + 4);
    chunk->count = get16(body + 6);
    used = readMimeType(body + 8, length - 8, chunk->mimeType);
    if (used < 0 || chunk->count == 0 || chunk->count > SS_CLIPBOARD_CHUNKS_MAX || chunk->index >= chunk->count) {
        return false;
    }

    chunk->length = (uint32_t)(length - 8 - used);
    chunk->data = chunk->length > 0 ? body + 8 + used : NULL;
    return chunk->length <= SS_CLIPBOARD_CHUNK_MAX;
}

int ClipboardAssemblerAdd(PCLIPBOARD_ASSEMBLER assembler, const CLIPBOARD_CHUNK* chunk) {
    if (chunk->index == 0) {
        ClipboardAssemblerReset(assembler);
        assembler->sequence = chunk->sequence;
        assembler->count = chunk->count;
        strcpy(assembler->mimeType, chunk->mimeType);
    }
    else if (chunk->index != assembler->received || chunk->count != assembler->count ||
             chunk->sequence != assembler->sequence || strcmp(chunk->mimeType, assembler->mimeType) != 0) {
        ClipboardAssemblerReset(assembler);
        return -1;
    }

    if (chunk->length > SS_CLIPBOARD_DATA_MAX - assembler->length) {
        ClipboardAssemblerReset(assembler);
        return -1;
    }

    if (chunk->length > 0) {
        uint8_t* data = realloc(assembler->data, assembler->length + chunk->length);
        if (data == NULL) {
            ClipboardAssemblerReset(assembler);
            return -1;
        }
        memcpy(data + assembler->length, chunk->data, chunk->length);
        assembler->data = data;
        assembler->length += chunk->length;
    }

    assembler->received++;
    return assembler->received == assembler->count ? 1 : 0;
}

void ClipboardAssemblerReset(PCLIPBOARD_ASSEMBLER assembler) {
    free(assembler->data);
    memset(assembler, 0, sizeof(*assembler));
}
