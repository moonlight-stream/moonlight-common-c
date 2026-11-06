#ifndef NATIVE_MIC_NEGOTIATION_H
#define NATIVE_MIC_NEGOTIATION_H

#include <stddef.h>
#include <string.h>

// ABSENT permits a caller-controlled fallback; INVALID must disable negotiation.
typedef enum _MIC_PORT_RESULT {
    MIC_PORT_INVALID = -1,
    MIC_PORT_ABSENT = 0,
    MIC_PORT_VALID = 1
} MIC_PORT_RESULT;

// Buffers are bounded spans, not necessarily NUL-terminated. NULL is valid only
// with zero length. Outputs are cleared on every call, including errors.

// Pass exactly one Transport header value. Comma-separated alternatives and
// duplicate server_port parameters are ambiguous and rejected. Callers must
// independently reject duplicate Transport headers in the RTSP option list.


static inline int whitespace(char c) {
    return c == ' ' || c == '\t';
}

static inline void trim(const char** start, const char** end) {
    while (*start < *end && whitespace(**start)) (*start)++;
    while (*end > *start && whitespace((*end)[-1])) (*end)--;
}

static inline int decimalPort(const char** current, const char* end, unsigned short* port) {
    unsigned value = 0;
    const char* p = *current;
    if (p == end || *p < '0' || *p > '9') return 0;
    do {
        unsigned digit = (unsigned)(*p - '0');
        if (value > (65535U - digit) / 10U) return 0;
        value = value * 10U + digit;
        p++;
    } while (p < end && *p >= '0' && *p <= '9');
    if (value == 0) return 0;
    *current = p;
    *port = (unsigned short)value;
    return 1;
}

static inline int equalAsciiInsensitive(const char* p, size_t length, const char* key) {
    if (length != strlen(key)) return 0;
    for (size_t i = 0; i < length; i++) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        if (c != key[i]) return 0;
    }
    return 1;
}

static inline MIC_PORT_RESULT parseMicSdpPort(const char* data, size_t length, unsigned short* port) {
    const char key[] = "a=x-nv-general.clientPorts.mic:";
    unsigned short parsed = 0;
    int seen = 0;
    if (port == NULL) return MIC_PORT_INVALID;
    *port = 0;
    if (data == NULL) return length == 0 ? MIC_PORT_ABSENT : MIC_PORT_INVALID;
    // Reject embedded NULs instead of silently ignoring a bounded suffix.
    if (memchr(data, '\0', length) != NULL) return MIC_PORT_INVALID;
    size_t offset = 0;
    while (offset < length) {
        size_t lineStart = offset;
        while (offset < length && data[offset] != '\r' && data[offset] != '\n') offset++;
        const char* start = data + lineStart;
        const char* end = data + offset;
        if ((size_t)(end - start) >= sizeof(key) - 1 &&
                memcmp(start, key, sizeof(key) - 1) == 0) {
            if (seen) return MIC_PORT_INVALID;
            seen = 1;
            start += sizeof(key) - 1;
            trim(&start, &end);
            if (!decimalPort(&start, end, &parsed) || start != end) return MIC_PORT_INVALID;
        }
        if (offset < length && data[offset] == '\r') {
            offset++;
            if (offset == length || data[offset] != '\n') return MIC_PORT_INVALID;
        }
        if (offset < length) offset++;
    }
    if (!seen) return MIC_PORT_ABSENT;
    *port = parsed;
    return MIC_PORT_VALID;
}

static inline MIC_PORT_RESULT parseMicTransportPort(const char* data, size_t length, unsigned short* port) {
    unsigned short parsed = 0;
    int seen = 0;
    if (port == NULL) return MIC_PORT_INVALID;
    *port = 0;
    if (data == NULL) return length == 0 ? MIC_PORT_ABSENT : MIC_PORT_INVALID;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];
        if ((c < 0x20 && c != '\t') || c > 0x7e || c == ',') return MIC_PORT_INVALID;
    }
    size_t offset = 0;
    while (offset < length) {
        size_t tokenStart = offset;
        while (offset < length && data[offset] != ';') offset++;
        const char* start = data + tokenStart;
        const char* end = data + offset;
        trim(&start, &end);
        if (start == end) return MIC_PORT_INVALID;
        const char* equals = start;
        while (equals < end && *equals != '=') equals++;
        const char* keyEnd = equals;
        while (keyEnd > start && whitespace(keyEnd[-1])) keyEnd--;
        if (equalAsciiInsensitive(start, (size_t)(keyEnd - start), "server_port")) {
            if (seen || equals == end) return MIC_PORT_INVALID;
            seen = 1;
            start = equals + 1;
            trim(&start, &end);
            if (!decimalPort(&start, end, &parsed)) return MIC_PORT_INVALID;
            if (start < end && *start == '-') {
                unsigned short last;
                start++;
                if (!decimalPort(&start, end, &last) || last < parsed) return MIC_PORT_INVALID;
            }
            if (start != end) return MIC_PORT_INVALID;
        }
        if (offset < length && ++offset == length) return MIC_PORT_INVALID;
    }
    if (!seen) return MIC_PORT_ABSENT;
    *port = parsed;
    return MIC_PORT_VALID;
}

#endif
