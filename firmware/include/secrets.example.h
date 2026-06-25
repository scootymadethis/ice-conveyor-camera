#pragma once
// -----------------------------------------------------------------------------
// Copy this file to "secrets.h" and fill in real values.
// secrets.h is gitignored and MUST NEVER be committed.
// -----------------------------------------------------------------------------

#define WIFI_SSID     "ice-free-wifi"
#define WIFI_PASSWORD "example"

// Stable identity for this physical camera. Used as X-Camera-Id and in filenames.
#define CAMERA_ID     "esp32s3-eye-01"

// MAC address dello ESP ricevente per invio ultimo frame via ESP-NOW.
// Lascia 00:00:00:00:00:00 per disabilitare la funzione.
#define ESP_NOW_PEER_MAC {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC}
