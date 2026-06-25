#pragma once

#include <Arduino.h>
#include <WiFi.h>

void espnow_store_last_frame(const uint8_t *data, size_t len);
void handle_espnow_send_last(WiFiClient &client);
