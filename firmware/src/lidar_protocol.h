#pragma once

#include <Arduino.h>
#include <WiFi.h>

void handle_lidar_status(WiFiClient &client);
void handle_lidar_base(WiFiClient &client, const String &message);
void handle_lidar_base_current(WiFiClient &client);
void handle_lidar_sample_config(WiFiClient &client, const String &message);
void handle_lidar_post_frame_delay(WiFiClient &client, const String &message);
void handle_lidar_enable(WiFiClient &client, const String &message);
