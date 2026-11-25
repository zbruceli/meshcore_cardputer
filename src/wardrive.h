#pragma once
// Wardrive logger (test tab, LOG page): which repeaters are heard where, as CSV on the SD card.
// Passive by default: every flood packet that came through a repeater credits its last hop (the repeater we
// heard), as MeshMapper does. Optional "ping": a zero-hop discovery (no flood) at most once a minute and only
// after moving 100 m, to also learn how well repeaters hear us. The GPS runs for the whole session.

#include <Arduino.h>
#include <Packet.h>

#define WD_RECENT 8   // repeaters shown live (heard in the last minute)
struct WdHeard { uint8_t hash[3]; uint8_t len; int8_t snr4; uint32_t ms; };

struct Wardrive {
  bool on;
  bool ping;                 // zero-hop discovery while logging (off by default)
  uint32_t records;          // lines written this session
  uint32_t started_ms;
  uint32_t ping_ms;          // last ping (0 = none yet)
  int ping_found;            // repeaters that answered the last ping, -1 = waiting
  char file[40];             // "/meshcore/wd-YYYYMMDD-HHMMSS.csv" (32 chars), empty until named
  char err[32];              // why it stopped or didn't start
  WdHeard recent[WD_RECENT];
};
extern Wardrive wd;

bool wdStart();   // false (and wd.err) without a readable SD card
void wdStop();    // writes what's pending, unmounts the card
void wdLoop();
void wdPacket(const mesh::Packet& pkt, float snr, float rssi);   // every packet received
bool wdFixOk();   // a recent, accurate enough GPS position
float snrFloor(uint8_t sf);   // lowest SNR the spreading factor decodes (link margin = SNR - floor)
