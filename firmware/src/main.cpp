#include <Arduino.h>
#include <WiFi.h>

#include "wifi_service.h"
#include "app_config.h"
#include "camera_service.h"

static const uint16_t SERVER_PORT = 3131;

WiFiServer server(SERVER_PORT);
WiFiClient activeClient;

static bool send_all(WiFiClient &client, const uint8_t *data, size_t len, unsigned long timeout_ms = 5000)
{
    size_t total_sent = 0;
    unsigned long start = millis();

    while (total_sent < len)
    {
        if (!client || !client.connected())
        {
            return false;
        }

        size_t sent = client.write(data + total_sent, len - total_sent);

        if (sent > 0)
        {
            total_sent += sent;
            start = millis();
        }
        else
        {
            if (millis() - start > timeout_ms)
            {
                return false;
            }

            delay(1);
        }
    }

    return true;
}

void setup()
{
    Serial.begin(9600);
    delay(300);

    Serial.println("[main] boot");

    if (!wifi_connect(WIFI_CONNECT_TIMEOUT_MS))
    {
        Serial.println("[main] WiFi initial connection failed");
        return;
    }

    Serial.printf("[main] WiFi connected, ESP32 IP: %s\n", wifi_ip());

    server.begin();
    Serial.printf("[main] TCP server listening on port %u\n", SERVER_PORT);

    if (camera_init())
    {
        Serial.println("[main] Camera avviata.");
    }
}

void loop()
{
    if (!wifi_ensure_connected())
    {
        Serial.println("[main] WiFi not available, retry later");

        if (activeClient)
        {
            activeClient.stop();
        }

        delay(1000);
        return;
    }

    if (!activeClient || !activeClient.connected())
    {
        if (activeClient)
        {
            activeClient.stop();
            Serial.println("[main] old client closed");
        }

        activeClient = server.available();

        if (activeClient)
        {
            Serial.println("[main] client connected");
            activeClient.println("ESP32 connected.");
        }

        delay(10);
        return;
    }

    if (activeClient.available())
    {
        String message = activeClient.readStringUntil('\n');
        message.trim();

        if (message == "camera")
        {
            Serial.println("[main] Frame richiesto.");

            // Scarta frame vecchi dal buffer
            for (int i = 0; i < 2; i++)
            {
                camera_fb_t *old_fb = camera_capture();

                if (old_fb)
                {
                    camera_release(old_fb);
                }

                delay(50);
            }

            camera_fb_t *fb = camera_capture();

            if (!fb)
            {
                Serial.println("[camera] capture failed");
                activeClient.println("CAMERA_ERROR");
                return;
            }

            uint32_t frame_len = fb->len;

            Serial.printf("[camera] frame len=%u\n", frame_len);

            activeClient.printf("FRAME %u\n", frame_len);

            bool ok = send_all(activeClient, fb->buf, fb->len);

            if (ok)
            {
                Serial.printf("[camera] frame sent: %u bytes\n", frame_len);
            }
            else
            {
                Serial.println("[camera] send failed, closing client");
                activeClient.stop();
            }

            camera_release(fb);
        }
        else if (message.startsWith("config "))
        {
            Serial.print("[main] config request: ");
            Serial.println(message);

            char framesize_name[16] = {0};
            int quality = -1;
            int ae = -1;
            int contrast = -1;
            int saturation = -1;
            int brightness = -1;

            int parsed = sscanf(message.c_str(), "config %15s %d %d %d %d %d", framesize_name, &quality, &ae, &contrast, &saturation, &brightness);

            if (parsed != 6)
            {
                activeClient.println("CONFIG_ERROR invalid format");
                return;
            }

            bool ok = camera_set_basic_settings(framesize_name, quality, ae, contrast, saturation, brightness);

            if (ok)
            {
                activeClient.printf("CONFIG_OK %s %d %d %d %d %d\n", framesize_name, quality, ae, contrast, saturation, brightness);
            }
            else
            {
                activeClient.printf("CONFIG_ERROR failed %s %d %d %d %d %d\n", framesize_name, quality, ae, contrast, saturation, brightness);
            }
        }
        else if (message.startsWith("ae_level "))
        {
            Serial.print("[main] ae_level request: ");
            Serial.println(message);

            int ae_level = 0;
            int parsed = sscanf(message.c_str(), "ae_level %d", &ae_level);

            if (parsed != 1)
            {
                activeClient.println("AE_ERROR invalid format");
                return;
            }

            bool ok = camera_set_ae_level(ae_level);

            if (ok)
            {
                activeClient.printf("AE_OK %d\n", ae_level);
            }
            else
            {
                activeClient.printf("AE_ERROR failed %d\n", ae_level);
            }
        }
        else
        {
            activeClient.print("UNKNOWN_COMMAND ");
            activeClient.println(message);
        }
    }

    delay(10);
}