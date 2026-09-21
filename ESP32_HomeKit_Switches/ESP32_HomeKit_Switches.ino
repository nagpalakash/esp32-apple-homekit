/*
 * Project 1  -  ESP32 WROOM-32  -  Apple HomeKit bridge for relays + IR
 *
 * Library:  ESPHap  (https://github.com/Yurik72/ESPHap)   -> provides homeintegration.h
 *           IRremote 4.x (https://github.com/Arduino-IRremote/Arduino-IRremote)
 *           WiFiManager (https://github.com/tzapu/WiFiManager)  - only if ENABLE_WIFI_MANAGER
 *
 * Board:    "ESP32 Dev Module", Partition scheme with SPIFFS (e.g. "Default 4MB with spiffs")
 *
 * Accessories exposed to Home.app:
 *   ch1  Main Light           -> momentary relay on GPIO19 (500 ms pulse)
 *   ch2  Night Lamp           -> momentary relay on GPIO21 (500 ms pulse)
 *   ch3  Dim Light            -> ON  = GPIO15 held 7 s,  OFF = GPIO2 pulsed 500 ms
 *   ch4  Side Lights          -> IR NEC on GPIO4
 *   ch5  Side Light Mode      -> IR NEC on GPIO4
 */

///////////////////////////////////////////////////////////////////////////////
// BASIC CONFIGURATION
///////////////////////////////////////////////////////////////////////////////
#define ENABLE_WIFI_MANAGER      // captive portal for WiFi setup; comment out to use ssid/password below
#define ENABLE_WEB_SERVER        // small built-in control page + REST endpoints
#define ENABLE_OTA               // /update page (requires ENABLE_WEB_SERVER)
#define ENABLE_RESET_BUTTON      // hold BOOT (GPIO0) 5 s to erase HomeKit pairing

// Credentials live in secrets.h, which is gitignored. Copy secrets.example.h
// to secrets.h and edit it once after cloning.
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
#define DEVICE_HOSTNAME "Akash-Nagpal-Accessory-1"
#endif
#ifndef ACCESSORY_NAME
#define ACCESSORY_NAME "Accessory 1"
#endif
#if !defined(ENABLE_WIFI_MANAGER) && !__has_include("secrets.h")
#error "secrets.h is missing: copy secrets.example.h to secrets.h and set WIFI_SSID / WIFI_PASSWORD"
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <IRremote.hpp>

#ifdef ENABLE_WIFI_MANAGER
#include <WiFiManager.h>
#endif

#ifdef ENABLE_WEB_SERVER
#include <WebServer.h>
WebServer server(80);
bool isWebserver_started = false;
#ifdef ENABLE_OTA
#include <Update.h>
#endif
#endif

extern "C" {
#include "homeintegration.h"
}

///////////////////////////////////////////////////////////////////////////////
// PINS
///////////////////////////////////////////////////////////////////////////////
const int relay_main      = 19;  // main light
const int relay_nightlamp = 21;  // night lamp
const int relay_dim_on    = 15;  // dim light ON  (strapping pin: must be LOW at boot)
const int relay_dim_off   = 2;   // dim light OFF (strapping pin / onboard LED)
const int ir_send_pin     = 4;   // IR LED driver

const uint8_t RELAY_ACTIVE = HIGH;   // relay modules that trigger on HIGH

const uint32_t PULSE_MAIN_MS      = 500;
const uint32_t PULSE_NIGHTLAMP_MS = 500;
const uint32_t PULSE_DIM_ON_MS    = 7000;
const uint32_t PULSE_DIM_OFF_MS   = 500;

///////////////////////////////////////////////////////////////////////////////
// WIFI / IDENTITY
///////////////////////////////////////////////////////////////////////////////
const char* HOSTNAME = DEVICE_HOSTNAME;
const char* ssid     = WIFI_SSID;       // used only when ENABLE_WIFI_MANAGER is off
const char* password = WIFI_PASSWORD;

///////////////////////////////////////////////////////////////////////////////
// HOMEKIT SERVICES
///////////////////////////////////////////////////////////////////////////////
homekit_service_t* svc_main      = NULL;
homekit_service_t* svc_nightlamp = NULL;
homekit_service_t* svc_dim       = NULL;
homekit_service_t* svc_side      = NULL;
homekit_service_t* svc_side_mode = NULL;

const char* pair_file_name = "/pair.dat";

// IR codes are legacy 32-bit MSB-first values, send them with sendNECMSB()
const uint32_t IR_SIDE_ON     = 0xFFA25D;
const uint32_t IR_SIDE_OFF    = 0xFFE21D;
const uint32_t IR_SIDE_MODE   = 0xFF22DD;

///////////////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS
///////////////////////////////////////////////////////////////////////////////
void init_hap_storage();
void storage_changed(char* szstorage, int bufsize);

void set_main(bool val);
void set_nightlamp(bool val);
void set_dim(bool val);
void set_side(bool val);
void set_side_mode(bool val);
void serviceSideMode();

void switch_callback_main(homekit_characteristic_t* ch, homekit_value_t value, void* context);
void switch_callback_nightlamp(homekit_characteristic_t* ch, homekit_value_t value, void* context);
void switch_callback_dim(homekit_characteristic_t* ch, homekit_value_t value, void* context);
void switch_callback_side(homekit_characteristic_t* ch, homekit_value_t value, void* context);
void switch_callback_side_mode(homekit_characteristic_t* ch, homekit_value_t value, void* context);

#ifdef ENABLE_WEB_SERVER
void setupWebServer();
void handleRoot();
void handleGetVal();
void handleSetVal();
#endif
#ifdef ENABLE_WIFI_MANAGER
void startwifimanager();
#endif

///////////////////////////////////////////////////////////////////////////////
// NON-BLOCKING RELAY PULSES
// A HomeKit callback must return quickly, so never delay() inside it.
///////////////////////////////////////////////////////////////////////////////
struct RelayPulse {
  int      pin;
  bool     active;
  uint32_t releaseAt;
};

RelayPulse pulses[] = {
  { relay_main,      false, 0 },
  { relay_nightlamp, false, 0 },
  { relay_dim_on,    false, 0 },
  { relay_dim_off,   false, 0 },
};
const size_t pulseCount = sizeof(pulses) / sizeof(pulses[0]);

void pulseRelay(int pin, uint32_t durationMs) {
  for (size_t i = 0; i < pulseCount; i++) {
    if (pulses[i].pin != pin) continue;
    digitalWrite(pin, RELAY_ACTIVE);
    pulses[i].active    = true;
    pulses[i].releaseAt = millis() + durationMs;
    return;
  }
}

void servicePulses() {
  uint32_t now = millis();
  for (size_t i = 0; i < pulseCount; i++) {
    if (!pulses[i].active) continue;
    if ((int32_t)(now - pulses[i].releaseAt) < 0) continue;   // millis() rollover safe
    digitalWrite(pulses[i].pin, !RELAY_ACTIVE);
    pulses[i].active = false;
  }
}

void sendNecLegacy(uint32_t code) {
  // IRremote 4.x: sendNEC(addr, cmd, repeats) takes split address/command.
  // Classic 32-bit dumps such as 0xFFA25D must go through sendNECMSB().
  IrSender.sendNECMSB(code, 32);
}

///////////////////////////////////////////////////////////////////////////////
// STATE HELPERS
///////////////////////////////////////////////////////////////////////////////
bool getSwitchVal(homekit_service_t* svc) {
  if (!svc) return false;
  homekit_characteristic_t* ch = homekit_service_characteristic_by_type(svc, HOMEKIT_CHARACTERISTIC_ON);
  return ch ? ch->value.bool_value : false;
}

void initSwitchState(homekit_service_t* svc, bool val) {
  if (!svc) return;
  homekit_characteristic_t* ch = homekit_service_characteristic_by_type(svc, HOMEKIT_CHARACTERISTIC_ON);
  if (ch) INIT_CHARACHTERISTIC_VAL(bool, ch, val);   // tell Home.app the initial state
}

void notifyHomeKit(homekit_service_t* svc, bool val) {
  if (!svc) return;
  homekit_characteristic_t* ch = homekit_service_characteristic_by_type(svc, HOMEKIT_CHARACTERISTIC_ON);
  if (!ch) return;
  if (ch->value.bool_value == val) return;      // notify only on real change
  ch->value.bool_value = val;
  homekit_characteristic_notify(ch, ch->value);
}

///////////////////////////////////////////////////////////////////////////////
// SETUP
///////////////////////////////////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println();
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

  pinMode(relay_main, OUTPUT);
  pinMode(relay_nightlamp, OUTPUT);
  pinMode(relay_dim_on, OUTPUT);
  pinMode(relay_dim_off, OUTPUT);
  digitalWrite(relay_main, !RELAY_ACTIVE);
  digitalWrite(relay_nightlamp, !RELAY_ACTIVE);
  digitalWrite(relay_dim_on, !RELAY_ACTIVE);
  digitalWrite(relay_dim_off, !RELAY_ACTIVE);

  IrSender.begin(ir_send_pin);   // owns GPIO4, do not drive it as a relay

#ifdef ENABLE_RESET_BUTTON
  pinMode(0, INPUT_PULLUP);
#endif

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  init_hap_storage();
  set_callback_storage_change(storage_changed);

  hap_setbase_accessorytype(homekit_accessory_category_switch);
  hap_initbase_accessory_service(ACCESSORY_NAME, "Akash Nagpal", "001", "AN_01", "1.0");

  svc_main      = hap_add_switch_service("Main Light",            switch_callback_main,      (void*)&relay_main);
  svc_nightlamp = hap_add_switch_service("Switch-Nightlamp",      switch_callback_nightlamp, (void*)&relay_nightlamp);
  svc_dim       = hap_add_switch_service("Dim Light",             switch_callback_dim,       (void*)&relay_dim_on);
  svc_side      = hap_add_switch_service("Side Lights",           switch_callback_side,      (void*)&ir_send_pin);
  svc_side_mode = hap_add_switch_service("Side Light Mode Change", switch_callback_side_mode, (void*)&ir_send_pin);

  initSwitchState(svc_main, false);
  initSwitchState(svc_nightlamp, false);
  initSwitchState(svc_dim, false);
  initSwitchState(svc_side, false);
  initSwitchState(svc_side_mode, false);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);   // must be set before connecting
#ifdef ENABLE_WIFI_MANAGER
  startwifimanager();
#else
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
#endif

  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  hap_init_homekit_server();

#ifdef ENABLE_WEB_SERVER
  setupWebServer();
#endif
}

///////////////////////////////////////////////////////////////////////////////
// LOOP
///////////////////////////////////////////////////////////////////////////////
void loop() {
  servicePulses();
  serviceSideMode();

#ifdef ENABLE_WEB_SERVER
  if (isWebserver_started) server.handleClient();
#endif

#ifdef ENABLE_RESET_BUTTON
  static uint32_t pressedSince = 0;
  if (digitalRead(0) == LOW) {
    if (pressedSince == 0) pressedSince = millis();
    if (millis() - pressedSince > 5000) {
      Serial.println("Erasing HomeKit pairing...");
      homekit_server_reset();
      SPIFFS.remove(pair_file_name);
      delay(100);
      ESP.restart();
    }
  } else {
    pressedSince = 0;
  }
#endif

  delay(1);   // yield to the HomeKit / WiFi tasks
}

///////////////////////////////////////////////////////////////////////////////
// HOMEKIT PAIRING STORAGE (SPIFFS)
///////////////////////////////////////////////////////////////////////////////
void init_hap_storage() {
  Serial.println("init_hap_storage");

  File fsDAT = SPIFFS.open(pair_file_name, "r");
  if (!fsDAT) {
    Serial.println("Failed to read pair.dat, formatting SPIFFS");
    SPIFFS.format();
  }

  int size = hap_get_storage_size_ex();
  char* buf = new char[size];
  memset(buf, 0xff, size);
  if (fsDAT) fsDAT.readBytes(buf, size);

  hap_init_storage_ex(buf, size);

  if (fsDAT) fsDAT.close();
  delete[] buf;
}

void storage_changed(char* szstorage, int bufsize) {
  SPIFFS.remove(pair_file_name);
  File fsDAT = SPIFFS.open(pair_file_name, "w+");
  if (!fsDAT) {
    Serial.println("Failed to open pair.dat");
    return;
  }
  fsDAT.write((uint8_t*)szstorage, bufsize);
  fsDAT.close();
}

///////////////////////////////////////////////////////////////////////////////
// ACTUATORS
///////////////////////////////////////////////////////////////////////////////
void set_main(bool val) {
  Serial.printf("set_main: %s\n", val ? "True" : "False");
  pulseRelay(relay_main, PULSE_MAIN_MS);     // same pulse for on and off (toggling switch)
  notifyHomeKit(svc_main, val);
}

void set_nightlamp(bool val) {
  Serial.printf("set_nightlamp: %s\n", val ? "True" : "False");
  pulseRelay(relay_nightlamp, PULSE_NIGHTLAMP_MS);
  notifyHomeKit(svc_nightlamp, val);
}

void set_dim(bool val) {
  Serial.printf("set_dim: %s\n", val ? "True" : "False");
  if (val) pulseRelay(relay_dim_on,  PULSE_DIM_ON_MS);
  else     pulseRelay(relay_dim_off, PULSE_DIM_OFF_MS);
  notifyHomeKit(svc_dim, val);
}

void set_side(bool val) {
  Serial.printf("set_side: %s\n", val ? "True" : "False");
  sendNecLegacy(val ? IR_SIDE_ON : IR_SIDE_OFF);
  notifyHomeKit(svc_side, val);
}

// The mode key is a stateless remote button, so the tile springs back to OFF.
uint32_t sideModeAutoOffAt = 0;
const uint32_t SIDE_MODE_AUTO_OFF_MS = 1000;

void set_side_mode(bool val) {
  Serial.printf("set_side_mode: %s\n", val ? "True" : "False");
  if (!val) {
    sideModeAutoOffAt = 0;
    return;
  }
  sendNecLegacy(IR_SIDE_MODE);
  sideModeAutoOffAt = millis() + SIDE_MODE_AUTO_OFF_MS;
}

void serviceSideMode() {
  if (sideModeAutoOffAt == 0) return;
  if ((int32_t)(millis() - sideModeAutoOffAt) < 0) return;
  sideModeAutoOffAt = 0;
  notifyHomeKit(svc_side_mode, false);
}

///////////////////////////////////////////////////////////////////////////////
// HOMEKIT CALLBACKS
///////////////////////////////////////////////////////////////////////////////
void switch_callback_main(homekit_characteristic_t* ch, homekit_value_t value, void* context) {
  set_main(ch->value.bool_value);
}
void switch_callback_nightlamp(homekit_characteristic_t* ch, homekit_value_t value, void* context) {
  set_nightlamp(ch->value.bool_value);
}
void switch_callback_dim(homekit_characteristic_t* ch, homekit_value_t value, void* context) {
  set_dim(ch->value.bool_value);
}
void switch_callback_side(homekit_characteristic_t* ch, homekit_value_t value, void* context) {
  set_side(ch->value.bool_value);
}
void switch_callback_side_mode(homekit_characteristic_t* ch, homekit_value_t value, void* context) {
  set_side_mode(ch->value.bool_value);
}

///////////////////////////////////////////////////////////////////////////////
// WEB SERVER
///////////////////////////////////////////////////////////////////////////////
#ifdef ENABLE_WEB_SERVER

static const char PAGE_INDEX[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AN HomeKit</title>
<style>body{font-family:sans-serif;margin:24px}button{font-size:16px;padding:8px 16px;margin:4px}
.row{margin:10px 0}</style></head><body>
<h2>AN HomeKit switches</h2>
<div id="list"></div>
<p><a href="/update">Firmware update</a></p>
<script>
const names={ch1:"Main Light",ch2:"Night Lamp",ch3:"Dim Light",ch4:"Side Lights",ch5:"Side Light Mode"};
function set(v,on){fetch(`/set?var=${v}&val=${on}`).then(load);}
function load(){fetch('/get').then(r=>r.json()).then(s=>{
 document.getElementById('list').innerHTML=Object.keys(names).map(k=>
 `<div class="row"><b>${names[k]}</b>: ${s[k]?'ON':'OFF'}
 <button onclick="set('${k}',true)">ON</button>
 <button onclick="set('${k}',false)">OFF</button></div>`).join('');});}
load();
</script></body></html>)HTML";

void handleRoot() {
  server.send_P(200, "text/html", PAGE_INDEX);
}

void handleGetVal() {
  // One response per request: return all channels as JSON.
  String json = "{";
  json += "\"ch1\":" + String(getSwitchVal(svc_main)      ? 1 : 0) + ",";
  json += "\"ch2\":" + String(getSwitchVal(svc_nightlamp) ? 1 : 0) + ",";
  json += "\"ch3\":" + String(getSwitchVal(svc_dim)       ? 1 : 0) + ",";
  json += "\"ch4\":" + String(getSwitchVal(svc_side)      ? 1 : 0) + ",";
  json += "\"ch5\":" + String(getSwitchVal(svc_side_mode) ? 1 : 0);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetVal() {
  if (!server.hasArg("var") || !server.hasArg("val")) {
    server.send(400, "text/plain", "Bad args");
    return;
  }

  String var = server.arg("var");
  bool   val = (server.arg("val") == "true" || server.arg("val") == "1");

  if      (var == "ch1") set_main(val);
  else if (var == "ch2") set_nightlamp(val);
  else if (var == "ch3") set_dim(val);
  else if (var == "ch4") set_side(val);
  else if (var == "ch5") set_side_mode(val);
  else {
    server.send(404, "text/plain", "Unknown var");
    return;
  }

  server.send(200, "text/plain", val ? "1" : "0");
}

#ifdef ENABLE_OTA
static const char PAGE_UPDATE[] PROGMEM =
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='update'><input type='submit' value='Update'></form>";

void setupOta() {
  server.on("/update", HTTP_GET, []() {
    server.send_P(200, "text/html", PAGE_UPDATE);
  });

  server.on("/update", HTTP_POST,
    []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK, rebooting");
      delay(500);
      ESP.restart();
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("OTA: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("OTA done: %u bytes\n", upload.totalSize);
        else Update.printError(Serial);
      }
    });
}
#endif  // ENABLE_OTA

void setupWebServer() {
  Serial.println("Setting web server");

  server.on("/",    handleRoot);
  server.on("/get", handleGetVal);
  server.on("/set", handleSetVal);
#ifdef ENABLE_OTA
  setupOta();
#endif
  server.begin();
  isWebserver_started = true;

  String url = String("http://") + WiFi.localIP().toString();
  Serial.println("Web site " + url);
#ifdef ENABLE_OTA
  Serial.println("Update   " + url + "/update");
#endif
}
#endif  // ENABLE_WEB_SERVER

///////////////////////////////////////////////////////////////////////////////
// WIFI MANAGER
///////////////////////////////////////////////////////////////////////////////
#ifdef ENABLE_WIFI_MANAGER
void startwifimanager() {
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  if (!wifiManager.autoConnect(HOSTNAME, NULL)) {
    Serial.println("WiFiManager failed, restarting");
    delay(1000);
    ESP.restart();
  }
}
#endif
