#pragma once

#include <Arduino.h>
#include <WiFi.h>

void handle_client_message(WiFiClient &client, const String &message);
