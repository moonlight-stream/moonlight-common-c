#include "ByteBuffer.h"

void BbInitializeWrappedBuffer(PBYTE_BUFFER buff, char* data, int offset, int length, int byteOrder) {
    buff->buffer = data + offset;
    buff->length = length;
    buff->position = 0;
    buff->byteOrder = byteOrder;
}

// Get the long long in the correct byte order
static long long byteSwapLongLong(PBYTE_BUFFER buff, long long l) {
    if (buff->byteOrder == BYTE_ORDER_BIG) {
        return HTONLL(l);
    }
    else {
        return l;
    }
}

// Get the int in the correct byte order
static int byteSwapInt(PBYTE_BUFFER buff, int i) {
    if (buff->byteOrder == BYTE_ORDER_BIG) {
        return htonl(i);
    }
    else {
        return i;
    }
}

// Get the short in the correct byte order
static int byteSwapShort(PBYTE_BUFFER buff, short s) {
    if (buff->byteOrder == BYTE_ORDER_BIG) {
        return htons(s);
    }
    else {
        return s;
    }
}

int BbAdvanceBuffer(PBYTE_BUFFER buff, int offset) {
    if (buff->position + offset > buff->length) {
        return 0;
    }

    buff->position += offset;

    return 1;
}

// Get a byte from the byte buffer
int BbGet(PBYTE_BUFFER buff, char* c) {
    if (buff->position + sizeof(*c) > buff->length) {
        return 0;
    }

    memcpy(c, &buff->buffer[buff->position], sizeof(*c));
    buff->position += sizeof(*c);

    return 1;
}

// Get a short from the byte buffer
int BbGetShort(PBYTE_BUFFER buff, short* s) {
    if (buff->position + sizeof(*s) > buff->length) {
        return 0;
    }

    memcpy(s, &buff->buffer[buff->position], sizeof(*s));
    buff->position += sizeof(*s);

    *s = byteSwapShort(buff, *s);

    return 1;
}

// Get an int from the byte buffer
int BbGetInt(PBYTE_BUFFER buff, int* i) {
    if (buff->position + sizeof(*i) > buff->length) {
        return 0;
    }

    memcpy(i, &buff->buffer[buff->position], sizeof(*i));
    buff->position += sizeof(*i);

    *i = byteSwapInt(buff, *i);

    return 1;
}

// Get a long from the byte buffer
int BbGetLong(PBYTE_BUFFER buff, long long* l) {
    if (buff->position + sizeof(*l) > buff->length) {
        return 0;
    }

    memcpy(l, &buff->buffer[buff->position], sizeof(*l));
    buff->position += sizeof(*l);

    *l = byteSwapLongLong(buff, *l);

    return 1;
}

// Put an int into the byte buffer
int BbPutInt(PBYTE_BUFFER buff, int i) {
    if (buff->position + sizeof(i) > buff->length) {
        return 0;
    }

    i = byteSwapInt(buff, i);

    memcpy(&buff->buffer[buff->position], &i, sizeof(i));
    buff->position += sizeof(i);

    return 1;
}

// Put a long into the byte buffer
int BbPutLong(PBYTE_BUFFER buff, long long l) {
    if (buff->position + sizeof(l) > buff->length) {
        return 0;
    }

    l = byteSwapLongLong(buff, l);

    memcpy(&buff->buffer[buff->position], &l, sizeof(l));
    buff->position += sizeof(l);

    return 1;
}

// Put a short into the byte buffer
int BbPutShort(PBYTE_BUFFER buff, short s) {
    if (buff->position + sizeof(s) > buff->length) {
        return 0;
    }

    s = byteSwapShort(buff, s);

    memcpy(&buff->buffer[buff->position], &s, sizeof(s));
    buff->position += sizeof(s);

    return 1;
}

// Put a byte into the buffer
int BbPut(PBYTE_BUFFER buff, char c) {
    if (buff->position + sizeof(c) > buff->length) {
        return 0;
    }

    memcpy(&buff->buffer[buff->position], &c, sizeof(c));
    buff->position += sizeof(c);

    return 1;
}
