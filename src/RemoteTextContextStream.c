// Adapted from qiin2333/moonlight-common-c, commit 31a2a458.
#include "RemoteTextContextStream.h"

#include <string.h>

#define REMOTE_TEXT_CONTEXT_WIRE_SIZE 76

static uint16_t readLe16(const uint8_t* value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t readLe32(const uint8_t* value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static uint64_t readLe64(const uint8_t* value) {
    return (uint64_t)readLe32(value) | ((uint64_t)readLe32(value + 4) << 32);
}

bool decodeRemoteTextContextPacket(const uint8_t* payload, size_t payloadLength,
                                   PLI_REMOTE_TEXT_CONTEXT context) {
    if (context == NULL) {
        return false;
    }
    memset(context, 0, sizeof(*context));
    if (payload == NULL || payloadLength != REMOTE_TEXT_CONTEXT_WIRE_SIZE ||
            payload[0] != 1 || payload[1] != REMOTE_TEXT_CONTEXT_WIRE_SIZE ||
            payload[26] != 0 || payload[27] != 0) {
        return false;
    }

    context->flags = readLe16(payload + 2);
    context->revision = readLe32(payload + 4);
    context->activationId = readLe64(payload + 8);
    context->inputToken = readLe64(payload + 16);
    context->source = payload[24];
    context->cause = payload[25];
    context->anchorX = (int32_t)readLe32(payload + 28);
    context->anchorY = (int32_t)readLe32(payload + 32);
    context->elementLeft = (int32_t)readLe32(payload + 36);
    context->elementTop = (int32_t)readLe32(payload + 40);
    context->elementRight = (int32_t)readLe32(payload + 44);
    context->elementBottom = (int32_t)readLe32(payload + 48);
    context->caretLeft = (int32_t)readLe32(payload + 52);
    context->caretTop = (int32_t)readLe32(payload + 56);
    context->caretRight = (int32_t)readLe32(payload + 60);
    context->caretBottom = (int32_t)readLe32(payload + 64);
    context->captureWidth = readLe32(payload + 68);
    context->captureHeight = readLe32(payload + 72);

    // Keep the transport decoder limited to wire-safety checks. Whether an
    // observation is trusted enough to activate an IME is client policy and
    // may evolve independently from this packet format.
    return context->captureWidth != 0 && context->captureHeight != 0 &&
           context->captureWidth <= 1000000 && context->captureHeight <= 1000000;
}
