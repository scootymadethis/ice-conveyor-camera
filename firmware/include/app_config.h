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
#define CAMERA_XCLK_FREQ_HZ     20000000
#define CAMERA_FB_COUNT         2
#define TCP_SEND_CHUNK_BYTES     1460
#define DISCARD_STALE_FRAMES     1
