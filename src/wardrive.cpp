#include <SD.h>
#include "wardrive.h"
#include "chat_client.h"
#include "target.h"
#include "ui.h"

Wardrive wd;

// MeshMapper's batching: per repeater keep the best SNR, at the position where it was first heard; close the batch
// after 30 s or 25 m. Lines collect in RAM and go to the card every 30 s (one short append, then the file is closed,
// so pulling the card or the power loses at most that much).
#define WD_BATCH      8
#define WD_BATCH_MS   30000UL
#define WD_BATCH_M    25.0f
#define WD_WRITE_MS   30000UL
#define WD_FIX_MS     5000UL     // position older than this: don't log
#define WD_FIX_ACC    50         // m: worse than this: don't log
#define WD_MIN_RSSI   -30        // stronger: a radio in the same car, not coverage (MeshMapper's filter)
#define WD_RECENT_MS  60000UL
#define WD_PING_MS    60000UL    // ping: at most once a minute...
#define WD_PING_M     100.0f     // ... and only after moving this far
#define WD_PING_WAIT  8000UL     // replies to a zero-hop discovery arrive within a few seconds
#define WD_DIR        "/meshcore"
// Time is UTC (Unix seconds). Status as MeshMapper's CSV import: 5 = heard (RX), 6 = answered our ping (DISC),
// 0 = ping not answered. SNR = how we heard the repeater, RemoteSNR = how it heard us (pings only).
#define WD_HEADER "Time,Latitude,Longitude,Status,Repeater,Name,SNR,RSSI,RemoteSNR,Packet,Accuracy,RptLat,RptLon\n"
enum { ST_DROP = 0, ST_RX = 5, ST_DISC = 6 };

struct WdSample { uint8_t hash[3], len, type; int8_t snr4, rssi; double lat, lon; uint32_t t; uint16_t acc; };
static WdSample batch[WD_BATCH];
static int nbatch;
static uint32_t batch_ms;
static double batch_lat, batch_lon;
static char buf[2048];
static int buf_len;
static bool header_done;
static uint32_t write_ms;
static double ping_lat, ping_lon;
static uint32_t ping_t;
static uint16_t ping_acc;

static float distM(double lat1, double lon1, double lat2, double lon2) {   // short distances: flat is fine
  double x = (lon2 - lon1) * cos((lat1 + lat2) / 2 * DEG_TO_RAD), y = lat2 - lat1;
  return sqrt(x * x + y * y) * 111320.0;
}

static uint32_t fixTime() { return gps_pos.at + (millis() - gps_pos_ms) / 1000; }

bool wdFixOk() {
  return gps_pos.at && millis() - gps_pos_ms < WD_FIX_MS && gps_pos.acc_m <= WD_FIX_ACC;
}

float snrFloor(uint8_t sf) { return -7.5f - 2.5f * (sf - 7); }   // SX126x: SF7 -7.5 dB ... SF12 -20 dB

static void fail(const char* why) {   // the card went away or is full: stop, keep what's on it
  snprintf(wd.err, sizeof(wd.err), "%s", why);
  buf_len = nbatch = 0;
  wd.on = false;
  sdUnmount();
  gpsTrack(false);
  toast("SD write failed: log stopped");
}

static bool writeOut() {
  if (!buf_len) return true;
  File f = SD.open(wd.file, FILE_APPEND);
  if (!f) { fail("SD write failed"); return false; }
  if (!header_done) header_done = f.print(WD_HEADER) > 0;
  size_t n = f.write((const uint8_t*)buf, buf_len);
  f.close();
  if (n != (size_t)buf_len) { fail("SD full or removed"); return false; }
  buf_len = 0;
  return true;
}

static void nameFile(uint32_t t) {   // after the local start time
  time_t lt = t + prefs.tz_hours * 3600;
  struct tm tm;
  gmtime_r(&lt, &tm);
  snprintf(wd.file, sizeof(wd.file), WD_DIR "/wd-%04d%02d%02d-%02d%02d%02d.csv", tm.tm_year + 1900,
           tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void addLine(uint32_t t, double lat, double lon, uint16_t acc, int status, const uint8_t* hash, int len,
                    int type, int snr4, int rssi, int remote4) {
  if (!wd.file[0]) nameFile(t);   // the clock wasn't set at the start: name it after the first record
  char id[8] = "", name[33] = "", snr[8] = "", rs[6] = "", rem[8] = "", rlat[12] = "", rlon[12] = "";
  for (int i = 0; i < len; i++) sprintf(id + 2 * i, "%02x", hash[i]);
  ContactInfo* c = len ? the_mesh.lookupContactByPubKey(hash, len) : NULL;   // a 1-byte id may be a lookalike
  if (c) {
    snprintf(name, sizeof(name), "%s", c->name);
    for (char* p = name; *p; p++) if (*p == ',' || *p == '"' || (uint8_t)*p < ' ') *p = ' ';
    if (c->gps_lat || c->gps_lon) {
      snprintf(rlat, sizeof(rlat), "%.6f", c->gps_lat / 1e6);
      snprintf(rlon, sizeof(rlon), "%.6f", c->gps_lon / 1e6);
    }
  }
  if (snr4 != -128) snprintf(snr, sizeof(snr), "%.2f", snr4 / 4.0f);
  if (rssi) snprintf(rs, sizeof(rs), "%d", rssi);
  if (remote4 != -128) snprintf(rem, sizeof(rem), "%.2f", remote4 / 4.0f);
  char line[200];
  int n = snprintf(line, sizeof(line), "%u,%.6f,%.6f,%d,%s,%s,%s,%s,%s,%s,%u,%s,%s\n", (unsigned)t, lat, lon, status,
                   id, name, snr, rs, rem, type >= 0 ? PKT_TYPES[type & 0x0F] : "", acc, rlat, rlon);
  n = min(n, (int)sizeof(line) - 1);
  if (buf_len + n > (int)sizeof(buf) && !writeOut()) return;
  memcpy(buf + buf_len, line, n);
  buf_len += n;
  wd.records++;
}

static void flushBatch() {
  for (int i = 0; i < nbatch; i++) {
    WdSample& s = batch[i];
    addLine(s.t, s.lat, s.lon, s.acc, ST_RX, s.hash, s.len, s.type, s.snr4, s.rssi, -128);
    if (!wd.on) return;
  }
  nbatch = 0;
}

static void noteRecent(const uint8_t* hash, int len, int8_t snr4) {
  uint32_t ms = millis();
  WdHeard* r = NULL;
  for (int i = 0; i < WD_RECENT && !r; i++)
    if (wd.recent[i].len == len && memcmp(wd.recent[i].hash, hash, len) == 0) r = &wd.recent[i];
  if (r && ms - r->ms < WD_RECENT_MS && r->snr4 > snr4) { r->ms = ms; return; }   // keep the minute's best
  if (!r) {   // a free slot, else the oldest
    r = &wd.recent[0];
    for (int i = 1; i < WD_RECENT; i++) if (!wd.recent[i].len || (r->len && wd.recent[i].ms < r->ms)) r = &wd.recent[i];
  }
  memcpy(r->hash, hash, len);
  r->len = len;
  r->snr4 = snr4;
  r->ms = ms;
}

void wdPacket(const mesh::Packet& pkt, float snr, float rssi) {
  int hops = pkt.getPathHashCount();
  if (!wd.on || !pkt.isRouteFlood() || hops == 0 || rssi > WD_MIN_RSSI) return;
  int hs = pkt.getPathHashSize();
  const uint8_t* hash = &pkt.path[(hops - 1) * hs];   // the repeater that sent it to us
  hs = min(hs, 3);
  int8_t snr4 = constrain((int)(snr * 4), -128, 127);
  noteRecent(hash, hs, snr4);
  if (!wdFixOk()) return;
  for (int i = 0; i < nbatch; i++) {
    WdSample& s = batch[i];
    if (s.len == hs && memcmp(s.hash, hash, hs) == 0) {
      if (snr4 > s.snr4) { s.snr4 = snr4; s.rssi = (int8_t)rssi; s.type = pkt.getPayloadType(); }
      return;
    }
  }
  if (nbatch == WD_BATCH) flushBatch();
  if (!wd.on) return;
  if (nbatch == 0) { batch_ms = millis(); batch_lat = gps_pos.lat; batch_lon = gps_pos.lon; }
  WdSample& s = batch[nbatch++];
  memcpy(s.hash, hash, hs);
  s.len = hs;
  s.type = pkt.getPayloadType();
  s.snr4 = snr4;
  s.rssi = (int8_t)constrain((int)rssi, -128, 0);
  s.lat = gps_pos.lat;
  s.lon = gps_pos.lon;
  s.t = fixTime();
  s.acc = gps_pos.acc_m;
}

static void collectPing() {   // replies to our discovery, or one "not answered" line
  int n = client.num_discovered;
  for (int i = 0; i < n && wd.on; i++) {
    Discovered& d = client.discovered[i];
    addLine(ping_t, ping_lat, ping_lon, ping_acc, ST_DISC, d.pub_key, 3, -1, d.snr_in4, 0, d.snr_out4);
    noteRecent(d.pub_key, 1, d.snr_in4);   // 1 byte: how flood paths name it by default
  }
  if (n == 0) addLine(ping_t, ping_lat, ping_lon, ping_acc, ST_DROP, NULL, 0, -1, -128, 0, -128);
  wd.ping_found = n;
}

bool wdStart() {
  if (wd.on) return true;
  wd.err[0] = 0;
  if (!sdMount()) { snprintf(wd.err, sizeof(wd.err), "No SD card (FAT32 needed)"); return false; }
  if (!SD.exists(WD_DIR) && !SD.mkdir(WD_DIR)) {
    sdUnmount();
    snprintf(wd.err, sizeof(wd.err), "SD card not writable");
    return false;
  }
  header_done = false;
  wd.file[0] = 0;
  if (client.clockSynced()) {   // create the file now: proves the card takes writes, and a quiet run still has one
    nameFile(client.now());
    File f = SD.open(wd.file, FILE_APPEND);
    header_done = f && f.print(WD_HEADER) > 0;
    if (f) f.close();
    if (!header_done) {
      sdUnmount();
      snprintf(wd.err, sizeof(wd.err), "SD card not writable");
      wd.file[0] = 0;
      return false;
    }
  }
  wd.on = true;
  wd.records = 0;
  wd.started_ms = write_ms = millis();
  wd.ping_ms = 0;
  wd.ping_found = -2;
  memset(wd.recent, 0, sizeof(wd.recent));
  nbatch = buf_len = 0;
  gpsTrack(true);
  return true;
}

void wdStop() {
  if (!wd.on) return;
  flushBatch();
  if (wd.on && wd.ping_found == -1) collectPing();
  if (wd.on && writeOut()) {
    wd.on = false;
    sdUnmount();
    gpsTrack(false);
  }
}

void wdLoop() {
  if (!wd.on) return;
  uint32_t ms = millis();
  if (nbatch && (ms - batch_ms >= WD_BATCH_MS ||
                 (wdFixOk() && distM(batch_lat, batch_lon, gps_pos.lat, gps_pos.lon) >= WD_BATCH_M))) flushBatch();
  if (wd.on && wd.ping_found == -1) {
    if (ms - wd.ping_ms >= WD_PING_WAIT) collectPing();
  } else if (wd.on && wd.ping && wdFixOk() && client.testRunning() < 0 && !client.discovering() &&
             (!wd.ping_ms || (ms - wd.ping_ms >= WD_PING_MS &&
                              distM(ping_lat, ping_lon, gps_pos.lat, gps_pos.lon) >= WD_PING_M))) {
    client.startDiscovery();
    wd.ping_ms = ms;
    wd.ping_found = -1;
    ping_lat = gps_pos.lat;
    ping_lon = gps_pos.lon;
    ping_t = fixTime();
    ping_acc = gps_pos.acc_m;
  }
  if (wd.on && buf_len && ms - write_ms >= WD_WRITE_MS) {
    write_ms = ms;
    writeOut();
  }
}
