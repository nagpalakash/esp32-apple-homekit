/*
 * Template for credentials. Copy this file to "secrets.h" in the same folder
 * and fill in your own values. secrets.h is listed in .gitignore and is never
 * committed, so the real WiFi password stays off GitHub.
 *
 *   copy secrets.example.h secrets.h        (Windows)
 *   cp   secrets.example.h secrets.h        (macOS / Linux)
 *
 * With ENABLE_WIFI_MANAGER active the SSID/password below are only a fallback,
 * because WiFi is normally configured through the captive portal.
 */
#pragma once

#define WIFI_SSID        "YourNetworkName"
#define WIFI_PASSWORD    "YourNetworkPassword"

// Network hostname / WiFiManager portal SSID: no spaces or punctuation.
#define DEVICE_HOSTNAME  "My-Accessory-1"

// Friendly name shown in the Apple Home app. Spaces and punctuation are fine.
#define ACCESSORY_NAME   "Accessory 1"
