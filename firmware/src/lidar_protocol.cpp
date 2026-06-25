#include "lidar_protocol.h"

#include <ArduinoJson.h>

#include "app_config.h"
#include "app_log.h"
#include "lidar_service.h"

static void send_lidar_error(WiFiClient &client, const char *error)
{
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = error;

    String json;
    serializeJson(doc, json);

    client.print("LIDAR_ERROR ");
    client.println(json);
}

static void send_lidar_status(WiFiClient &client, const char *source, const LidarStatus &status)
{
    JsonDocument doc;
    doc["ok"] = true;
    doc["source"] = source;
    doc["lidar_ok"] = status.lidarOk;
    doc["enabled"] = status.enabled;
    doc["baseline_ready"] = status.baselineReady;
    doc["current_valid"] = status.currentValid;
    doc["threshold_percent"] = status.thresholdPercent;
    doc["threshold_mm"] = status.baselineReady ? status.thresholdMm : 0;
    doc["stable_delta_mm"] = status.stableDeltaMm;
    doc["good_sample_count"] = status.goodSampleCount;
    doc["sample_delay_ms"] = status.sampleDelayMs;
    doc["post_frame_delay_ms"] = status.postFrameDelayMs;
    doc["good_sample_count_min"] = LIDAR_GOOD_SAMPLE_COUNT_MIN;
    doc["good_sample_count_max"] = LIDAR_GOOD_SAMPLE_COUNT_MAX;
    doc["sample_delay_ms_min"] = LIDAR_SAMPLE_DELAY_MS_MIN;
    doc["sample_delay_ms_max"] = LIDAR_SAMPLE_DELAY_MS_MAX;
    doc["post_frame_delay_ms_min"] = LIDAR_POST_FRAME_DELAY_MS_MIN;
    doc["post_frame_delay_ms_max"] = LIDAR_POST_FRAME_DELAY_MS_MAX;
    doc["out_of_range_mm"] = LIDAR_OUT_OF_RANGE_DISTANCE_MM;

    if (status.baselineReady)
    {
        doc["base_mm"] = status.baselineMm;
    }
    else
    {
        doc["base_mm"] = nullptr;
    }

    if (status.currentValid)
    {
        doc["current_mm"] = status.currentDistanceMm;
    }
    else
    {
        doc["current_mm"] = nullptr;
    }

    String json;
    serializeJson(doc, json);

    client.print("LIDAR_OK ");
    client.println(json);
}

void handle_lidar_status(WiFiClient &client)
{
    // Intentionally no Serial log here: the web UI may poll this endpoint often.
    LidarStatus status;
    lidar_get_status(status, true);
    send_lidar_status(client, "status", status);
}

void handle_lidar_base(WiFiClient &client, const String &message)
{
    int baseMm = 0;
    int parsed = sscanf(message.c_str(), "lidar_base %d", &baseMm);

    if (parsed != 1)
    {
        send_lidar_error(client, "invalid format");
        return;
    }

    if (baseMm <= 0 || baseMm > LIDAR_OUT_OF_RANGE_DISTANCE_MM)
    {
        send_lidar_error(client, "base distance out of range");
        return;
    }

    if (!lidar_set_base_distance((uint16_t)baseMm))
    {
        send_lidar_error(client, "failed to set base distance");
        return;
    }

    log_infof("lidar", "manual baseline set base_mm=%d", baseMm);
    LidarStatus status;
    lidar_get_status(status, false);
    send_lidar_status(client, "manual", status);
}

void handle_lidar_base_current(WiFiClient &client)
{
    uint16_t distanceMm = 0;

    if (!lidar_set_base_to_current(distanceMm))
    {
        send_lidar_error(client, "failed to read current distance");
        return;
    }

    log_infof("lidar", "baseline set from current reading base_mm=%u", distanceMm);
    LidarStatus status;
    lidar_get_status(status, false);
    send_lidar_status(client, "current", status);
}

void handle_lidar_sample_config(WiFiClient &client, const String &message)
{
    int goodSampleCount = 0;
    int sampleDelayMs = 0;
    int parsed = sscanf(message.c_str(), "lidar_sample_config %d %d", &goodSampleCount, &sampleDelayMs);

    if (parsed != 2)
    {
        send_lidar_error(client, "invalid format");
        return;
    }

    if (goodSampleCount < LIDAR_GOOD_SAMPLE_COUNT_MIN ||
        goodSampleCount > LIDAR_GOOD_SAMPLE_COUNT_MAX ||
        sampleDelayMs < LIDAR_SAMPLE_DELAY_MS_MIN ||
        sampleDelayMs > LIDAR_SAMPLE_DELAY_MS_MAX)
    {
        send_lidar_error(client, "sample config out of range");
        return;
    }

    if (!lidar_set_sample_config((uint16_t)goodSampleCount, (uint16_t)sampleDelayMs))
    {
        send_lidar_error(client, "failed to set sample config");
        return;
    }

    log_infof("lidar", "sample config updated good_samples=%d delay_ms=%d", goodSampleCount, sampleDelayMs);
    LidarStatus status;
    lidar_get_status(status, false);
    send_lidar_status(client, "sample_config", status);
}

void handle_lidar_post_frame_delay(WiFiClient &client, const String &message)
{
    int delayMs = 0;
    int parsed = sscanf(message.c_str(), "lidar_post_frame_delay %d", &delayMs);

    if (parsed != 1)
    {
        send_lidar_error(client, "invalid format");
        return;
    }

    if (delayMs < LIDAR_POST_FRAME_DELAY_MS_MIN || delayMs > LIDAR_POST_FRAME_DELAY_MS_MAX)
    {
        send_lidar_error(client, "post-frame delay out of range");
        return;
    }

    if (!lidar_set_post_frame_delay((uint16_t)delayMs))
    {
        send_lidar_error(client, "failed to set post-frame delay");
        return;
    }

    log_infof("lidar", "post-frame delay updated delay_ms=%d", delayMs);
    LidarStatus status;
    lidar_get_status(status, false);
    send_lidar_status(client, "post_frame_delay", status);
}

void handle_lidar_enable(WiFiClient &client, const String &message)
{
    int enabled = 0;
    int parsed = sscanf(message.c_str(), "lidar_enable %d", &enabled);

    if (parsed != 1 || (enabled != 0 && enabled != 1))
    {
        send_lidar_error(client, "invalid format");
        return;
    }

    lidar_set_enabled(enabled == 1);
    log_infof("lidar", "trigger capture %s", enabled == 1 ? "enabled" : "disabled");

    LidarStatus status;
    lidar_get_status(status, false);
    send_lidar_status(client, enabled ? "enabled" : "disabled", status);
}
