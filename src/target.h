#pragma once
// Hardware glue. MeshCore's ESP32Board.cpp includes <target.h> and expects
// board / radio_driver / sensors to be declared here.

#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/ESP32Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>

class CardputerBoard : public ESP32Board {
public:
  void begin();   // M5Cardputer (display, keyboard) + Cap LoRa RF switch
  const char* getManufacturerName() const override { return "M5Stack Cardputer-Adv"; }
  uint8_t battPercent();
};

extern CardputerBoard board;
extern CustomSX1262Wrapper radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
void gpsBegin();
uint32_t gpsReadTime();   // UTC epoch once a valid RMC sentence (fix) arrives, else 0. Position is ignored for now.
mesh::LocalIdentity radio_new_identity();
