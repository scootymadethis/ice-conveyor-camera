#include "camera_service.h"
#include "camera_pins.h"
#include "app_config.h"
#include <Arduino.h>
#include <string.h>

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

        // Neutro
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
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
        Serial.println("[camera] sensor handle unavailable");
        return false;
    }

    if (jpeg_quality < 4 || jpeg_quality > 63)
    {
        Serial.printf("[camera] invalid jpeg quality: %d\n", jpeg_quality);
        return false;
    }

    if (ae < -2 || ae > 2)
    {
        Serial.printf("[camera] invalid exposure: %d (must be -2..+2)\n", ae);
        return false;
    }

    if (contrast < -2 || contrast > 2)
    {
        Serial.printf("[camera] invalid contrast: %d (must be -2..+2)\n", contrast);
        return false;
    }

    if (saturation < -2 || saturation > 2)
    {
        Serial.printf("[camera] invalid saturation: %d (must be -2..+2)\n", saturation);
        return false;
    }

    if (brightness < -2 || brightness > 2)
    {
        Serial.printf("[camera] invalid brightness: %d (must be -2..+2)\n", brightness);
        return false;
    }

    framesize_t fs = camera_framesize_from_string(framesize_name);

    Serial.printf(
        "[camera] applying settings: framesize=%s quality=%d exposure=%d contrast=%d saturation=%d brightness=%d\n",
        camera_framesize_to_string(fs),
        jpeg_quality, ae, contrast, saturation, brightness);

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
        Serial.printf("[camera] apply failed: framesize=%d quality=%d exposure=%d contrast=%d saturation=%d brightness=%d\n", r1, r2, r3, r4, r5, r6);
        return false;
    }

    Serial.println("[camera] settings applied OK");
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