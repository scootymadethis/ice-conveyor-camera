#pragma once

#include <Arduino.h>
#include <WiFi.h>

bool read_client_line(WiFiClient &client, String &out, unsigned long timeout_ms);

bool send_all(
    WiFiClient &client,
    const uint8_t *data,
    size_t len,
    unsigned long stall_timeout_ms,
    unsigned long total_timeout_ms);

bool send_line(WiFiClient &client, const String &line, unsigned long timeout_ms);
bool send_line(WiFiClient &client, const char *line, unsigned long timeout_ms);
bool send_line_default(WiFiClient &client, const String &line);
bool send_line_default(WiFiClient &client, const char *line);
