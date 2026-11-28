#pragma once

#include <stddef.h>

#include "Limelight.h"

bool decodeRemoteTextContextPacket(const uint8_t* payload, size_t payloadLength,
                                   PLI_REMOTE_TEXT_CONTEXT context);
