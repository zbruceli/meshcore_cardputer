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
  Serial.printf("boot ok, free heap %u\n", ESP.getFreeHeap());
}

void loop() {
  // Nothing is received or transmitted until onboarding has picked a legal region.
  if (radio_ok && prefs.onboarded) the_mesh.loop();
  rtc_clock.tick();

  // GPS time beats everything else; re-apply it at most hourly (the RTC drifts slowly)
  static uint32_t gps_set_at = 0;
  uint32_t gps_time = gpsReadTime();
  if (gps_time && (the_mesh.clockSource() != CLOCK_GPS || millis() - gps_set_at > 3600000UL)) {
    the_mesh.setClock(gps_time, CLOCK_GPS);
    gps_set_at = millis();
  }

  storeLoop();
  uiLoop();
}
