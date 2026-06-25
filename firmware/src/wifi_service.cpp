#include "wifi_service.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#include "app_config.h"
#include "app_log.h"

static char s_ip[20] = "0.0.0.0";

static void on_wifi_event(WiFiEvent_t event)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        log_warn("wifi", "station disconnected from access point");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        strncpy(s_ip, WiFi.localIP().toString().c_str(), sizeof(s_ip) - 1);
        s_ip[sizeof(s_ip) - 1] = '\0';
        log_infof("wifi", "got ip=%s rssi=%d dBm", s_ip, WiFi.RSSI());
        break;
    default:
        break;
    }
}

bool wifi_connect(unsigned long timeout_ms)
{
    static bool eventRegistered = false;
    if (!eventRegistered)
    {
        WiFi.onEvent(on_wifi_event);
        eventRegistered = true;
    }

    log_infof("wifi", "connecting ssid='%s' timeout_ms=%lu", WIFI_SSID, timeout_ms);
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms)
    {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        log_warn("wifi", "connection timed out");
        return false;
    }

    return true;
}

bool wifi_is_connected()
{
    return WiFi.status() == WL_CONNECTED;
}

bool wifi_ensure_connected()
{
    if (wifi_is_connected())
    {
        return true;
    }

    log_warn("wifi", "link lost; reconnecting now");
    return wifi_connect(WIFI_CONNECT_TIMEOUT_MS);
}

const char *wifi_ip()
{
    return s_ip;
}

int wifi_rssi()
{
    return wifi_is_connected() ? WiFi.RSSI() : 0;
}
