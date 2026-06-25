#include "preset_service.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string.h>

#include "file_utils.h"

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

static bool save_preset_to_storage(
    const String &name,
    const char *framesize,
    int quality,
    int ae,
    int contrast,
    int saturation,
    int brightness)
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

void handle_preset_save(WiFiClient &client, const String &message)
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
    name.toLowerCase();

    const char *framesize = doc["framesize"] | "HD";
    int quality = doc["quality"] | 10;
    int ae = doc["ae"] | 0;
    int contrast = doc["contrast"] | 0;
    int saturation = doc["saturation"] | 0;
    int brightness = doc["brightness"] | 0;

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

void handle_preset_delete(WiFiClient &client, const String &message)
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

void handle_preset_get(WiFiClient &client, const String &message)
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

void handle_list_presets(WiFiClient &client)
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
