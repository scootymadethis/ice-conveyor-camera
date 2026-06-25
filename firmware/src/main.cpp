#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "app_config.h"
#include "app_log.h"
#include "camera_service.h"
#include "capture_service.h"
#include "command_handler.h"
#include "display_service.h"
#include "lidar_service.h"
#include "tcp_utils.h"
#include "wifi_service.h"

static const uint16_t SERVER_PORT = 3131;

static WiFiServer server(SERVER_PORT);
static WiFiClient activeClient;

static bool mount_filesystem()
{
    if (LittleFS.begin(true))
    {
        log_info("storage", "LittleFS mounted");
        return true;
    }

    log_error("storage", "LittleFS mount failed");
    return false;
}

static bool connect_wifi_and_start_server()
{
    WiFi.setSleep(false);

    bool connected = wifi_connect(WIFI_CONNECT_TIMEOUT_MS);
    if (connected)
    {
        WiFi.setTxPower(WIFI_TX_POWER);
        log_infof("wifi", "connected ip=%s rssi=%d", wifi_ip(), wifi_rssi());
    }
    else
    {
        log_error("wifi", "initial connection failed; loop() will keep retrying");
    }

    // Start the server once. It will accept clients after Wi-Fi comes back.
    server.begin();
    log_infof("tcp", "server listening port=%u", SERVER_PORT);
    return connected;
}

static void start_camera()
{
    if (camera_init())
    {
        log_info("camera", "camera initialized and ready");
    }
    else
    {
        log_error("camera", "camera init failed; TCP commands will still be served where possible");
    }
}

static void start_optional_lidar_and_display()
{
#if ENABLE_LIDAR_AT_BOOT
    bool lidarOk = lidar_init();
#else
    bool lidarOk = false;
    log_info("lidar", "boot init disabled by ENABLE_LIDAR_AT_BOOT=0");
#endif

#if ENABLE_DISPLAY_AT_BOOT
    if (display_init())
    {
        display_show_ip(String(wifi_ip()));
        log_info("display", "OLED initialized");
    }
    else
    {
        log_warn("display", "OLED init failed");
    }
#else
    log_info("display", "boot init disabled by ENABLE_DISPLAY_AT_BOOT=0");
#endif

    if (lidarOk)
    {
        if (lidar_start_measure_task())
        {
            log_info("lidar", "measurement task started");
        }
        else
        {
            log_warn("lidar", "measurement task did not start");
        }
    }
}


static void send_boot_config_snapshot(WiFiClient &client)
{
    JsonDocument doc;
    doc["ok"] = true;
    doc["ip"] = wifi_ip();
    doc["wifi_channel"] = WiFi.channel();

    JsonObject camera = doc["camera"].to<JsonObject>();
    camera_fill_config_json(camera);

    LidarStatus lidarStatus;
    lidar_get_status(lidarStatus, false);
    JsonObject lidar = doc["lidar"].to<JsonObject>();
    lidar["lidar_ok"] = lidarStatus.lidarOk;
    lidar["enabled"] = lidarStatus.enabled;
    lidar["baseline_ready"] = lidarStatus.baselineReady;
    lidar["current_valid"] = lidarStatus.currentValid;
    lidar["threshold_percent"] = lidarStatus.thresholdPercent;
    lidar["threshold_mm"] = lidarStatus.baselineReady ? lidarStatus.thresholdMm : 0;
    lidar["stable_delta_mm"] = lidarStatus.stableDeltaMm;
    lidar["good_sample_count"] = lidarStatus.goodSampleCount;
    lidar["sample_delay_ms"] = lidarStatus.sampleDelayMs;
    lidar["post_frame_delay_ms"] = lidarStatus.postFrameDelayMs;
    lidar["out_of_range_mm"] = LIDAR_OUT_OF_RANGE_DISTANCE_MM;
    if (lidarStatus.baselineReady)
    {
        lidar["base_mm"] = lidarStatus.baselineMm;
    }
    else
    {
        lidar["base_mm"] = nullptr;
    }
    if (lidarStatus.currentValid)
    {
        lidar["current_mm"] = lidarStatus.currentDistanceMm;
    }
    else
    {
        lidar["current_mm"] = nullptr;
    }

    String json;
    serializeJson(doc, json);
    client.print("BOOT_CONFIG ");
    client.println(json);
}

static void close_stale_client()
{
    if (activeClient)
    {
        activeClient.stop();
        log_info("tcp", "stale client closed");
    }
}

static void accept_client_if_available()
{
    if (activeClient && activeClient.connected())
    {
        return;
    }

    close_stale_client();
    activeClient = server.available();

    if (!activeClient)
    {
        return;
    }

    activeClient.setNoDelay(true);
    activeClient.setTimeout(CLIENT_READ_TIMEOUT_MS);
    send_line_default(activeClient, "ESP32 connected.");
    send_boot_config_snapshot(activeClient);
    log_infof("tcp", "hub connected remote_ip=%s", activeClient.remoteIP().toString().c_str());
}

static void handle_available_client_command()
{
    if (!activeClient || !activeClient.connected() || !activeClient.available())
    {
        return;
    }

    String message = activeClient.readStringUntil('\n');
    message.trim();

    if (message.length() == 0)
    {
        return;
    }

    handle_client_message(activeClient, message);
}

static void handle_idle_lidar_trigger()
{
    if (activeClient && activeClient.connected() && capture_lidar_request_if_ready(activeClient))
    {
        delay(1);
    }
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(300);

    log_info("boot", "ESP32-S3-EYE firmware starting");

    if (!mount_filesystem())
    {
        return;
    }

    connect_wifi_and_start_server();
    start_camera();
    start_optional_lidar_and_display();
}

void loop()
{
    if (!wifi_ensure_connected())
    {
        log_warn("wifi", "not connected; closing active client and retrying soon");
        close_stale_client();
        delay(1000);
        return;
    }

    accept_client_if_available();
    handle_available_client_command();
    handle_idle_lidar_trigger();

    delay(1);
}
