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
// The phone app's GPS switch (Position Settings) reads and sets the custom var "gps" through the sensor manager, as
// on MeshCore boards with a GPS. Ours maps it onto SET > GPS: app off = off, app on = clock or location (main.cpp).
class CardputerSensors : public SensorManager {
public:
  int getNumSettings() const override { return 1; }
  const char* getSettingName(int i) const override { return i == 0 ? "gps" : NULL; }
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};

void onRadioRx(const uint8_t* raw, int len, float snr);   // implemented by the chat client
void onRadioTx(const uint8_t* raw, int len);

extern CardputerBoard board;
extern CustomSX1262 radio;
extern CardputerRadio radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern CardputerSensors sensors;

bool radio_init();
void gpsBegin();
void gpsOutput(bool on);   // true: GGA + RMC once a second, false: GPS module silent
void gpsStandby(uint32_t secs);   // receiver off (~30 mA saved) until the time is up or any command wakes it
struct GpsFix { uint32_t time; double lat, lon; float hdop; uint8_t sats; };
// Drains what the module sent. True when a valid fix (RMC status A) arrived: time = UTC epoch, hdop/sats from the
// latest GGA. woke = the module's start-up banner arrived (power-on, or out of standby).
bool gpsRead(GpsFix& fix, bool& woke);
uint8_t gpsSats();   // satellites used, from the latest GGA (progress while searching)
mesh::LocalIdentity radio_new_identity();
