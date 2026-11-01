#include <SPIFFS.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/IdentityStore.h>
#include "chat_mesh.h"
#include "target.h"

// same timeout model as MeshCore's companion_radio
#define SEND_TIMEOUT_BASE_MILLIS          500
#define FLOOD_SEND_TIMEOUT_FACTOR         16.0f
#define DIRECT_SEND_PERHOP_FACTOR         6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS   250

#define CLOCK_SANE_MIN  1767225600   // 1 Jan 2026: ignore adverts from nodes with an unset clock

ChatMesh::ChatMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables)
  : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables) { }

void ChatMesh::begin() {
  BaseChatMesh::begin();

  IdentityStore store(SPIFFS, "/identity");
  store.begin();
  if (!store.load("_main", self_id)) {
    self_id = radio_new_identity();
    int count = 0;
    while (count++ < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {  // reserved hashes
      self_id = radio_new_identity();
    }
    store.save("_main", self_id);
  }
  if (prefs.name[0] == 0) {
    mesh::Utils::toHex(prefs.name, self_id.pub_key, 4);   // MeshCore's default node name
  }

  resetContacts();
  contactsLoad(*this);
  bootstrapRTCfromContacts();
  addChannel("Public", PUBLIC_GROUP_PSK);
}

void ChatMesh::applyRadio() {
  radio_driver.setParams(prefs.freq, prefs.bw, prefs.sf, prefs.cr);
  radio_driver.setTxPower(prefs.tx_power);
}

void ChatMesh::loop() {
  BaseChatMesh::loop();
  checkDMRetries();
}

void ChatMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  sendFlood(pkt, delay_millis, prefs.path_hash_size);
}
void ChatMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  sendFlood(pkt, delay_millis, prefs.path_hash_size);
}

uint32_t ChatMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
}
uint32_t ChatMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  uint8_t path_hash_count = path_len & 63;
  return SEND_TIMEOUT_BASE_MILLIS +
         ((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) * (path_hash_count + 1));
}

// ---- receive ----

void ChatMesh::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  // No GPS/phone to set the clock: take it from the first (signed) advert heard this boot.
  // Adverts older than what we already know of that contact are dropped as replays by BaseChatMesh.
  if (!clock_synced && contact.last_advert_timestamp > CLOCK_SANE_MIN) {
    getRTCClock()->setCurrentTime(contact.last_advert_timestamp);
    contact.lastmod = contact.last_advert_timestamp;
    clock_synced = true;
  }
  storeMarkContactsDirty();
}

void ChatMesh::onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) {
  Msg* m = msgAdd();
  convOfContact(m->conv, from);
  m->ts = now();
  m->sender_ts = sender_timestamp;
  m->unread = 1;
  StrHelper::strncpy(m->text, text, sizeof(m->text));
}

void ChatMesh::onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char* text) {
  Msg* m = msgAdd();
  convOfChannel(m->conv, channel);
  m->ts = now();
  m->sender_ts = timestamp;
  m->unread = 1;
  StrHelper::strncpy(m->text, text, sizeof(m->text));
}

ContactInfo* ChatMesh::processAck(const uint8_t* data) {
  for (int i = msgCount() - 1; i >= 0; i--) {
    Msg* m = msgAt(i);
    if (m->out && m->status == ST_SENDING && memcmp(data, &m->ack, 4) == 0) {
      m->status = ST_ACKED;
      msgsChanged();
      msg_seq++;   // redraw
      return contactByConv(m->conv);
    }
  }
  return NULL;
}

// ---- send ----

bool ChatMesh::sendAdvert(bool flood) {
  mesh::Packet* pkt = createSelfAdvert(prefs.name);
  if (!pkt) return false;
  if (flood) {
    sendFlood(pkt, 0, prefs.path_hash_size);
  } else {
    sendZeroHop(pkt);
  }
  return true;
}

bool ChatMesh::sendChannel(int channel_idx, const char* text) {
  ChannelDetails ch;
  if (!getChannel(channel_idx, ch)) return false;

  Msg* m = msgAdd();
  convOfChannel(m->conv, ch.channel);
  m->ts = m->sender_ts = now();
  m->out = 1;
  snprintf(m->text, sizeof(m->text), "%s: %s", prefs.name, text);
  bool ok = sendGroupMessage(m->sender_ts, ch.channel, prefs.name, text, strlen(text));
  m->status = ok ? ST_SENT : ST_FAILED;
  return ok;
}

void ChatMesh::sendDM(ContactInfo& to, const char* text) {
  Msg* m = msgAdd();
  convOfContact(m->conv, to);
  m->ts = m->sender_ts = now();
  m->out = 1;
  StrHelper::strncpy(m->text, text, sizeof(m->text));
  sendDMAttempt(*m, to);
}

void ChatMesh::sendDMAttempt(Msg& m, ContactInfo& to) {
  uint32_t est_timeout;
  int rc = sendMessage(to, m.sender_ts, m.attempt, m.text, m.ack, est_timeout);
  if (rc == MSG_SEND_FAILED) {
    m.status = ST_FAILED;
  } else {
    m.status = ST_SENDING;
    m.deadline = millis() + est_timeout;
  }
  msgsChanged();
}

void ChatMesh::checkDMRetries() {
  uint32_t t = millis();
  for (int i = 0; i < msgCount(); i++) {
    Msg* m = msgAt(i);
    if (m->status != ST_SENDING || (int32_t)(t - m->deadline) < 0) continue;

    ContactInfo* to = contactByConv(m->conv);
    if (!to || m->attempt + 1 >= DM_MAX_ATTEMPTS) {
      m->status = ST_FAILED;
      msgsChanged();
      msg_seq++;
      continue;
    }
    m->attempt++;
    if (m->attempt == DM_MAX_ATTEMPTS - 1 && to->out_path_len != OUT_PATH_UNKNOWN) {
      resetPathTo(*to);   // last try: the direct path may be stale, flood instead
      storeMarkContactsDirty();
    }
    sendDMAttempt(*m, *to);
  }
}

int ChatMesh::channelByConv(const uint8_t conv[]) {
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (getChannel(i, ch) && ch.name[0] && memcmp(&conv[1], ch.channel.secret, CONV_ID_LEN - 1) == 0) return i;
  }
  return -1;
}
