/*
 * Template for credentials. Copy this file to "secrets.h" in the same folder
 * and fill in your own values. secrets.h is listed in .gitignore and is never
 * committed.
 *
 *   copy secrets.example.h secrets.h
 *
 * ESP32 is 2.4 GHz only. On dual-band single-SSID routers, prefer a dedicated
 * 2.4 GHz SSID, or set WIFI_BSSID to the 2.4 GHz AP MAC from your router.
 */
#pragma once

#define WIFI_SSID        "YourNetworkName"
#define WIFI_PASSWORD    "YourNetworkPassword"
#define WIFI_BSSID       ""   // optional "AA:BB:CC:DD:EE:FF"

#define DEVICE_HOSTNAME  "Photo-Light"
#define ACCESSORY_NAME   "Photo Light"
