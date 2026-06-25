#pragma once

#include "esp_camera.h"
#include <ArduinoJson.h>

framesize_t camera_framesize_from_string(const char *name);
const char *camera_framesize_to_string(framesize_t fs);

bool camera_init();
camera_fb_t *camera_capture();
void camera_release(camera_fb_t *fb);

bool camera_set_basic_settings(const char *framesize_name, int jpeg_quality, int ae, int contrast, int saturation, int brightness);
bool camera_set_ae_level(int ae_level);
void camera_fill_config_json(JsonObject obj);
