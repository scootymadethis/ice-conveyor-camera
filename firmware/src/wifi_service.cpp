#include "wifi_service.h"
#include "app_config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

static char s_ip[20] = "0.0.0.0";

static void _on_wifi_event(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[wifi] disconnected");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            strncpy(s_ip, WiFi.localIP().toString().c_str(), sizeof(s_ip) - 1);
            s_ip[sizeof(s_ip) - 1] = '\0';
            Serial.printf("[wifi] IP: %s  RSSI: %d dBm\n", s_ip, WiFi.RSSI());
            break;
        default:
            break;
    }
}

bool wifi_connect(unsigned long timeout_ms) {
    static bool s_event_registered = false;
    if (!s_event_registered) {
        WiFi.onEvent(_on_wifi_event);
        s_event_registered = true;
    }

    Serial.printf("[wifi] connecting to SSID '%s'...\n", WIFI_SSID);
    WiFi.disconnect(false);     // reset stale stack state before (re)connecting
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // keep latency low for frame uploads
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] connect FAILED");
        return false;
    }
    // IP and RSSI are logged by _on_wifi_event(ARDUINO_EVENT_WIFI_STA_GOT_IP).
    return true;
}

bool wifi_is_connected() { return WiFi.status() == WL_CONNECTED; }

bool wifi_ensure_connected() {
    if (wifi_is_connected()) return true;
    Serial.println("[wifi] link lost, reconnecting...");
    return wifi_connect(WIFI_CONNECT_TIMEOUT_MS);
}

const char *wifi_ip() { return s_ip; }

int wifi_rssi() { return wifi_is_connected() ? WiFi.RSSI() : 0; }
