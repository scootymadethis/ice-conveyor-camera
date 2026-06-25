#pragma once

#include <Arduino.h>

struct ReceivedFrame
{
    const uint8_t *data;
    uint32_t length;
    uint32_t frame_id;
};

bool espnow_receiver_begin();
bool espnow_frame_available();
ReceivedFrame espnow_current_frame();
void espnow_release_frame();
