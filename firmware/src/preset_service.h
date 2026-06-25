#pragma once

#include <Arduino.h>
#include <WiFi.h>

void handle_list_presets(WiFiClient &client);
void handle_preset_save(WiFiClient &client, const String &message);
void handle_preset_delete(WiFiClient &client, const String &message);
void handle_preset_get(WiFiClient &client, const String &message);
