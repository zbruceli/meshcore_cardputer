#pragma once
// MeshCore chat client: BaseChatMesh + message log + DM retries. No companion/BLE protocol.

#include <Arduino.h>
#include <Mesh.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>
#include "store.h"

#define PUBLIC_GROUP_PSK  "izOH6cXN6mrJ5e26oRXNcg=="
#define DM_MAX_ATTEMPTS   3
#define MAX_HEARD         8    // recent adverts not added to contacts (auto-add off / contacts full)
#define MAX_DISCOVERED    16
#define DISCOVER_MILLIS   60000
enum { CLOCK_NONE, CLOCK_MESH, CLOCK_MANUAL, CLOCK_GPS };

#define CTL_TYPE_NODE_DISCOVER_REQ   0x80   // from MeshCore's companion_radio / simple_repeater
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90

struct Discovered { uint8_t pub_key[PUB_KEY_SIZE]; int8_t snr_in4, snr_out4; };   // SNR x4

inline void convOfContact(uint8_t conv[], const ContactInfo& c) {
  conv[0] = CONV_DM;
  memcpy(&conv[1], c.id.pub_key, CONV_ID_LEN - 1);
}
inline void convOfChannel(uint8_t conv[], const mesh::GroupChannel& ch) {
  conv[0] = CONV_CHANNEL;
  memcpy(&conv[1], ch.secret, CONV_ID_LEN - 1);
}

class ChatMesh : public BaseChatMesh {
  uint8_t clock_src = CLOCK_NONE;
  uint32_t disc_tag = 0, disc_until = 0;
  int32_t clock_votes[4];   // (heard timestamp - uptime secs) of recent adverts/messages
  uint8_t vote_src[4];      // who sent each one (pubkey byte / sender name hash)
  int num_votes = 0;

  void clockVote(uint32_t ts, uint8_t src);

  void sendDMAttempt(Msg& m, ContactInfo& to);
  void checkDMRetries();

protected:
  float getAirtimeBudgetFactor() const override { return 1.0f; }
  int calcRxDelay(float score, uint32_t air_time) const override { return 0; }
  bool allowPacketForward(const mesh::Packet* packet) override { return false; }   // client, not a repeater
  bool shouldAutoAddContactType(uint8_t type) const override { return prefs.auto_add & (1 << type); }
  bool shouldOverwriteWhenFull() const override { return prefs.overwrite; }
  void onContactOverwrite(const uint8_t* pub_key) override;
  int  getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override;
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) override;
  void onControlDataRecv(mesh::Packet* packet) override;

  void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) override;
  void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) override;

  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo& contact) override { storeMarkContactsDirty(); }
  ContactInfo* processAck(const uint8_t* data) override;
  void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char* text) override;
  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override { }
  void onCLICommandRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*, char*) override { }
  void onSignedMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const uint8_t*, const char*) override { }
  uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t, uint8_t*) override { return 0; }
  void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override { }
  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override { }   // retries are driven by checkDMRetries()

public:
  ChatMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables);

  void begin();
  void applyRadio();
  void loop();

  bool sendAdvert(bool flood);
  bool sendChannel(int channel_idx, const char* text);
  void sendDM(ContactInfo& to, const char* text);

  int  joinChannel(const char* name, const uint8_t* secret, int secret_len);   // index, or -1 if full
  void leaveChannel(int idx);

  void deleteContact(ContactInfo& c);
  ContactInfo heard[MAX_HEARD];   // newest last
  int num_heard = 0;
  bool addHeard(int i);

  void startDiscovery();
  bool discovering() { return disc_tag && (int32_t)(millis() - disc_until) < 0; }
  int  discoverSecsLeft() { return discovering() ? (int32_t)(disc_until - millis()) / 1000 + 1 : 0; }
  Discovered discovered[MAX_DISCOVERED];
  int num_discovered = 0;
  bool addDiscovered(int i);

  void setClock(uint32_t epoch, uint8_t src) { getRTCClock()->setCurrentTime(epoch); clock_src = src; }
  bool clockSynced() const { return clock_src != CLOCK_NONE; }
  uint8_t clockSource() const { return clock_src; }
  ContactInfo* contactByConv(const uint8_t conv[]) { return lookupContactByPubKey(&conv[1], CONV_ID_LEN - 1); }
  int channelByConv(const uint8_t conv[]);
  const uint8_t* selfPubKey() const { return self_id.pub_key; }
  uint32_t now() { return getRTCClock()->getCurrentTime(); }
};

extern ChatMesh the_mesh;
