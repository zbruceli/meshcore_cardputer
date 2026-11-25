#include <Arduino.h>
#include <SPIFFS.h>
#include <driver/rtc_io.h>
#include <M5Cardputer.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/MultiSerialInterface.h>
#include <helpers/esp32/SerialBLEInterface.h>
#include "target.h"
#include "chat_client.h"
#include "ui.h"

// Same objects and setup order as MeshCore's companion_radio main.cpp: the official engine serves the phone app
// over Bluetooth, and the device UI (ChatClient + ui) is its listener.
DataStore store(SPIFFS, rtc_clock);
StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);
MultiSerialInterface interface_manager;
SerialBLEInterface bluetooth_interface;

bool radio_ok = false;
uint32_t loop_max_ms = 0;

// Before v0.3 our own files held the node settings, contacts and channels: copy them over once.
static void migrateOldData() {
  if (prefs.onboarded && !SPIFFS.exists("/prefs.json") && !SPIFFS.exists("/new_prefs")) {
    NodePrefs& np = client.node();
    StrHelper::strncpy(np.node_name, prefs.name, sizeof(np.node_name));
    np.freq = prefs.freq;
    np.bw = prefs.bw;
    np.sf = prefs.sf;
    np.cr = prefs.cr;
    np.tx_power_dbm = prefs.tx_power;
    np.path_hash_mode = prefs.path_hash_size - 1;
    uint8_t chat = 1 << ADV_TYPE_CHAT, rpt = 1 << ADV_TYPE_REPEATER;
    int mode = prefs.auto_add == 0 ? ADD_OFF : prefs.auto_add == chat ? ADD_CHAT :
               prefs.auto_add == (chat | rpt) ? ADD_CHAT_RPT : ADD_ALL;
    client.setAddMode(mode, prefs.overwrite);   // saves the node prefs
    client.applyRadio();
  }
  if (!SPIFFS.exists("/contacts3") && contactsLoad(the_mesh)) store.saveContacts(&the_mesh);
  if (!SPIFFS.exists("/channels2") && channelsLoad(the_mesh)) store.saveChannels(&the_mesh);
}

void bluetoothApply() {   // on only once onboarded (no app-driven TX before a region is chosen) and not turned off
  if (prefs.onboarded && !prefs.ble_off) interface_manager.enableBluetooth();
  else interface_manager.disableBluetooth();
}

// GPS (Settings > GPS: off / clock / location). The module has no power switch and draws ~30 mA while tracking,
// so it only runs in short sessions and sits in CASIC standby otherwise (wakes on its timer or on any command; each
// wake-up prints a banner, which starts the next session). A session streams GGA + RMC and ends as soon as it has
// what it came for:
//   clock:     two valid fixes a second apart agree -> set the clock; next session in 6 h
//   location:  3 agreeing fixes and HDOP <= 2 (or the best after 20 s) -> position; next session in 10 min
//   request:   the same position, once (send my location / refresh), in any mode
// No fix in time (indoors): give up and retry in 30 min. Ephemeris stays valid ~4 h, so a recent fix means a quick
// restart and a shorter timeout; otherwise allow a cold start. By a window a fix can still take 20-40 s.
// Typical cost in location mode: ~5-35 s at 30 mA every 10 min, ~0.3-2 mA on average.
#define GPS_CLOCK_SECS   21600    // clock only (and "off"): sleep 6 h; the RTC drifts ~0.1 s an hour
#define GPS_LOC_SECS     600      // location: a fresh position every 10 min
#define GPS_RETRY_SECS   1800     // no fix: retry in 30 min
#define GPS_HOT_MS       90000UL  // session timeout after a fix in the last 4 h
#define GPS_COLD_MS      150000UL // ... otherwise
#define GPS_REQ_MS       180000UL // ... when the user asked and is watching (can cancel)
#define GPS_GOOD_HDOP    2.0f
#define GPS_SETTLE_MS    20000UL  // take the best position after this long even if HDOP stays above 2
uint8_t gps_state = GPS_OFF;
GpsPos gps_pos;
bool gps_req;
static uint32_t gps_start, gps_first_valid, gps_last_t, gps_wake_at, gps_fix_ms, gps_clock_ms;
static uint8_t gps_stable;

static void gpsIdle(uint32_t secs) {   // end a session: standby until the next one
  gps_req = false;
  gps_state = prefs.gps ? GPS_FIX : GPS_OFF;
  if (!prefs.gps) secs = GPS_CLOCK_SECS;   // off: re-sent each time it wakes by itself
  gpsOutput(false);
  gpsStandby(secs);
  gps_wake_at = millis() + secs * 1000UL + 60000;
}

static void gpsStart() {   // also wakes it from standby; the banner that follows re-sends the output setting
  gps_state = GPS_SEARCH;
  gps_start = millis();
  gps_first_valid = gps_last_t = 0;
  gps_stable = 0;
  gpsOutput(true);
}

void gpsApply() {
  if (prefs.gps) gpsStart();
  else if (!gps_req) gpsIdle(GPS_CLOCK_SECS);
}

void gpsRequest() {
  if (gps_pos.at && client.now() - gps_pos.at < 60) return;   // fresh enough
  gps_req = true;
  if (gps_state != GPS_SEARCH) gpsStart();
}

uint32_t gpsSearchSecs() { return gps_state == GPS_SEARCH ? (millis() - gps_start) / 1000 : 0; }

bool myPosition(double& lat, double& lon) {
  if (gps_pos.at) { lat = gps_pos.lat; lon = gps_pos.lon; return true; }
  lat = sensors.node_lat;   // set from the phone app (or a fix saved before a reboot)
  lon = sensors.node_lon;
  return lat != 0 || lon != 0;
}

// Phone app: GPS on/off. Off -> off; on keeps clock or location, and from off picks location (what "GPS on" means on
// other MeshCore boards: the node keeps a position).
const char* CardputerSensors::getSettingValue(int i) const { return i == 0 ? (prefs.gps ? "1" : "0") : NULL; }

bool CardputerSensors::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") != 0) return false;   // e.g. "gps_interval": ours is fixed (10 min)
  uint8_t mode = strcmp(value, "0") == 0 ? GPS_MODE_OFF : prefs.gps ? prefs.gps : GPS_MODE_LOC;
  if (mode != prefs.gps) {
    prefs.gps = mode;
    prefsSave();
    gpsApply();
  }
  return true;
}

static void gpsSetPosition(const GpsFix& f) {
  gps_pos.lat = f.lat;
  gps_pos.lon = f.lon;
  gps_pos.acc_m = (uint16_t)constrain(f.hdop * 5.0f, 2.0f, 999.0f);   // rough: HDOP x ~5 m receiver error
  gps_pos.at = f.time;
  if (Serial) Serial.printf("GPS position, hdop %.1f, %d sats\n", f.hdop, f.sats);
  if (prefs.gps != GPS_MODE_LOC) return;   // requests in other modes don't move the node's location
  // the node's location (adverts if shared, the phone app); saved only after a real move, to spare the flash
  NodePrefs& np = client.node();
  bool moved = fabs(np.node_lat - f.lat) > 0.0005 || fabs(np.node_lon - f.lon) > 0.0005;   // ~50 m
  sensors.node_lat = f.lat;
  sensors.node_lon = f.lon;
  if (moved) client.saveNode();
}

static void gpsLoop() {
  GpsFix f;
  bool woke;
  bool got = gpsRead(f, woke);   // also drains anything the module sent
  uint32_t ms = millis();
  if (woke) {
    if (Serial) Serial.println("GPS awake");
    if (gps_state == GPS_SEARCH) gpsOutput(true);   // woken by our command: it may have missed it
    else if (prefs.gps || gps_req) gpsStart();       // its standby ran out: next session
    else gpsIdle(GPS_CLOCK_SECS);                    // off: back to sleep
    return;
  }
  if (gps_state != GPS_SEARCH) {
    if (gps_state == GPS_FIX && (int32_t)(ms - gps_wake_at) > 0) gpsStart();   // never heard it wake: try anyway
    return;
  }
  bool want_pos = prefs.gps == GPS_MODE_LOC || gps_req;
  if (got && f.time >= CLOCK_SANE_MIN) {
    gps_stable = gps_last_t && f.time - gps_last_t >= 1 && f.time - gps_last_t <= 2 ? gps_stable + 1 : 1;
    gps_last_t = f.time;
    if (!gps_first_valid) gps_first_valid = ms;
    gps_fix_ms = ms;   // ephemeris is fresh: the next start is a hot one
    // GPS time beats the app and the mesh; once per clock interval is plenty
    if (gps_stable >= 2 && (!gps_clock_ms || ms - gps_clock_ms > (GPS_CLOCK_SECS - 600) * 1000UL)) {
      client.setClock(f.time, CLOCK_GPS);
      gps_clock_ms = ms;
      if (Serial) Serial.printf("GPS clock set: %u\n", (unsigned)f.time);
    }
    if (!want_pos && gps_stable >= 2) {
      gpsIdle(GPS_CLOCK_SECS);
    } else if (want_pos && gps_stable >= 3 && (f.hdop <= GPS_GOOD_HDOP || ms - gps_first_valid > GPS_SETTLE_MS)) {
      gpsSetPosition(f);
      gpsIdle(prefs.gps == GPS_MODE_LOC ? GPS_LOC_SECS : GPS_CLOCK_SECS);
    }
    return;
  }
  bool hot = gps_fix_ms && ms - gps_fix_ms < 4 * 3600000UL;
  if (ms - gps_start > (gps_req ? GPS_REQ_MS : hot ? GPS_HOT_MS : GPS_COLD_MS)) {
    if (Serial) Serial.println("GPS: no fix, retry later");
    gpsIdle(GPS_RETRY_SECS);
  }
}

// ---- power off (G0 held 3 s, or an empty battery) ----
// Deep sleep with everything else off; G0 turns it back on (a normal boot; the clock survives). The GPS can't be
// switched off, only put in standby for up to 6 h, so a timer wakes the ESP32 just after it, sends it back to sleep
// and goes straight back to deep sleep.
RTC_DATA_ATTR static uint8_t soft_off;   // survives deep sleep: "off", as opposed to a normal reset

static void gpsSleepLong() {
  gpsOutput(false);                       // wakes it if it's in standby: commands right after waking get lost
  uint32_t t = millis();
  while (millis() - t < 1200) { while (Serial1.available()) Serial1.read(); delay(10); }
  gpsStandby(GPS_CLOCK_SECS);
  Serial1.flush();
}

static void deepSleep() {
  soft_off = 1;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);                          // G0 pressed
  esp_sleep_enable_timer_wakeup((GPS_CLOCK_SECS + 60) * 1000000ULL);   // just after the GPS wakes itself
  esp_deep_sleep_start();
}

void powerOff() {
  storeFlush();
  if (radio_ok) radio.sleep(false);       // cold sleep, TCXO off
  digitalWrite(P_LORA_NSS, HIGH);         // keep the radio deselected while the ESP32 sleeps
  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
  gpsSleepLong();
  M5Cardputer.Speaker.end();
  M5Cardputer.Display.setBrightness(0);
  M5Cardputer.Display.sleep();
  while (digitalRead(GPIO_NUM_0) == LOW) delay(10);   // still holding G0: it would wake at once
  delay(50);
  if (Serial) { Serial.println("power off"); Serial.flush(); }
  deepSleep();
}

void setup() {
  if (esp_reset_reason() == ESP_RST_DEEPSLEEP && soft_off) {
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {   // still off: put the GPS back to sleep
      Serial1.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
      gpsSleepLong();
      deepSleep();
    }
    soft_off = 0;   // G0: turn on
  }
  rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
  Serial.begin(115200);
  board.begin();
  // the splash uses the saved colour scheme; only the first boot (formatting, ~22 s) shows it before prefs exist
  bool fs_ok = SPIFFS.begin(false);
  if (fs_ok) prefsLoad();
  themeApply();
  uiSplash();
  if (!fs_ok) {
    SPIFFS.begin(true);   // formats the data partition
    prefsLoad();
  }

  radio_ok = radio_init();
  fast_rng.begin(radio_ok ? radio_driver.getRngSeed() : esp_random());

  gpsBegin();
  gpsApply();
  int mv = board.getBattMilliVolts();   // turned on with an empty battery (under 2.5 V = no battery reading)
  if (mv > 2500 && mv < BATT_EMPTY_MV + 50) powerOffScreen(true);
  store.begin();
  the_mesh.begin();
  migrateOldData();
  msgsLoad();
  quickLoad();

  // as MeshCore does on boards with a screen: a random PIN each boot (shown in settings), unless the app set one
  if (client.node().ble_pin == 0) the_mesh.setBLEPin(fast_rng.nextInt(100000, 999999));
  else the_mesh.setBLEPin(client.node().ble_pin);
  bluetooth_interface.begin(BLE_NAME_PREFIX, client.node().node_name, the_mesh.getBLEPin());
  interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
  the_mesh.startInterface(interface_manager);
  bluetoothApply();
  the_mesh.setListener(&client);

  uiBegin();
  Serial.printf("MeshCore Cardputer v%s, boot ok, free heap %u\n", FW_VERSION, ESP.getFreeHeap());
}

void loop() {
  uint32_t loop_start = millis();
  // Nothing is received or transmitted until onboarding has picked a legal region.
  if (radio_ok && prefs.onboarded) {
    the_mesh.loop();
    interface_manager.loop();
    client.loop();
  }

  static uint32_t next_log = 0;
  if ((int32_t)(millis() - next_log) >= 0) {   // health line for `pio device monitor`
    next_log = millis() + 10000;
    if (radio_ok) client.logStatus();
  }
  rtc_clock.tick();

  gpsLoop();

  storeLoop();
  bool display_off = uiLoop();
  loop_max_ms = max(loop_max_ms, (uint32_t)(millis() - loop_start));
  delay(display_off ? 20 : 5);
}
