#pragma once
// The device UI's side of the mesh. The mesh itself is MeshCore's official companion engine (`MyMesh`, which also
// serves the phone app over Bluetooth); this listens to it and adds what the screen needs: a message log, DM
// retries with ACK marks, repeater discovery, adverts heard but not added, clock votes and channel echo counts.

#include <Arduino.h>
#include <MyMesh.h>
#include <helpers/MultiSerialInterface.h>
#include "store.h"

#define DM_MAX_ATTEMPTS   3
#define MAX_HEARD         8    // recent adverts not added to contacts (auto-add off / contacts full)
#define MAX_DISCOVERED    16
#define DISCOVER_MILLIS   60000
enum { CLOCK_NONE, CLOCK_MESH, CLOCK_MANUAL, CLOCK_GPS, CLOCK_APP };
#define CLOCK_SANE_MIN    1767225600   // 1 Jan 2026: older times come from an unset clock

#define MAX_SENT_ACKS     16   // expected ACKs of recent DM attempts (each attempt has its own)
struct SentAck { uint32_t ack, sender_ts; uint8_t conv[CONV_ID_LEN]; };

#define MAX_RPT_SNR       32   // last SNR of packets repeaters sent us directly (keyed by path hash)
struct RptSnr { uint8_t hash[3]; uint8_t len; int8_t snr4; uint32_t at; };

// test tab: the last packets in and out, and counters since the last reset
#define PKT_LOG_SIZE      32
#define PKT_LOG_PATH      8    // path bytes kept per packet
struct PktLog {
  uint32_t at;              // our clock
  uint8_t  type;            // PAYLOAD_TYPE_*
  uint8_t  tx;              // 1 = sent by us (or the app)
  uint8_t  flood;           // route type flood (else direct)
  uint8_t  hops, hash_size, len;
  int8_t   snr4, rssi;      // received packets only
  uint8_t  path[PKT_LOG_PATH];
  uint8_t  key[4];          // advert: sender's public key prefix; else the first payload bytes (dest/src hashes)
  uint8_t  hash[4];         // packet hash (same at every hop)
};
struct RadioStats { uint32_t rx, rx_err, tx, rx_flood, rx_direct, tx_flood, tx_direct, tx_air, rx_air, at; };

struct Discovered { uint8_t pub_key[PUB_KEY_SIZE]; int8_t snr_in4, snr_out4; uint32_t at; };   // SNR x4

// test tab, part 2: tests that transmit. A shared mesh: few rounds, spaced out, nothing on Public.
enum { TEST_HEAR, TEST_ECHO, TEST_TRACE, TEST_SDR, TEST_COUNT };
enum { TS_NEVER, TS_RUNNING, TS_DONE, TS_STOPPED };
#define TRACE_MAX_HOPS 9      // out + target + back: up to 4 repeaters on the way
struct TestResult {
  uint8_t state;             // TS_*
  uint8_t round, rounds;     // sent so far / planned
  uint8_t ok;                // hear: rounds with a reply; echo: messages echoed; trace: 1 = came back
  uint8_t count;             // hear: distinct repeaters; echo: echoes in all
  int8_t best_in4, best_out4;   // hear: best SNR x4 (in: we hear them, out: they hear us)
};
struct TraceHop { uint8_t hash; int8_t snr4; };   // snr4 = how this node heard the previous one; -128 = no reply

// Auto-add choices, as the app's "Auto Add" settings (NodePrefs manual_add_contacts / autoadd_config)
enum { ADD_CHAT_RPT, ADD_ALL, ADD_CHAT, ADD_OFF, NUM_ADD_MODES };

inline void convOfContact(uint8_t conv[], const ContactInfo& c) {
  conv[0] = CONV_DM;
  memcpy(&conv[1], c.id.pub_key, CONV_ID_LEN - 1);
}
inline void convOfChannel(uint8_t conv[], const mesh::GroupChannel& ch) {
  conv[0] = CONV_CHANNEL;
  memcpy(&conv[1], ch.secret, CONV_ID_LEN - 1);
}

class ChatClient : public MyMesh::Listener {
  uint8_t clock_src = CLOCK_NONE;
  int32_t clock_offset = 0;   // RTC time - uptime secs, to notice the app setting the clock
  uint32_t disc_tag = 0, disc_until = 0;
  SentAck sent_acks[MAX_SENT_ACKS];
  int sent_ack_next = 0;
  // our latest channel message: its packet hash, to count repeaters echoing it (like the apps' "heard N repeats")
  uint8_t echo_hash[MAX_HASH_SIZE], echo_conv[CONV_ID_LEN];
  uint32_t echo_ts = 0;
  bool echo_wait_tx = false;
  RptSnr rpt_snr[MAX_RPT_SNR];
  int rpt_snr_next = 0;
  PktLog pkt_log[PKT_LOG_SIZE];
  uint32_t pkt_total = 0;
  RadioStats stats_base = {};
  uint32_t air_min[60];      // TX airtime total at each of the last 60 minutes (for the last-hour share)
  uint32_t air_min_count = 0, air_min_at = 0;
  int test_kind = -1;        // running test, -1 = none
  uint32_t test_next = 0;    // millis() of the next step
  int test_chan = 0;         // echo test: channel index
  uint32_t test_echo_ts = 0; // echo test: sender_ts of the latest test message
  uint8_t hear_keys[MAX_DISCOVERED][4];   // hear test: repeaters that replied in any round
  int hear_nkeys = 0;
  uint32_t trace_tag = 0, trace_until = 0;
  int32_t clock_votes[4];   // (heard timestamp - uptime secs) of recent adverts/messages
  uint8_t vote_src[4];      // who sent each one (pubkey byte / sender name hash)
  int num_votes = 0;

  void noteRepeaterSnr(const uint8_t* hash, uint8_t len, float snr);
  void logPacket(mesh::Packet& pkt, int len, bool tx, float snr);
  void clockVote(uint32_t ts, uint8_t src);
  void sendDMAttempt(Msg& m, ContactInfo& to);
  void checkDMRetries();
  void testTick();
  void testCollect();

public:
  // MyMesh::Listener
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp,
                     const char* text) override;
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel, const char* text) override;
  void onQueueSizeChanged(int msgcount) override { }
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onControlDataRecv(const mesh::Packet* pkt) override;
  void onACKRecv(uint32_t ack_crc) override;
  void onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags, const uint8_t* path_snrs,
                   const uint8_t* path_hashes, uint8_t path_len) override;

  void packetIn(const uint8_t* raw, int len, float snr);   // every packet received / sent (radio hooks)
  void packetOut(const uint8_t* raw, int len);

  void loop();
  void logStatus();   // one serial line: radio counters, heap

  // test tab
  uint32_t pktTotal() const { return pkt_total; }
  const PktLog* pktBySeq(uint32_t seq) const;   // NULL once overwritten
  RadioStats stats();    // counters since resetStats()
  void resetStats();
  uint32_t txAirLastHour(uint32_t& window_ms);
  int repeatersHeard();
  bool startTest(int kind, int chan = 0);    // TEST_HEAR / TEST_ECHO (chan = channel index) / TEST_SDR
  bool startTrace(const ContactInfo& c);     // to a repeater with a known path, and back
  void stopTest();
  int  testRunning() const { return test_kind; }
  int  testSecsToNext() const { return test_kind >= 0 ? max(0, (int32_t)(test_next - millis()) / 1000 + 1) : 0; }
  TestResult tests[TEST_COUNT] = {};
  TraceHop trace_hops[TRACE_MAX_HOPS];
  int trace_nhops = 0;
  int8_t trace_final4 = -128;                // how we heard the last hop

  // node settings, shared with the phone app
  NodePrefs& node() { return *the_mesh.getNodePrefs(); }
  void applyRadio();     // retune to node() settings
  void saveNode() { the_mesh.savePrefs(); }
  int  addMode();        // ADD_* from node()
  void setAddMode(int mode, bool overwrite);
  bool overwrite() { return node().autoadd_config & 0x01; }   // AUTO_ADD_OVERWRITE_OLDEST

  bool sendAdvert(bool flood);
  bool sendChannel(int channel_idx, const char* text);
  void sendDM(ContactInfo& to, const char* text);

  int  joinChannel(const char* name, const uint8_t* secret, int secret_len);   // index, or -1 if full
  void leaveChannel(int idx);

  void saveContacts();   // after changing a contact from the UI
  void deleteContact(ContactInfo& c);
  ContactInfo heard[MAX_HEARD];   // newest last
  int num_heard = 0;
  bool addHeard(int i);

  bool repeaterSnr(const uint8_t* pub_key, int8_t& snr4, uint32_t& at);   // last direct SNR, if heard this boot

  void startDiscovery();
  bool discovering() { return disc_tag && (int32_t)(millis() - disc_until) < 0; }
  bool discoveryRan() { return disc_tag != 0; }
  int  discoverSecsLeft() { return discovering() ? (int32_t)(disc_until - millis()) / 1000 + 1 : 0; }
  Discovered discovered[MAX_DISCOVERED];
  int num_discovered = 0;
  bool addDiscovered(int i);

  void setClock(uint32_t epoch, uint8_t src);
  bool clockSynced() const { return clock_src != CLOCK_NONE; }
  uint8_t clockSource() const { return clock_src; }
  ContactInfo* contactByConv(const uint8_t conv[]) { return the_mesh.lookupContactByPubKey(&conv[1], CONV_ID_LEN - 1); }
  int channelByConv(const uint8_t conv[]);
  const uint8_t* selfPubKey() const { return the_mesh.self_id.pub_key; }
  uint32_t now() { return the_mesh.getRTCClock()->getCurrentTime(); }
};

extern const char* const PKT_TYPES[16];   // payload type names
extern ChatClient client;
extern DataStore store;
extern MultiSerialInterface interface_manager;   // the phone app link (Bluetooth)
void bluetoothApply();   // on/off from prefs (main.cpp)
// GPS (main.cpp). prefs.gps: GPS_MODE_*. The receiver only runs for short sessions, in standby otherwise.
enum { GPS_MODE_OFF, GPS_MODE_CLOCK, GPS_MODE_LOC };
enum { GPS_OFF, GPS_SEARCH, GPS_FIX };   // standby / a session is running / standby until the next session
extern uint8_t gps_state;
struct GpsPos { double lat, lon; uint16_t acc_m; uint32_t at; };   // at = UTC of the fix, 0 = none yet
extern GpsPos gps_pos;
extern uint32_t gps_pos_ms;   // millis() when gps_pos was taken
extern bool gps_req;     // a position request (gpsRequest) is running
void gpsApply();         // mode changed in settings
void gpsRequest();       // get a fresh position now (unless the last one is under a minute old)
uint32_t gpsSearchSecs();
void gpsTrack(bool on);  // wardrive logger: GPS on with a fix every second, whatever the mode   // how long the current session has run
bool myPosition(double& lat, double& lon);   // GPS fix, else the location set from the app; false if none
