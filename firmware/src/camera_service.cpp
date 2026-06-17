#include <Arduino.h>
#include <string.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "camera_service.h"
#include "camera_pins.h"
#include "app_config.h"
#include "file_utils.h"

const char *FILE_PATH = "/config/camera.json";

bool save_camera_config(
    framesize_t fs,
    int jpeg_quality,
    int ae,
    int contrast,
    int saturation,
    int brightness)
{
    JsonDocument doc;

    doc["framesize"] = camera_framesize_to_string(fs);
    doc["quality"] = jpeg_quality;
    doc["exposure"] = ae;
    doc["contrast"] = contrast;
    doc["saturation"] = saturation;
    doc["brightness"] = brightness;

    String json;
    serializeJson(doc, json);

    bool ok = FileUtils::writeText(
        LittleFS,
        FILE_PATH,
        json);

    JsonDocument logDoc;
    logDoc["component"] = "config";
    logDoc["event"] = ok ? "camera_config_saved" : "camera_config_save_failed";
    logDoc["path"] = FILE_PATH;

    if (ok)
    {
        logDoc["config"].set(doc.as<JsonObject>());
    }

    String logJson;
    serializeJson(logDoc, logJson);
    Serial.println(logJson);

    return ok;
}

bool load_camera_config(
    framesize_t &fs,
    int &jpeg_quality,
    int &ae,
    int &contrast,
    int &saturation,
    int &brightness)
{
    String json;

    bool ok = FileUtils::readText(LittleFS, FILE_PATH, json);

    if (!ok)
    {
        JsonDocument logDoc;
        logDoc["component"] = "config";
        logDoc["event"] = "camera_config_read_failed";
        logDoc["path"] = FILE_PATH;

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);

        return false;
    }

    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, json);

    if (error)
    {
        JsonDocument logDoc;
        logDoc["component"] = "config";
        logDoc["event"] = "camera_config_parse_failed";
        logDoc["path"] = FILE_PATH;
        logDoc["error"] = error.c_str();

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);

        return false;
    }

    const char *framesizeName = doc["framesize"] | DEFAULT_FRAMESIZE;

    fs = camera_framesize_from_string(framesizeName);
    jpeg_quality = doc["quality"] | DEFAULT_JPEG_QUALITY;
    ae = doc["exposure"] | 0;
    contrast = doc["contrast"] | 0;
    saturation = doc["saturation"] | 0;
    brightness = doc["brightness"] | 0;

    if (jpeg_quality < 4 || jpeg_quality > 63)
        jpeg_quality = DEFAULT_JPEG_QUALITY;

    if (ae < -2 || ae > 2)
        ae = 0;

    if (contrast < -2 || contrast > 2)
        contrast = 0;

    if (saturation < -2 || saturation > 2)
        saturation = 0;

    if (brightness < -2 || brightness > 2)
        brightness = 0;

    JsonDocument logDoc;
    logDoc["component"] = "config";
    logDoc["event"] = "camera_config_loaded";
    logDoc["path"] = FILE_PATH;
    logDoc["config"].set(doc.as<JsonObject>());

    String logJson;
    serializeJson(logDoc, logJson);
    Serial.println(logJson);

    return true;
}

// In base alla qualità selezionata si prende il valore della risoluzione dalla variabile
framesize_t camera_framesize_from_string(const char *name)
{
    if (!name)
        return FRAMESIZE_QVGA;
    struct
    {
        const char *n;
        framesize_t fs;
    } table[] = {
        {"QQVGA", FRAMESIZE_QQVGA},
        {"QVGA", FRAMESIZE_QVGA},
        {"CIF", FRAMESIZE_CIF},
        {"VGA", FRAMESIZE_VGA},
        {"SVGA", FRAMESIZE_SVGA},
        {"XGA", FRAMESIZE_XGA},
        {"HD", FRAMESIZE_HD},
        {"SXGA", FRAMESIZE_SXGA},
        {"UXGA", FRAMESIZE_UXGA},
    };
    for (auto &e : table)
    {
        if (strcasecmp(name, e.n) == 0)
            return e.fs;
    }
    Serial.printf("[camera] unknown framesize '%s', using QVGA\n", name);
    return FRAMESIZE_QVGA;
}

const char *camera_framesize_to_string(framesize_t fs)
{
    switch (fs)
    {
    case FRAMESIZE_QQVGA:
        return "QQVGA";
    case FRAMESIZE_QVGA:
        return "QVGA";
    case FRAMESIZE_CIF:
        return "CIF";
    case FRAMESIZE_VGA:
        return "VGA";
    case FRAMESIZE_SVGA:
        return "SVGA";
    case FRAMESIZE_XGA:
        return "XGA";
    case FRAMESIZE_HD:
        return "HD";
    case FRAMESIZE_SXGA:
        return "SXGA";
    case FRAMESIZE_UXGA:
        return "UXGA";
    default:
        return "UNKNOWN";
    }
}

bool camera_init()
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = CAMERA_XCLK_FREQ_HZ;
    config.frame_size = camera_framesize_from_string(DEFAULT_FRAMESIZE);
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = DEFAULT_JPEG_QUALITY;
    config.fb_count = CAMERA_FB_COUNT;

    if (psramFound())
    {
        Serial.println("[camera] PSRAM found: yes");
    }
    else
    {
        // Without PSRAM we cannot hold multiple JPEG buffers; degrade safely.
        Serial.println("[camera] PSRAM found: no — falling back to DRAM, fb_count=1");
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.fb_count = 1;
        config.frame_size = FRAMESIZE_QVGA;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("[camera] init failed: 0x%x\n", err);
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();

    if (sensor)
    {
        sensor->set_quality(sensor, DEFAULT_JPEG_QUALITY);

        // Colori / rumore / pixel difettosi
        sensor->set_whitebal(sensor, 1);
        sensor->set_awb_gain(sensor, 1);
        sensor->set_wb_mode(sensor, 0); // auto
        sensor->set_bpc(sensor, 1);     // bad pixel correction
        sensor->set_wpc(sensor, 1);     // white pixel correction
        sensor->set_lenc(sensor, 1);    // lens correction
        sensor->set_raw_gma(sensor, 1); // gamma correction
        sensor->set_dcw(sensor, 1);

        framesize_t savedFs;
        int savedQuality;
        int savedAe;
        int savedContrast;
        int savedSaturation;
        int savedBrightness;

        bool configOk = load_camera_config(
            savedFs,
            savedQuality,
            savedAe,
            savedContrast,
            savedSaturation,
            savedBrightness);

        if (configOk)
        {
            Serial.println("[camera] applying saved camera config");

            sensor->set_framesize(sensor, savedFs);
            delay(150);

            sensor->set_quality(sensor, savedQuality);
            delay(150);

            sensor->set_exposure_ctrl(sensor, 1);
            sensor->set_gain_ctrl(sensor, 1);
            sensor->set_ae_level(sensor, savedAe);

            sensor->set_contrast(sensor, savedContrast);
            delay(100);

            sensor->set_saturation(sensor, savedSaturation);
            delay(100);

            sensor->set_brightness(sensor, savedBrightness);
            delay(100);
        }
        else
        {
            Serial.println("[camera] no valid saved config, using defaults");

            sensor->set_quality(sensor, DEFAULT_JPEG_QUALITY);
            sensor->set_brightness(sensor, 0);
            sensor->set_contrast(sensor, 0);
            sensor->set_saturation(sensor, 0);
            sensor->set_ae_level(sensor, 0);
        }

        // Neutro
        sensor->set_special_effect(sensor, 0);

        // Auto esposizione/gain
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_ae_level(sensor, 0);

        Serial.println("[camera] sensor tuning applied");
    }

    Serial.println("[camera] init OK");
    return true;
}

// cattura un frame e restituisce un camera_fb_t
camera_fb_t *camera_capture()
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("[camera] capture failed (fb == null)");
    }
    return fb;
}

void camera_release(camera_fb_t *fb)
{
    if (fb)
        esp_camera_fb_return(fb);
}

bool camera_set_basic_settings(const char *framesize_name, int jpeg_quality, int ae, int contrast, int saturation, int brightness)
{
    sensor_t *sensor = esp_camera_sensor_get();

    if (!sensor)
    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = "camera_settings_apply_failed";
        logDoc["error"] = "sensor_handle_unavailable";

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);

        return false;
    }

    if (jpeg_quality < 4 || jpeg_quality > 63)
    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = "camera_settings_validation_failed";
        logDoc["field"] = "quality";
        logDoc["value"] = jpeg_quality;
        logDoc["min"] = 4;
        logDoc["max"] = 63;

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);

        return false;
    }

    if (ae < -2 || ae > 2)
    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = "camera_settings_validation_failed";
        logDoc["field"] = "exposure";
        logDoc["value"] = ae;
        logDoc["min"] = -2;
        logDoc["max"] = 2;

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);

        return false;
    }

    if (contrast < -2 || contrast > 2)
    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = "camera_settings_validation_failed";
        logDoc["field"] = "contrast";
        logDoc["value"] = contrast;
        logDoc["min"] = -2;
        logDoc["max"] = 2;

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);

        return false;
    }

    if (saturation < -2 || saturation > 2)
    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = "camera_settings_validation_failed";
        logDoc["field"] = "saturation";
        logDoc["value"] = saturation;
        logDoc["min"] = -2;
        logDoc["max"] = 2;

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);

        return false;
    }

    if (brightness < -2 || brightness > 2)
    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = "camera_settings_validation_failed";
        logDoc["field"] = "brightness";
        logDoc["value"] = brightness;
        logDoc["min"] = -2;
        logDoc["max"] = 2;

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);

        return false;
    }

    framesize_t fs = camera_framesize_from_string(framesize_name);

    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = "camera_settings_apply_requested";
        logDoc["config"]["framesize"] = camera_framesize_to_string(fs);
        logDoc["config"]["quality"] = jpeg_quality;
        logDoc["config"]["exposure"] = ae;
        logDoc["config"]["contrast"] = contrast;
        logDoc["config"]["saturation"] = saturation;
        logDoc["config"]["brightness"] = brightness;

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);
    }

    int r1 = sensor->set_framesize(sensor, fs);
    delay(150);

    int r2 = sensor->set_quality(sensor, jpeg_quality);
    delay(150);

    // mi assicuro che l'auto-esposizione sia abilitata prima di settare il livello
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_gain_ctrl(sensor, 1);

    int r3 = sensor->set_ae_level(sensor, ae);

    int r4 = sensor->set_contrast(sensor, contrast);
    delay(150);

    int r5 = sensor->set_saturation(sensor, saturation);
    delay(150);

    int r6 = sensor->set_brightness(sensor, brightness);
    delay(150);

    // Manteniamo i fix colore/pixel attivi
    sensor->set_whitebal(sensor, 1);
    sensor->set_awb_gain(sensor, 1);
    sensor->set_wb_mode(sensor, 0);
    sensor->set_bpc(sensor, 1);
    sensor->set_wpc(sensor, 1);
    sensor->set_lenc(sensor, 1);
    sensor->set_raw_gma(sensor, 1);
    sensor->set_dcw(sensor, 1);

    // Scarta alcuni frame vecchi dopo cambio risoluzione/qualità
    for (int i = 0; i < 3; i++)
    {
        camera_fb_t *fb = esp_camera_fb_get();

        if (fb)
        {
            esp_camera_fb_return(fb);
        }

        delay(80);
    }

    if (r1 != 0 || r2 != 0 || r3 != 0 || r4 != 0 || r5 != 0 || r6 != 0)
    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = "camera_settings_apply_failed";
        logDoc["result"]["framesize"] = r1;
        logDoc["result"]["quality"] = r2;
        logDoc["result"]["exposure"] = r3;
        logDoc["result"]["contrast"] = r4;
        logDoc["result"]["saturation"] = r5;
        logDoc["result"]["brightness"] = r6;

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);

        return false;
    }

    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = "camera_settings_applied";

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);
    }

    bool saveOk = save_camera_config(
        fs,
        jpeg_quality,
        ae,
        contrast,
        saturation,
        brightness);

    {
        JsonDocument logDoc;
        logDoc["component"] = "camera";
        logDoc["event"] = saveOk ? "camera_settings_saved" : "camera_settings_save_failed";
        logDoc["path"] = FILE_PATH;

        String logJson;
        serializeJson(logDoc, logJson);
        Serial.println(logJson);
    }

    return true;
}
bool camera_set_ae_level(int ae_level)
{
    if (ae_level < -2 || ae_level > 2)
    {
        Serial.printf("[camera] invalid ae_level: %d (must be -2..+2)\n", ae_level);
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();

    if (!sensor)
    {
        Serial.println("[camera] sensor handle unavailable");
        return false;
    }

    // mi assicuro che l'auto-esposizione sia abilitata prima di settare il livello
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_gain_ctrl(sensor, 1);

    int r = sensor->set_ae_level(sensor, ae_level);

    Serial.printf("[camera] ae_level set to %d (result=%d)\n", ae_level, r);

    return r == 0;
}