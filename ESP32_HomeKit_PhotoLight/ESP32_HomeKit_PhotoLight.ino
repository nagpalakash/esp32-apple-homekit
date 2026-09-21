/*
 * Project 3 - ESP32 WROOM-32 - single HomeKit switch "Photo Light"
 *
 * Uses HomeSpan (works with ESP32 Arduino cores 2.x / 3.x).
 *
 * Board:  ESP32 Dev Module
 * Partition Scheme: Minimal SPIFFS
 * Pair:   Serial Monitor @ 115200 -> type 'H'  (default code 466-37-726)
 *
 * Dual-band tip: ESP32 is 2.4 GHz only. On a single SSID that mixes 2.4/5 GHz,
 * this sketch forces B/G/N + HT20. Best reliability = a dedicated 2.4 GHz SSID
 * (or optional WIFI_BSSID lock to the 2.4 GHz radio in secrets.h).
 */

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "Photo-Light"
#endif
#ifndef ACCESSORY_NAME
#define ACCESSORY_NAME "Photo Light"
#endif
#ifndef WIFI_BSSID
#define WIFI_BSSID ""   // optional "AA:BB:CC:DD:EE:FF" of the 2.4 GHz AP
#endif
#if !__has_include("secrets.h")
#error "secrets.h is missing: copy secrets.example.h to secrets.h"
#endif

#include "HomeSpan.h"
#include <WiFi.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Run before setup() — USB power dips when WiFi starts and trips the brownout reset.
struct BrownoutOff {
  BrownoutOff() { WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); }
} g_brownoutOff;

// Bump when you want a brand-new HomeKit Accessory ID on next boot
#define HOMESPAN_ID_GENERATION  1

const int     RELAY_PIN    = 19;
const uint8_t RELAY_ACTIVE = HIGH;

struct PhotoLightSwitch : Service::Switch {
  int pin;
  SpanCharacteristic *power;

  PhotoLightSwitch(int relayPin) : Service::Switch() {
    pin = relayPin;
    power = new Characteristic::On(false);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, !RELAY_ACTIVE);
  }

  boolean update() override {
    bool on = power->getNewVal();
    digitalWrite(pin, on ? RELAY_ACTIVE : !RELAY_ACTIVE);
    Serial.printf("Photo Light -> %s\n", on ? "ON" : "OFF");
    return true;
  }
};

bool parseBssid(const char *text, uint8_t out[6]) {
  if (!text || !*text) return false;
  unsigned int b[6];
  if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x",
             &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)b[i];
  return true;
}

void wifiBegin24GHz(const char *ssid, const char *pwd) {
  Serial.printf("WiFi: connecting to '%s' (2.4 GHz B/G/N, HT20)...\n", ssid);

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);

  uint8_t bssid[6];
  if (parseBssid(WIFI_BSSID, bssid)) {
    Serial.printf("WiFi: locking to BSSID %s (use the 2.4 GHz AP MAC)\n", WIFI_BSSID);
    WiFi.begin(ssid, pwd, 0, bssid);
  } else {
    WiFi.begin(ssid, pwd);
  }
}

void serviceWifi() {
  static uint32_t disconnectedSince = 0;
  static uint32_t lastAttempt = 0;

  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSince = 0;
    return;
  }

  uint32_t now = millis();
  if (disconnectedSince == 0) disconnectedSince = now;

  if (now - disconnectedSince < 20000) return;
  if (lastAttempt != 0 && now - lastAttempt < 30000) return;

  lastAttempt = now;
  Serial.println("WiFi: still down - retrying 2.4 GHz connect...");
  wifiBegin24GHz(WIFI_SSID, WIFI_PASSWORD);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.setSleep(false);

  homeSpan.setLogLevel(1);
  homeSpan.setWifiCredentials(WIFI_SSID, WIFI_PASSWORD);
  homeSpan.setWifiBegin(wifiBegin24GHz);
  homeSpan.setHostNameSuffix("");
  homeSpan.setQRID("PHOT");              // unique Setup ID (4 chars)
  homeSpan.setPairingCode("46637726");   // 466-37-726
  homeSpan.enableAutoStartAP();
  homeSpan.enableWebLog(50, "pool.ntp.org", "UTC+9", "status");

  homeSpan.begin(Category::Switches, ACCESSORY_NAME, DEVICE_HOSTNAME);

  // One-shot: new HomeKit Accessory ID for this project
  Preferences prefs;
  prefs.begin("photoflash", false);
  if (prefs.getUInt("idgen", 0) != HOMESPAN_ID_GENERATION) {
    prefs.putUInt("idgen", HOMESPAN_ID_GENERATION);
    prefs.end();
    Serial.println("Generating NEW HomeKit Accessory ID - rebooting once...");
    homeSpan.processSerialCommand("H");
  }
  prefs.end();

  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Name("Photo Light");
      new Characteristic::Manufacturer("Akash Nagpal");
      new Characteristic::SerialNumber("PHOTO-001");
      new Characteristic::Model("AN_PHOTO");
      new Characteristic::FirmwareRevision("1.0.0");

    new PhotoLightSwitch(RELAY_PIN);
}

void loop() {
  homeSpan.poll();
  serviceWifi();

  auto [status, duration] = homeSpan.getStatus();
  if (status == HS_PAIRED && duration > 60) {
    static uint32_t lastWifiKick = 0;
    if (millis() - lastWifiKick > 60000) {
      lastWifiKick = millis();
      Serial.println("No HomeKit session - reconnecting WiFi to refresh mDNS");
      WiFi.reconnect();
    }
  }
  if (status == HS_PAIRED && duration > 180) {
    Serial.println("Still no HomeKit session after 3 min - rebooting");
    homeSpan.processSerialCommand("R");
  }
}
