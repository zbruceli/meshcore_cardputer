#include <Arduino.h>
#include <M5Cardputer.h>
#include <utility/PI4IOE5V6408_Class.hpp>
#include "target.h"

CardputerBoard board;

static SPIClass spi(FSPI);
CustomSX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
CustomSX1262Wrapper radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

void CardputerBoard::begin() {
  ESP32Board::begin();   // Wire on the Grove port (PIN_BOARD_SDA/SCL), battery ADC

  pinMode(PIN_SD_CS, OUTPUT);       // SD card shares the LoRa SPI bus: keep it deselected
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(46, OUTPUT);              // Cap power enable, as in the prior Cardputer-ADV port (verify on hw)
  digitalWrite(46, HIGH);
  delay(50);

  auto cfg = M5.config();
  cfg.internal_imu = false;
  cfg.internal_mic = false;
  M5Cardputer.begin(cfg, true);

  // Cap LoRa-1262: the antenna RF switch is powered from P0 of the PI4IOE5V6408 (0x43, internal I2C)
  m5::PI4IOE5V6408_Class io;
  if (io.begin()) {
    io.setDirection(0, true);
    io.setHighImpedance(0, false);
    io.digitalWrite(0, true);
  } else {
    Serial.println("WARN: Cap LoRa I/O expander not found");
  }
}

uint8_t CardputerBoard::battPercent() {
  int mv = getBattMilliVolts();
  int pct = (mv - 3300) * 100 / (4150 - 3300);
  return constrain(pct, 0, 100);
}

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  return radio.std_init(&spi);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
