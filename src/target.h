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

// MeshCore's SX1262 wrapper, plus the 16-symbol preamble and hooks so the device UI sees every packet in and out
// (the official companion engine only forwards raw packets to the phone app).
class CardputerRadio : public CustomSX1262Wrapper {
public:
  CardputerRadio(CustomSX1262& radio, mesh::MainBoard& board) : CustomSX1262Wrapper(radio, board) { }
  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override;
  int recvRaw(uint8_t* bytes, int sz) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
};
void onRadioRx(const uint8_t* raw, int len, float snr);   // implemented by the chat client
void onRadioTx(const uint8_t* raw, int len);

extern CardputerBoard board;
extern CustomSX1262 radio;
extern CardputerRadio radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
void gpsBegin();
void gpsOutput(bool rmc);   // true: RMC once a second, false: GPS module silent
uint32_t gpsReadTime();   // UTC epoch once a valid RMC sentence (fix) arrives, else 0. Position is ignored for now.
mesh::LocalIdentity radio_new_identity();
