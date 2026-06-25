#pragma once
// -----------------------------------------------------------------------------
// Compile-time application defaults (non-secret). Tunables for timeouts,
// polling, and initial camera settings live here. Secrets come from secrets.h.
// -----------------------------------------------------------------------------
#include "secrets.h"

#define SERIAL_BAUD 115200

// --- Wi-Fi -------------------------------------------------------------------
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_TX_POWER WIFI_POWER_19_5dBm

// --- Capture loop ------------------------------------------------------------

#define DEFAULT_JPEG_QUALITY 5
#define DEFAULT_FRAMESIZE "HD"

// --- Camera ------------------------------------------------------------------
#define CAMERA_XCLK_FREQ_HZ 20000000
#define CAMERA_FB_COUNT 2
#define TCP_SEND_CHUNK_BYTES 1436
#define TCP_SEND_STALL_TIMEOUT_MS 7000
#define TCP_SEND_TOTAL_TIMEOUT_MS 30000
#define CLIENT_READ_TIMEOUT_MS 10000
#define DISCARD_STALE_FRAMES 1
#define PHOTO_ACCEPT_TIMEOUT_MS 1200
#define FRAME_READY_TIMEOUT_MS 5000

// --- Optional peripherals at boot -------------------------------------------
// Keep current behavior explicit: the code supports LiDAR/display commands, but
// boot-time init stays disabled until the hardware wiring is confirmed.
#ifndef ENABLE_LIDAR_AT_BOOT
#define ENABLE_LIDAR_AT_BOOT 1
#endif
#ifndef ENABLE_DISPLAY_AT_BOOT
#define ENABLE_DISPLAY_AT_BOOT 0
#endif

// --- Lidar / trigger -----------------------------------------------------------
#define LIDAR_OUT_OF_RANGE_DISTANCE_MM 8191
#define LIDAR_BASELINE_SAMPLE_COUNT 20
#define LIDAR_BASELINE_STABLE_DELTA_MM 8
#define LIDAR_BASELINE_TRACK_DIVISOR 12
#define LIDAR_POLL_INTERVAL_MS 50
#define LIDAR_GOOD_SAMPLE_COUNT_DEFAULT 3
#define LIDAR_SAMPLE_DELAY_MS_DEFAULT 10
#define LIDAR_GOOD_SAMPLE_COUNT_MIN 1
#define LIDAR_GOOD_SAMPLE_COUNT_MAX 25
#define LIDAR_SAMPLE_DELAY_MS_MIN 0
#define LIDAR_SAMPLE_DELAY_MS_MAX 500
#define LIDAR_TRIGGER_COOLDOWN_MS 1000
#define LIDAR_POST_FRAME_DELAY_MS_DEFAULT 2500
#define LIDAR_POST_FRAME_DELAY_MS_MIN 0
#define LIDAR_POST_FRAME_DELAY_MS_MAX 30000
#define LIDAR_ERROR_COOLDOWN_MS 3000
#define LIDAR_TASK_STACK_BYTES 4096
#define LIDAR_TASK_PRIORITY 1
#define LIDAR_TASK_CORE 0

#define LIDAR_THRESHOLD_PERCENT 80

// --- ESP-NOW last-frame forwarding ------------------------------------------
#ifndef ESP_NOW_PEER_MAC
#define ESP_NOW_PEER_MAC {0xE0, 0x72, 0xA1, 0xD6, 0x2C, 0xD4}
#endif
#define ESPNOW_FRAME_CHUNK_BYTES 190
#define ESPNOW_SEND_DELAY_MS 8
#define ESPNOW_SEND_ACK_TIMEOUT_MS 500
#define ESPNOW_SEND_RETRIES 6
// Some ESP-NOW STA/AP combinations can receive unicast frames while the sender
// callback still reports ESP_NOW_SEND_FAIL. Keep this at 0 for large JPEG
// transfers: the receiver deduplicates chunks and validates completion. Set to
// 1 only when you want strict MAC-layer ACK enforcement.
#ifndef ESPNOW_REQUIRE_MAC_ACK
#define ESPNOW_REQUIRE_MAC_ACK 0
#endif
#define ESPNOW_FRAME_MAGIC 0x49434546UL
#define ESPNOW_HELLO_MAGIC 0x49434548UL
#define ESPNOW_PREFLIGHT_HELLO_MS 1800
#define ESPNOW_PREFLIGHT_HELLO_INTERVAL_MS 60
#ifndef ESPNOW_FORCE_CHANNEL
#define ESPNOW_FORCE_CHANNEL 0
#endif
