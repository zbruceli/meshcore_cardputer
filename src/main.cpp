#include <Arduino.h>
#include <SPIFFS.h>
#include <M5Cardputer.h>
#include <helpers/ArduinoHelpers.h>
#include "target.h"
#include "chat_mesh.h"
#include "ui.h"

StdRNG fast_rng;
SimpleMeshTables tables;
ChatMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables);

bool radio_ok = false;

void setup() {
  Serial.begin(115200);
  board.begin();
  SPIFFS.begin(true);
  prefsLoad();

  radio_ok = radio_init();
  if (radio_ok) fast_rng.begin(radio_driver.getRngSeed());

  gpsBegin();
  the_mesh.begin();
  msgsLoad();
  if (radio_ok) the_mesh.applyRadio();

  uiBegin();
  Serial.printf("MeshCore Cardputer v%s, boot ok, free heap %u\n", FW_VERSION, ESP.getFreeHeap());
}

void loop() {
  // Nothing is received or transmitted until onboarding has picked a legal region.
  if (radio_ok && prefs.onboarded) the_mesh.loop();

  static uint32_t next_log = 0;
  if ((int32_t)(millis() - next_log) >= 0) {   // health line for `pio device monitor`
    next_log = millis() + 10000;
    if (radio_ok) the_mesh.logStatus();
    ioexDiag();
  }
  rtc_clock.tick();

  // GPS time beats everything else. Once set, the GPS is silenced (its NMEA stream is suspected of hurting the
  // radio) and woken hourly to re-sync (the RTC drifts slowly). Inactive while gpsBegin() keeps the GPS silent.
  static uint32_t gps_set_at = 0;
  static bool gps_on = true;
  uint32_t gps_time = gpsReadTime();
  if (gps_time && gps_on) {
    the_mesh.setClock(gps_time, CLOCK_GPS);
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
