#pragma once
// MeshCore chat client: BaseChatMesh + message log + DM retries. No companion/BLE protocol.

#include <Arduino.h>
#include <Mesh.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>
#include "store.h"

#define PUBLIC_GROUP_PSK  "izOH6cXN6mrJ5e26oRXNcg=="
#define DM_MAX_ATTEMPTS   3

inline void convOfContact(uint8_t conv[], const ContactInfo& c) {
  conv[0] = CONV_DM;
  memcpy(&conv[1], c.id.pub_key, CONV_ID_LEN - 1);
}
inline void convOfChannel(uint8_t conv[], const mesh::GroupChannel& ch) {
  conv[0] = CONV_CHANNEL;
  memcpy(&conv[1], ch.secret, CONV_ID_LEN - 1);
}

class ChatMesh : public BaseChatMesh {
  bool clock_synced = false;

  void sendDMAttempt(Msg& m, ContactInfo& to);
  void checkDMRetries();

protected:
  float getAirtimeBudgetFactor() const override { return 1.0f; }
  int calcRxDelay(float score, uint32_t air_time) const override { return 0; }
  bool allowPacketForward(const mesh::Packet* packet) override { return false; }   // client, not a repeater

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

  ContactInfo* contactByConv(const uint8_t conv[]) { return lookupContactByPubKey(&conv[1], CONV_ID_LEN - 1); }
  int channelByConv(const uint8_t conv[]);
  const uint8_t* selfPubKey() const { return self_id.pub_key; }
  uint32_t now() { return getRTCClock()->getCurrentTime(); }
};

extern ChatMesh the_mesh;
