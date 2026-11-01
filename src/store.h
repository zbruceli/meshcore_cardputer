#pragma once
// Persistent state: prefs, contacts, message log. Flash FS = SPIFFS (same as MeshCore's ESP32 examples).

#include <Arduino.h>
#include <helpers/BaseChatMesh.h>

#define PREFS_VERSION   1
#define REGION_CUSTOM   0xFF

struct Prefs {
  uint8_t version;
  uint8_t onboarded;        // 0 = never transmit, show onboarding
  uint8_t region;           // index into PRESETS, or REGION_CUSTOM
  uint8_t path_hash_size;   // 1..3
  char    name[32];
  float   freq, bw;
  uint8_t sf, cr;
  int8_t  tx_power;
  uint8_t brightness;       // index into BRIGHTNESS_LEVELS
  uint8_t screen_off;       // index into SCREEN_OFF_SECS
  uint8_t beep;
  int8_t  tz_hours;
};

extern Prefs prefs;
void prefsDefaults();
void prefsLoad();
void prefsSave();

void contactsLoad(BaseChatMesh& mesh);
void contactsSave(BaseChatMesh& mesh);

// ---- message log (one ring for all conversations) ----

#define MSG_LOG_SIZE  96
#define CONV_ID_LEN   7       // [0] = CONV_*, [1..6] = contact pubkey prefix / channel secret prefix
enum { CONV_CHANNEL = 1, CONV_DM = 2 };
enum { ST_RECV = 0, ST_SENDING, ST_SENT, ST_ACKED, ST_FAILED };

struct Msg {
  uint32_t ts;          // our clock, for display
  uint32_t sender_ts;   // timestamp in the packet (kept across DM retries)
  uint32_t ack;         // expected ACK crc (outgoing DM)
  uint32_t deadline;    // millis() when the current DM attempt times out
  uint8_t  conv[CONV_ID_LEN];
  uint8_t  out;         // 1 = sent by us
  uint8_t  status;      // ST_*
  uint8_t  attempt;
  uint8_t  unread;
  char     text[MAX_TEXT_LEN + 1];   // channel msgs keep MeshCore's "sender: text" form
};

extern uint32_t msg_seq;    // bumps on any message change (UI redraws)
extern uint32_t msg_added;  // bumps on every new message (UI notifies)
int  msgCount();
Msg* msgAt(int i);          // 0 = oldest
Msg* msgAdd();              // overwrites the oldest when full
void msgsChanged();         // schedule a lazy save
void msgsLoad();

void storeLoop();           // performs lazy saves
void storeMarkContactsDirty();
void storeWipe();           // factory reset
