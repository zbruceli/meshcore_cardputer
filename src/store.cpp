#include <SPIFFS.h>
#include "store.h"

#define LAZY_SAVE_MILLIS  5000

Prefs prefs;
uint32_t msg_seq = 0;
uint32_t msg_added = 0;

static Msg msgs[MSG_LOG_SIZE];   // oldest first
static int msg_count = 0;
static uint32_t msgs_save_at = 0;

static bool readFile(const char* path, void* dest, size_t len) {
  File f = SPIFFS.open(path, "r");
  if (!f) return false;
  bool ok = f.read((uint8_t*)dest, len) == len;
  f.close();
  return ok;
}

static void writeFile(const char* path, const void* src, size_t len) {
  File f = SPIFFS.open(path, "w", true);
  if (!f) return;
  f.write((const uint8_t*)src, len);
  f.close();
}

// ---- prefs ----

void prefsDefaults() {
  memset(&prefs, 0, sizeof(prefs));
  prefs.version = PREFS_VERSION;
  prefs.region = REGION_CUSTOM;
  prefs.path_hash_size = 1;
  prefs.freq = LORA_FREQ;
  prefs.bw = LORA_BW;
  prefs.sf = LORA_SF;
  prefs.cr = LORA_CR;
  prefs.tx_power = LORA_TX_POWER;
  prefs.brightness = 2;
  prefs.screen_off = 1;
  prefs.beep = 1;
  prefs.auto_add = (1 << ADV_TYPE_CHAT) | (1 << ADV_TYPE_REPEATER);
  prefs.overwrite = 1;
}

void prefsLoad() {
  prefsDefaults();
  File f = SPIFFS.open("/prefs", "r");
  if (!f) return;
  f.read((uint8_t*)&prefs, sizeof(prefs));   // a shorter (older) file leaves the newer fields at their defaults
  f.close();
  if (prefs.version != PREFS_VERSION) prefsDefaults();
}

void prefsSave() { writeFile("/prefs", &prefs, sizeof(prefs)); }

// ---- contacts ----

struct ContactRec {   // on-flash form of ContactInfo (minus the cached shared secret)
  uint8_t  pub_key[PUB_KEY_SIZE];
  char     name[32];
  uint8_t  type, flags, out_path_len;
  uint8_t  out_path[MAX_PATH_SIZE];
  uint32_t last_advert_timestamp, lastmod;
  int32_t  gps_lat, gps_lon;
};

bool contactsLoad(BaseChatMesh& mesh) {
  File f = SPIFFS.open("/contacts", "r");
  if (!f) return false;
  ContactRec r;
  while (f.read((uint8_t*)&r, sizeof(r)) == sizeof(r)) {
    ContactInfo c;
    memset(&c, 0, sizeof(c));
    c.id = mesh::Identity(r.pub_key);
    memcpy(c.name, r.name, sizeof(c.name));
    c.type = r.type;
    c.flags = r.flags;
    c.out_path_len = r.out_path_len;
    memcpy(c.out_path, r.out_path, sizeof(c.out_path));
    c.last_advert_timestamp = r.last_advert_timestamp;
    c.lastmod = r.lastmod;
    c.gps_lat = r.gps_lat;
    c.gps_lon = r.gps_lon;
    if (!mesh.addContact(c)) break;
  }
  f.close();
  return true;
}

// ---- channels ----

bool channelsLoad(BaseChatMesh& mesh) {
  ChannelDetails chs[MAX_GROUP_CHANNELS];
  if (!readFile("/channels", chs, sizeof(chs))) return false;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) mesh.setChannel(i, chs[i]);
  return true;
}

// ---- message log ----

int msgCount() { return msg_count; }

Msg* msgAt(int i) { return &msgs[i]; }

static void msgRemove(int i) {
  memmove(&msgs[i], &msgs[i + 1], (msg_count - i - 1) * sizeof(Msg));
  msg_count--;
}

Msg* msgAdd() {
  if (msg_count == MSG_LOG_SIZE) {
    // evict the oldest message of the conversation with the most messages,
    // so a busy Public channel can't push out a quiet DM thread
    int victim = 0, most = 0;
    for (int i = 0; i < msg_count; i++) {
      int n = 0;
      for (int j = i; j < msg_count; j++) n += memcmp(msgs[j].conv, msgs[i].conv, CONV_ID_LEN) == 0;
      if (n > most) { most = n; victim = i; }
    }
    msgRemove(victim);
  }
  Msg* m = &msgs[msg_count++];
  memset(m, 0, sizeof(*m));
  msg_seq++;
  msg_added++;
  msgsChanged();
  return m;
}

void msgsChanged() { msgs_save_at = millis() + LAZY_SAVE_MILLIS; }

void msgsDeleteConv(const uint8_t conv[]) {
  for (int i = msg_count - 1; i >= 0; i--) {
    if (memcmp(msgs[i].conv, conv, CONV_ID_LEN) == 0) msgRemove(i);
  }
  msg_seq++;
  msgsChanged();
}

void msgsLoad() {
  File f = SPIFFS.open("/msgs", "r");
  if (!f) return;
  while (msg_count < MSG_LOG_SIZE && f.read((uint8_t*)&msgs[msg_count], sizeof(Msg)) == sizeof(Msg)) {
    Msg& m = msgs[msg_count++];
    if (m.status == ST_SENDING) m.status = ST_FAILED;   // retries don't survive a reboot
  }
  f.close();
}

static void msgsSave() {
  File f = SPIFFS.open("/msgs", "w", true);
  if (!f) return;
  for (int i = 0; i < msg_count; i++) f.write((uint8_t*)msgAt(i), sizeof(Msg));
  f.close();
}

// ---- housekeeping ----

void storeLoop() {
  uint32_t now = millis();
  if (msgs_save_at && (int32_t)(now - msgs_save_at) >= 0) {
    msgs_save_at = 0;
    msgsSave();
  }
}

void storeWipe() {
  SPIFFS.format();
  ESP.restart();
}
