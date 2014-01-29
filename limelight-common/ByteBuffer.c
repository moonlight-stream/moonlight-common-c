#include "ByteBuffer.h"

void BbInitializeWrappedBuffer(PBYTE_BUFFER buff, char* data, int offset, int length, int byteOrder) {
	buff->buffer = data;
	buff->offset = offset;
	buff->length = length;
	buff->position = 0;
	buff->byteOrder = byteOrder;
}

static int byteSwapInt(PBYTE_BUFFER buff, int i) {
	if (buff->byteOrder == BYTE_ORDER_BIG) {
		return htonl(i);
	}
	else {
		return i;
	}
}

static int byteSwapShort(PBYTE_BUFFER buff, short s) {
	if (buff->byteOrder == BYTE_ORDER_BIG) {
		return htons(s);
	}
	else {
		return s;
	}
}

int BbGet(PBYTE_BUFFER buff, char *c) {
	if (buff->position + sizeof(*c) > buff->length) {
		return 0;
	}

	memcpy(c, &buff->buffer[buff->position], sizeof(*c));
	buff->position += sizeof(*c);

	return 1;
}

int BbGetShort(PBYTE_BUFFER buff, short *s) {
	if (buff->position + sizeof(*s) >= buff->length) {
		return 0;
	}

	memcpy(s, &buff->buffer[buff->position], sizeof(*s));
	buff->position += sizeof(*s);

	*s = byteSwapShort(buff, *s);

	return 1;
}

int BbGetInt(PBYTE_BUFFER buff, int *i) {
	if (buff->position + sizeof(*i) > buff->length) {
		return 0;
	}

	memcpy(i, &buff->buffer[buff->position], sizeof(*i));
	buff->position += sizeof(*i);

	*i = byteSwapInt(buff, *i);

	return 1;
}

int BbPutInt(PBYTE_BUFFER buff, int i) {
	if (buff->position + sizeof(i) > buff->length) {
		return 0;
	}

	i = byteSwapInt(buff, i);

	memcpy(&buff->buffer[buff->position], &i, sizeof(i));
	buff->position += sizeof(i);

	return 1;
}

int BbPutShort(PBYTE_BUFFER buff, short s) {
	if (buff->position + sizeof(s) > buff->length) {
		return 0;
	}

	s = byteSwapShort(buff, s);

	memcpy(&buff->buffer[buff->position], &s, sizeof(s));
	buff->position += sizeof(s);

	return 1;
}

int BbPut(PBYTE_BUFFER buff, char c) {
	if (buff->position + sizeof(c) > buff->length) {
		return 0;
	}

	memcpy(&buff->buffer[buff->position], &c, sizeof(c));
	buff->position += sizeof(c);
	
	return 1;
}
