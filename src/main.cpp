#include <Arduino.h>
#include <SPIFFS.h>
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

// GPS clock (Settings > GPS). The module has no power switch, so whenever it isn't needed it sits in standby,
// which saves ~30 mA (a fifth of the idle draw). With the setting on it streams only RMC (time + fix flag) until
// two valid fixes a second apart agree, sets the clock, then sleeps 6 h; waking up by itself starts the re-sync.
// Any command wakes it early, so each wake-up (its start-up banner) re-sends what the current state wants.
// GPS time beats the phone app and the mesh.
#define GPS_SLEEP_SECS 21600   // 6 h; the RTC drifts ~0.1 s an hour
uint8_t gps_state = GPS_OFF;
static uint32_t gps_fix_at, gps_last;

static void gpsSleep() {
  gpsOutput(false);
  gpsStandby(GPS_SLEEP_SECS);
}

void gpsApply() {
  gps_state = prefs.gps ? GPS_SEARCH : GPS_OFF;
  gps_last = 0;
  if (prefs.gps) gpsOutput(true);   // wakes it if asleep; the banner that follows re-sends this
  else gpsSleep();
}

static void gpsLoop() {
  bool woke;
  uint32_t t = gpsReadTime(&woke);   // also drains anything the module sent
  if (woke) {
    if (Serial) Serial.println("GPS awake");
    if (gps_state == GPS_FIX) gps_state = GPS_SEARCH;   // the 6 h standby ran out: re-sync
    gps_last = 0;
    if (gps_state == GPS_OFF) gpsSleep(); else gpsOutput(true);
  } else if (gps_state == GPS_SEARCH && t >= CLOCK_SANE_MIN) {
    if (gps_last && t - gps_last >= 1 && t - gps_last <= 2) {
      client.setClock(t, CLOCK_GPS);
      if (Serial) Serial.printf("GPS clock set: %u\n", (unsigned)t);
      gpsSleep();
      gps_state = GPS_FIX;
      gps_fix_at = millis();
    }
    gps_last = t;
  } else if (gps_state == GPS_FIX && millis() - gps_fix_at > (GPS_SLEEP_SECS + 600) * 1000UL) {
    gpsApply();   // never heard it wake: try again
  }
}

void setup() {
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
