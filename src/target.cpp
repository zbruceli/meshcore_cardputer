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

// ---- GPS: parse NMEA RMC for date + time only ----

void gpsBegin() {
  Serial1.setRxBufferSize(1024);   // 115200 baud and loop() may be busy drawing for a few ms
  Serial1.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
}

static bool nmeaChecksumOk(const char* s) {
  const char* star = strchr(s, '*');
  if (s[0] != '$' || !star) return false;
  uint8_t sum = 0;
  for (const char* p = s + 1; p < star; p++) sum ^= *p;
  return sum == strtoul(star + 1, NULL, 16);
}

// "$GNRMC,hhmmss.ss,A,lat,N,lon,E,speed,course,ddmmyy,..." -> UTC epoch, 0 unless status is A (valid fix)
static uint32_t parseRmc(const char* s) {
  if (strncmp(s + 3, "RMC,", 4) != 0 || !nmeaChecksumOk(s)) return 0;
  const char* f[10];
  int n = 0;
  for (const char* p = s; *p && n < 10; p++) if (*p == ',') f[n++] = p + 1;
  if (n < 10 || f[1][0] != 'A') return 0;
  int hh, mi, ss, dd, mo, yy;
  if (sscanf(f[0], "%2d%2d%2d", &hh, &mi, &ss) != 3 || sscanf(f[8], "%2d%2d%2d", &dd, &mo, &yy) != 3) return 0;
  struct tm t = {};
  t.tm_year = yy + 100;   // years since 1900
  t.tm_mon = mo - 1;
  t.tm_mday = dd;
  t.tm_hour = hh;
  t.tm_min = mi;
  t.tm_sec = ss;
  return mktime(&t);      // no TZ is set, so this is UTC
}

uint32_t gpsReadTime() {
  static char line[96];
  static int len = 0;
  uint32_t found = 0;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      line[len] = 0;
      if (len > 0) { uint32_t t = parseRmc(line); if (t) found = t; }
      len = 0;
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = c;
    }
  }
  return found;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
