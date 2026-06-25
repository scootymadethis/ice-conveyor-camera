#pragma once

#include <Arduino.h>
#include "detector_config.h"

struct __attribute__((packed)) EspNowFrameChunk
{
    uint32_t magic;
    uint32_t frameId;
    uint32_t totalLen;
    uint16_t chunkIndex;
    uint16_t chunkCount;
    uint16_t payloadLen;
    uint8_t payload[ESPNOW_FRAME_CHUNK_BYTES];
};
