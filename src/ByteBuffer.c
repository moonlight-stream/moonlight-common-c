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

bool BbAdvanceBuffer(PBYTE_BUFFER buff, int offset) {
    if (buff->position + offset > buff->length) {
        return false;
    }

    buff->position += offset;

    return true;
}

// Get a byte from the byte buffer
bool BbGet(PBYTE_BUFFER buff, char* c) {
    if (buff->position + sizeof(*c) > buff->length) {
        return false;
    }

    memcpy(c, &buff->buffer[buff->position], sizeof(*c));
    buff->position += sizeof(*c);

    return true;
}

// Get a short from the byte buffer
bool BbGetShort(PBYTE_BUFFER buff, short* s) {
    if (buff->position + sizeof(*s) > buff->length) {
        return false;
    }

    memcpy(s, &buff->buffer[buff->position], sizeof(*s));
    buff->position += sizeof(*s);

    *s = byteSwapShort(buff, *s);

    return true;
}

// Get an int from the byte buffer
bool BbGetInt(PBYTE_BUFFER buff, int* i) {
    if (buff->position + sizeof(*i) > buff->length) {
        return false;
    }

    memcpy(i, &buff->buffer[buff->position], sizeof(*i));
    buff->position += sizeof(*i);

    *i = byteSwapInt(buff, *i);

    return true;
}

// Get a long from the byte buffer
bool BbGetLong(PBYTE_BUFFER buff, long long* l) {
    if (buff->position + sizeof(*l) > buff->length) {
        return false;
    }

    memcpy(l, &buff->buffer[buff->position], sizeof(*l));
    buff->position += sizeof(*l);

    *l = byteSwapLongLong(buff, *l);

    return true;
}

// Put an int into the byte buffer
bool BbPutInt(PBYTE_BUFFER buff, int i) {
    if (buff->position + sizeof(i) > buff->length) {
        return false;
    }

    i = byteSwapInt(buff, i);

    memcpy(&buff->buffer[buff->position], &i, sizeof(i));
    buff->position += sizeof(i);

    return true;
}

// Put a long into the byte buffer
bool BbPutLong(PBYTE_BUFFER buff, long long l) {
    if (buff->position + sizeof(l) > buff->length) {
        return false;
    }

    l = byteSwapLongLong(buff, l);

    memcpy(&buff->buffer[buff->position], &l, sizeof(l));
    buff->position += sizeof(l);

    return true;
}

// Put a short into the byte buffer
bool BbPutShort(PBYTE_BUFFER buff, short s) {
    if (buff->position + sizeof(s) > buff->length) {
        return false;
    }

    s = byteSwapShort(buff, s);

    memcpy(&buff->buffer[buff->position], &s, sizeof(s));
    buff->position += sizeof(s);

    return true;
}

// Put a byte into the buffer
bool BbPut(PBYTE_BUFFER buff, char c) {
    if (buff->position + sizeof(c) > buff->length) {
        return false;
    }

    memcpy(&buff->buffer[buff->position], &c, sizeof(c));
    buff->position += sizeof(c);

    return true;
}
