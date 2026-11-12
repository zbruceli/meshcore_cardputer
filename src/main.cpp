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

void setup() {
  Serial.begin(115200);
  board.begin();
  uiSplash();
  SPIFFS.begin(true);   // formats the data partition on first boot (several seconds)
  prefsLoad();

  radio_ok = radio_init();
  fast_rng.begin(radio_ok ? radio_driver.getRngSeed() : esp_random());

  gpsBegin();
  store.begin();
  the_mesh.begin();
  migrateOldData();
  msgsLoad();

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

  // GPS time beats the mesh. Once set, the GPS is silenced (its NMEA stream is suspected of hurting the radio)
  // and woken hourly to re-sync (the RTC drifts slowly). Inactive while gpsBegin() keeps the GPS silent.
  static uint32_t gps_set_at = 0;
  static bool gps_on = true;
  uint32_t gps_time = gpsReadTime();
  if (gps_time && gps_on) {
    client.setClock(gps_time, CLOCK_GPS);
    gps_set_at = millis();
    gpsOutput(false);
    gps_on = false;
  } else if (!gps_on && millis() - gps_set_at > 3600000UL) {
    gpsOutput(true);
    gps_on = true;
  }

  storeLoop();
  uiLoop();
}
