#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "wifi_service.h"
#include "app_config.h"
#include "camera_service.h"
#include "file_utils.h"

static const uint16_t SERVER_PORT = 3131;

WiFiServer server(SERVER_PORT);
WiFiClient activeClient;

static const char *PRESETS_DIR = "/presets";

static bool is_valid_preset_name(const String &name)
{
    if (name.length() == 0 || name.length() > 32)
    {
        return false;
    }

    for (int i = 0; i < name.length(); i++)
    {
        char c = name[i];

        bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' ||
            c == '-';

        if (!ok)
        {
            return false;
        }
    }

    return true;
}

static String preset_file_path(const String &name)
{
    return String(PRESETS_DIR) + "/" + name + ".json";
}

static bool is_allowed_framesize(const char *framesize)
{
    if (!framesize)
    {
        return false;
    }

    return strcmp(framesize, "QQVGA") == 0 ||
           strcmp(framesize, "QVGA") == 0 ||
           strcmp(framesize, "CIF") == 0 ||
           strcmp(framesize, "VGA") == 0 ||
           strcmp(framesize, "SVGA") == 0 ||
           strcmp(framesize, "XGA") == 0 ||
           strcmp(framesize, "HD") == 0 ||
           strcmp(framesize, "SXGA") == 0 ||
           strcmp(framesize, "UXGA") == 0;
}

static void send_preset_error(WiFiClient &client, const char *error)
{
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = error;

    String json;
    serializeJson(doc, json);

    client.print("PRESET_ERROR ");
    client.println(json);
}

static void send_preset_json(WiFiClient &client, const char *prefix, JsonDocument &doc)
{
    String json;
    serializeJson(doc, json);

    client.print(prefix);
    client.print(" ");
    client.println(json);
}

static bool save_preset_to_storage(const String &name, const char *framesize, int quality, int ae, int contrast, int saturation, int brightness)
{
    if (!FileUtils::ensureDirectory(LittleFS, PRESETS_DIR))
    {
        return false;
    }

    String path = preset_file_path(name);

    JsonDocument doc;
    doc["name"] = name;
    doc["framesize"] = framesize;
    doc["quality"] = quality;
    doc["ae"] = ae;
    doc["contrast"] = contrast;
    doc["saturation"] = saturation;
    doc["brightness"] = brightness;

    String json;
    serializeJson(doc, json);

    return FileUtils::writeText(LittleFS, path.c_str(), json);
}

static void handle_preset_save(WiFiClient &client, const String &message)
{
    String payload = message.substring(strlen("preset_save "));
    payload.trim();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
        send_preset_error(client, "invalid json");
        return;
    }

    String name = String(doc["name"] | "");
    name.trim();

    const char *framesize = doc["framesize"] | "HD";
    int quality = doc["quality"] | 10;
    int ae = doc["ae"] | 0;
    int contrast = doc["contrast"] | 0;
    int saturation = doc["saturation"] | 0;
    int brightness = doc["brightness"] | 0;

    name.toLowerCase();

    if (!is_valid_preset_name(name))
    {
        send_preset_error(client, "invalid preset name");
        return;
    }

    if (!is_allowed_framesize(framesize))
    {
        send_preset_error(client, "invalid framesize");
        return;
    }

    if (quality < 4 || quality > 63)
    {
        send_preset_error(client, "quality must be between 4 and 63");
        return;
    }

    if (ae < -2 || ae > 2)
    {
        send_preset_error(client, "ae must be between -2 and 2");
        return;
    }

    if (contrast < -2 || contrast > 2)
    {
        send_preset_error(client, "contrast must be between -2 and 2");
        return;
    }

    if (saturation < -2 || saturation > 2)
    {
        send_preset_error(client, "saturation must be between -2 and 2");
        return;
    }

    if (brightness < -2 || brightness > 2)
    {
        send_preset_error(client, "brightness must be between -2 and 2");
        return;
    }

    bool ok = save_preset_to_storage(
        name,
        framesize,
        quality,
        ae,
        contrast,
        saturation,
        brightness);

    if (!ok)
    {
        send_preset_error(client, "failed to save preset");
        return;
    }

    JsonDocument response;
    response["ok"] = true;
    response["name"] = name;
    response["path"] = preset_file_path(name);
    response["framesize"] = framesize;
    response["quality"] = quality;
    response["ae"] = ae;
    response["contrast"] = contrast;
    response["saturation"] = saturation;
    response["brightness"] = brightness;

    send_preset_json(client, "PRESET_SAVE_OK", response);
}

static void handle_preset_delete(WiFiClient &client, const String &message)
{
    String name = message.substring(strlen("preset_delete "));
    name.trim();
    name.toLowerCase();

    if (!is_valid_preset_name(name))
    {
        send_preset_error(client, "invalid preset name");
        return;
    }

    String path = preset_file_path(name);

    if (!LittleFS.exists(path))
    {
        send_preset_error(client, "preset not found");
        return;
    }

    bool ok = FileUtils::removeFile(LittleFS, path.c_str());

    if (!ok)
    {
        send_preset_error(client, "failed to delete preset");
        return;
    }

    JsonDocument response;
    response["ok"] = true;
    response["name"] = name;
    response["deleted"] = true;
    response["path"] = path;

    send_preset_json(client, "PRESET_DELETE_OK", response);
}

static void handle_preset_get(WiFiClient &client, const String &message)
{
    String name = message.substring(strlen("preset_get "));
    name.trim();
    name.toLowerCase();

    if (!is_valid_preset_name(name))
    {
        send_preset_error(client, "invalid preset name");
        return;
    }

    String path = preset_file_path(name);

    if (!LittleFS.exists(path))
    {
        send_preset_error(client, "preset not found");
        return;
    }

    String json;

    if (!FileUtils::readText(LittleFS, path.c_str(), json))
    {
        send_preset_error(client, "failed to read preset");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error)
    {
        send_preset_error(client, "stored preset json is invalid");
        return;
    }

    doc["ok"] = true;

    send_preset_json(client, "PRESET_GET_OK", doc);
}

static void handle_list_presets(WiFiClient &client)
{
    if (!LittleFS.exists(PRESETS_DIR))
    {
        FileUtils::ensureDirectory(LittleFS, PRESETS_DIR);
    }

    File root = LittleFS.open(PRESETS_DIR, FILE_READ);

    if (!root || !root.isDirectory())
    {
        send_preset_error(client, "failed to open presets directory");
        return;
    }

    JsonDocument response;
    JsonArray presets = response.to<JsonArray>();

    File file = root.openNextFile();

    while (file)
    {
        if (!file.isDirectory())
        {
            String path = String(file.name());

            if (!path.startsWith("/"))
            {
                path = String(PRESETS_DIR) + "/" + path;
            }

            if (path.endsWith(".json"))
            {
                String json;

                file.close();

                if (FileUtils::readText(LittleFS, path.c_str(), json))
                {
                    JsonDocument presetDoc;
                    DeserializationError error = deserializeJson(presetDoc, json);

                    if (!error)
                    {
                        JsonObject item = presets.add<JsonObject>();

                        item["name"] = presetDoc["name"] | "";
                        item["framesize"] = presetDoc["framesize"] | "";
                        item["quality"] = presetDoc["quality"] | 10;
                        item["ae"] = presetDoc["ae"] | 0;
                        item["contrast"] = presetDoc["contrast"] | 0;
                        item["saturation"] = presetDoc["saturation"] | 0;
                        item["brightness"] = presetDoc["brightness"] | 0;
                    }
                }
            }
            else
            {
                file.close();
            }
        }
        else
        {
            file.close();
        }

        file = root.openNextFile();
    }

    root.close();

    send_preset_json(client, "PRESETS_OK", response);
}

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

        size_t remaining = len - total_sent;
        size_t chunk_len = remaining > TCP_SEND_CHUNK_BYTES ? TCP_SEND_CHUNK_BYTES : remaining;
        size_t sent = client.write(data + total_sent, chunk_len);

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

            delay(0);
        }
    }

    return true;
}

void setup()
{
    Serial.begin(9600);
    delay(300);

    Serial.println("[main] boot");

    if (!LittleFS.begin(true))
    {
        Serial.println("[main] Errore mount LittleFS");
        return;
    }

    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_TX_POWER);

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

        // ... Inside your loop(), replacing the image capture and send logic:

        if (message == "list_presets")
        {
            Serial.println("[main] preset list request");
            handle_list_presets(activeClient);
        }
        else if (message.startsWith("preset_save "))
        {
            Serial.print("[main] preset save request: ");
            Serial.println(message);
            handle_preset_save(activeClient, message);
        }
        else if (message.startsWith("preset_delete "))
        {
            Serial.print("[main] preset delete request: ");
            Serial.println(message);
            handle_preset_delete(activeClient, message);
        }
        else if (message.startsWith("preset_get "))
        {
            Serial.print("[main] preset get request: ");
            Serial.println(message);
            handle_preset_get(activeClient, message);
        }
        else if (message == "camera")
        {
            Serial.println("[main] Frame richiesto.");

            // Scarta al massimo un frame vecchio: più reattivo ma ancora stabile.
            for (int i = 0; i < DISCARD_STALE_FRAMES; i++)
            {
                camera_fb_t *old_fb = camera_capture();
                if (old_fb)
                    camera_release(old_fb);
                delay(5);
            }

            // ── CATTURA con timing ──────────────────────────────────────────
            int64_t t_cap_start = esp_timer_get_time();
            camera_fb_t *fb = camera_capture();
            int64_t t_cap_us = esp_timer_get_time() - t_cap_start;

            if (!fb)
            {
                Serial.println("[camera] capture failed");
                activeClient.println("CAMERA_ERROR");
                return;
            }

            uint32_t frame_len = fb->len;
            uint32_t cap_ms = (uint32_t)(t_cap_us / 1000);

            Serial.printf("[camera] frame len=%u | cap=%u ms\n", frame_len, cap_ms);

            // ── PROGRESSED HEADER FOR THE COMPUTER HUB ─────────────────────
            // We send the capture time to the computer ahead of the image.
            activeClient.printf("FRAME %u CAPTURE_MS %u\n", frame_len, cap_ms);

            // ── INVIO con timing ────────────────────────────────────────────
            int64_t t_send_start = esp_timer_get_time();
            bool ok = send_all(activeClient, fb->buf, fb->len);

            // CRITICAL: Force TCP to flush all buffered bytes over the air
            // before checking the final transmission clock stop!
            if (ok)
            {
                activeClient.flush();
            }

            int64_t t_send_us = esp_timer_get_time() - t_send_start;
            uint32_t send_ms = (uint32_t)(t_send_us / 1000);

            camera_release(fb);

            // ── LOG timing ──────────────────────────────────────────────────
            float kbps = (t_send_us > 0)
                             ? (frame_len * 8.0f) / (t_send_us / 1e6f) / 1000.0f
                             : 0.0f;

            Serial.printf("[timing] capture=%u ms | send=%u ms | size=%u B | %.0f kbit/s\n",
                          cap_ms, send_ms, frame_len, kbps);

            if (!ok)
            {
                Serial.println("[camera] send failed, closing client");
                activeClient.stop();
            }
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