#pragma once

#include <Arduino.h>
#include <WiFi.h>

bool capture_is_busy();
bool capture_is_lidar_busy();
bool capture_and_send(WiFiClient &client, const char *reason, uint16_t lidarDistanceMm = 0);
bool capture_lidar_request_if_ready(WiFiClient &client);
void handle_camera_capture_command(WiFiClient &client);
void handle_camera_config_command(WiFiClient &client, const String &message);
void handle_ae_level_command(WiFiClient &client, const String &message);
