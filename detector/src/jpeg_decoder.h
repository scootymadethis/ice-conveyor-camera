#pragma once

#include <Arduino.h>

bool frame_is_jpeg(const uint8_t *buffer, uint32_t length);
uint8_t *jpeg_to_rgb888(const uint8_t *jpeg, uint32_t length, int *out_w, int *out_h);
