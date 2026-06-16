#pragma once
// -----------------------------------------------------------------------------
// wifi_service: connect to the lab Wi-Fi (WPA2-PSK) and expose status helpers.
// -----------------------------------------------------------------------------

// Connect using the credentials from secrets.h. Blocks up to timeout_ms.
bool wifi_connect(unsigned long timeout_ms);

// True if currently associated.
bool wifi_is_connected();

// Reconnect if the link dropped. Returns true once connected.
bool wifi_ensure_connected();

// Last known local IP as a string ("0.0.0.0" before first connect).
const char *wifi_ip();

// Current RSSI in dBm (0 if not connected).
int wifi_rssi();
