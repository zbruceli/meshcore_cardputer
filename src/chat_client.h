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

#define MAX_SENT_ACKS     16   // expected ACKs of recent DM attempts (each attempt has its own)
struct SentAck { uint32_t ack, sender_ts; uint8_t conv[CONV_ID_LEN]; };

#define MAX_RPT_SNR       32   // last SNR of packets repeaters sent us directly (keyed by path hash)
struct RptSnr { uint8_t hash[3]; uint8_t len; int8_t snr4; uint32_t at; };

struct Discovered { uint8_t pub_key[PUB_KEY_SIZE]; int8_t snr_in4, snr_out4; };   // SNR x4

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
  int32_t clock_votes[4];   // (heard timestamp - uptime secs) of recent adverts/messages
  uint8_t vote_src[4];      // who sent each one (pubkey byte / sender name hash)
  int num_votes = 0;

  void noteRepeaterSnr(const uint8_t* hash, uint8_t len, float snr);
  void clockVote(uint32_t ts, uint8_t src);
  void sendDMAttempt(Msg& m, ContactInfo& to);
  void checkDMRetries();

public:
  // MyMesh::Listener
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp,
                     const char* text) override;
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel, const char* text) override;
  void onQueueSizeChanged(int msgcount) override { }
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onControlDataRecv(const mesh::Packet* pkt) override;
  void onACKRecv(uint32_t ack_crc) override;

  void packetIn(const uint8_t* raw, int len, float snr);   // every packet received / sent (radio hooks)
  void packetOut(const uint8_t* raw, int len);

  void loop();
  void logStatus();   // one serial line: radio counters, heap

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

extern ChatClient client;
extern DataStore store;
extern MultiSerialInterface interface_manager;   // the phone app link (Bluetooth)
void bluetoothApply();   // on/off from prefs (main.cpp)
