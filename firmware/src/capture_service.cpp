#include "capture_service.h"

#include <esp_timer.h>
#include <string.h>

#include "app_config.h"
#include "app_log.h"
#include "camera_service.h"
#include "espnow_service.h"
#include "lidar_service.h"
#include "tcp_utils.h"

static volatile bool s_captureInProgress = false;
static volatile bool s_lidarCaptureInProgress = false;

struct CaptureStateGuard
{
    bool lidarCapture = false;

    explicit CaptureStateGuard(const char *reason)
    {
        lidarCapture = (reason && strcmp(reason, "lidar") == 0);
        s_captureInProgress = true;
        s_lidarCaptureInProgress = lidarCapture;
    }

    ~CaptureStateGuard()
    {
        if (lidarCapture)
        {
            // Arm the post-frame holdoff before the LiDAR task can resume.
            // This closes a race where the sensor was unpaused first and could
            // queue a second trigger while the same object was still present.
            lidar_mark_lidar_frame_completed();
            lidar_set_paused(false);
        }
        s_lidarCaptureInProgress = false;
        s_captureInProgress = false;
    }
};

bool capture_is_busy()
{
    return s_captureInProgress;
}

bool capture_is_lidar_busy()
{
    return s_lidarCaptureInProgress;
}

static bool request_client_photo_accept(WiFiClient &client, const char *reason, uint16_t distanceMm)
{
    if (!client || !client.connected())
    {
        return false;
    }

    String request = String("PHOTO_REQUEST REASON ") + String(reason ? reason : "unknown");

    if (reason && strcmp(reason, "lidar") == 0)
    {
        request += " DISTANCE_MM ";
        request += String(distanceMm);
    }

    if (!send_line_default(client, request))
    {
        log_warn("capture", "photo request could not be sent; closing client connection");
        client.stop();
        return false;
    }

    log_infof("capture", "waiting for hub ACK reason=%s distance_mm=%u", reason ? reason : "unknown", distanceMm);

    String ack;
    if (!read_client_line(client, ack, PHOTO_ACCEPT_TIMEOUT_MS) || ack != "ACK")
    {
        log_warnf("capture", "hub did not ACK photo request reason=%s received='%s'", reason ? reason : "unknown", ack.c_str());
        return false;
    }

    return true;
}

static void discard_stale_camera_frames()
{
    for (int i = 0; i < DISCARD_STALE_FRAMES; i++)
    {
        camera_fb_t *old_fb = camera_capture();
        if (old_fb)
        {
            camera_release(old_fb);
        }
        delay(5);
    }
}

static String build_frame_header(size_t frameLen, const char *reason, uint32_t captureMs, uint16_t lidarDistanceMm)
{
    String header = String("FRAME ") + String((uint32_t)frameLen) +
                    " REASON " + String(reason ? reason : "unknown") +
                    " CAPTURE_MS " + String(captureMs);

    if (reason && strcmp(reason, "lidar") == 0)
    {
        header += " DISTANCE_MM ";
        header += String(lidarDistanceMm);
    }

    return header;
}

static bool wait_for_hub_ready(WiFiClient &client)
{
    String ready;
    if (!read_client_line(client, ready, FRAME_READY_TIMEOUT_MS) || ready != "READY")
    {
        log_warnf("capture", "hub not ready for JPEG payload received='%s'", ready.c_str());
        return false;
    }

    return true;
}

bool capture_and_send(WiFiClient &client, const char *reason, uint16_t lidarDistanceMm)
{
    const char *safeReason = reason ? reason : "unknown";

    if (s_captureInProgress)
    {
        log_warnf("capture", "request skipped because another capture is active reason=%s", safeReason);
        return false;
    }

    bool isLidarRequest = strcmp(safeReason, "lidar") == 0;
    CaptureStateGuard captureGuard(safeReason);

    if (isLidarRequest)
    {
        // Stop sampling immediately, before waiting for the Hub ACK. Otherwise
        // the LiDAR task may keep seeing the same object and queue another
        // trigger while this request is still handshaking.
        lidar_set_paused(true);
    }

    if (!client || !client.connected())
    {
        log_warn("capture", "request skipped because no TCP client is connected");
        return false;
    }

    if (!request_client_photo_accept(client, safeReason, lidarDistanceMm))
    {
        log_warnf("capture", "request cancelled because hub did not accept photo reason=%s", safeReason);
        return false;
    }

    log_infof("capture", "starting camera capture reason=%s", safeReason);

    // Pause LiDAR I2C reads while the camera frame is being captured/sent.
    lidar_set_paused(true);
    delay(80);
    discard_stale_camera_frames();

    int64_t captureStartUs = esp_timer_get_time();
    camera_fb_t *fb = camera_capture();
    uint32_t captureMs = (uint32_t)((esp_timer_get_time() - captureStartUs) / 1000);

    if (!fb)
    {
        log_errorf("capture", "camera_capture failed reason=%s", safeReason);
        client.println("CAMERA_ERROR");
        if (!isLidarRequest)
        {
            lidar_set_paused(false);
        }
        return false;
    }

    String header = build_frame_header(fb->len, safeReason, captureMs, lidarDistanceMm);
    if (!send_line_default(client, header))
    {
        log_warn("capture", "FRAME header send failed; closing client connection");
        camera_release(fb);
        client.stop();
        if (!isLidarRequest)
        {
            lidar_set_paused(false);
        }
        return false;
    }

    if (!wait_for_hub_ready(client))
    {
        camera_release(fb);
        client.stop();
        if (!isLidarRequest)
        {
            lidar_set_paused(false);
        }
        return false;
    }

    delay(6);
    int64_t sendStartUs = esp_timer_get_time();
    bool sendOk = send_all(client, fb->buf, fb->len, TCP_SEND_STALL_TIMEOUT_MS, TCP_SEND_TOTAL_TIMEOUT_MS);
    uint32_t sendMs = (uint32_t)((esp_timer_get_time() - sendStartUs) / 1000);

    if (sendOk)
    {
        espnow_store_last_frame(fb->buf, fb->len);
    }

    size_t frameLen = fb->len;
    camera_release(fb);
    if (!isLidarRequest)
    {
        lidar_set_paused(false);
    }

    float kbps = (sendMs > 0) ? (frameLen * 8.0f) / ((float)sendMs / 1000.0f) / 1000.0f : 0.0f;
    log_infof("capture", "done reason=%s bytes=%u capture_ms=%u send_ms=%u kbps=%.0f", safeReason, (unsigned)frameLen, captureMs, sendMs, kbps);

    if (!sendOk)
    {
        log_warn("capture", "JPEG payload send failed; closing client connection");
        client.stop();
        return false;
    }

    (void)isLidarRequest;
    return true;
}

bool capture_lidar_request_if_ready(WiFiClient &client)
{
    uint16_t lidarTriggerDistanceMm = 0;

    if (s_captureInProgress || !client || !client.connected() || !lidar_is_enabled())
    {
        return false;
    }

    if (!lidar_consume_photo_request(lidarTriggerDistanceMm))
    {
        return false;
    }

    log_infof("lidar", "trigger accepted; starting capture distance_mm=%u", lidarTriggerDistanceMm);
    capture_and_send(client, "lidar", lidarTriggerDistanceMm);
    return true;
}

void handle_camera_capture_command(WiFiClient &client)
{
    if (s_lidarCaptureInProgress)
    {
        log_warn("capture", "manual camera command rejected because LiDAR capture is active");
        client.println("BUSY lidar_capture");
        return;
    }

    if (s_captureInProgress)
    {
        log_warn("capture", "manual camera command rejected because capture/send is busy");
        client.println("BUSY camera");
        return;
    }

    capture_and_send(client, "manual");
}

void handle_camera_config_command(WiFiClient &client, const String &message)
{
    log_infof("camera", "config command received: %s", message.c_str());

    char framesizeName[16] = {0};
    int quality = -1;
    int ae = -1;
    int contrast = -1;
    int saturation = -1;
    int brightness = -1;

    int parsed = sscanf(message.c_str(), "config %15s %d %d %d %d %d", framesizeName, &quality, &ae, &contrast, &saturation, &brightness);

    if (parsed != 6)
    {
        client.println("CONFIG_ERROR invalid format");
        return;
    }

    bool ok = camera_set_basic_settings(framesizeName, quality, ae, contrast, saturation, brightness);

    if (ok)
    {
        client.printf("CONFIG_OK %s %d %d %d %d %d\n", framesizeName, quality, ae, contrast, saturation, brightness);
    }
    else
    {
        client.printf("CONFIG_ERROR failed %s %d %d %d %d %d\n", framesizeName, quality, ae, contrast, saturation, brightness);
    }
}

void handle_ae_level_command(WiFiClient &client, const String &message)
{
    log_infof("camera", "ae_level command received: %s", message.c_str());

    int aeLevel = 0;
    int parsed = sscanf(message.c_str(), "ae_level %d", &aeLevel);

    if (parsed != 1)
    {
        client.println("AE_ERROR invalid format");
        return;
    }

    bool ok = camera_set_ae_level(aeLevel);

    if (ok)
    {
        client.printf("AE_OK %d\n", aeLevel);
    }
    else
    {
        client.printf("AE_ERROR failed %d\n", aeLevel);
    }
}
