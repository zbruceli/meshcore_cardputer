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
  channelsLoad(*this);
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
  clockVote(contact.last_advert_timestamp, contact.id.pub_key[0]);
  if (is_new && !lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE)) {   // heard, but not added
    int i = 0;
    while (i < num_heard && !heard[i].id.matches(contact.id)) i++;
    if (i == num_heard) {
      if (num_heard == MAX_HEARD) i = 0; else num_heard++;
    }
    memmove(&heard[i], &heard[i + 1], (num_heard - 1 - i) * sizeof(ContactInfo));   // move to the end
    heard[num_heard - 1] = contact;
    heard[num_heard - 1].lastmod = now();
    return;
  }
  storeMarkContactsDirty();
}

// Raw advert packets, kept so a contact can be shared (re-broadcast zero-hop). Same layout as companion_radio.
static void blobPath(char* path, const uint8_t key[]) {
  strcpy(path, "/bl/");
  mesh::Utils::toHex(&path[4], key, 8);
}

int ChatMesh::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  char path[24];
  blobPath(path, key);
  File f = SPIFFS.open(path, "r");
  if (!f) return 0;
  int len = f.read(dest_buf, 255);
  f.close();
  return len;
}

bool ChatMesh::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) {
  char path[24];
  blobPath(path, key);
  File f = SPIFFS.open(path, "w", true);
  if (!f) return false;
  bool ok = f.write(src_buf, len) == (size_t)len;
  f.close();
  return ok;
}

void ChatMesh::onControlDataRecv(mesh::Packet* packet) {
  // CTL_TYPE_NODE_DISCOVER_RESP from a repeater: [type|node_type] [snr x4] [tag:4] [pub_key:32]
  if ((packet->payload[0] & 0xF0) != CTL_TYPE_NODE_DISCOVER_RESP || packet->payload_len < 6 + PUB_KEY_SIZE) return;
  if (!discovering() || memcmp(&packet->payload[2], &disc_tag, 4) != 0) return;
  const uint8_t* key = &packet->payload[6];
  for (int i = 0; i < num_discovered; i++) {
    if (memcmp(discovered[i].pub_key, key, PUB_KEY_SIZE) == 0) return;
  }
  if (num_discovered == MAX_DISCOVERED) return;
  Discovered& d = discovered[num_discovered++];
  memcpy(d.pub_key, key, PUB_KEY_SIZE);
  d.snr_out4 = (int8_t)packet->payload[1];
  d.snr_in4 = (int8_t)(packet->getSNR() * 4);
}

// Until the GPS has a fix, learn the clock from timestamps heard on the mesh. A single node (often a repeater)
// can have a wrong clock, so only set it once timestamps from two different senders agree within 5 minutes.
void ChatMesh::clockVote(uint32_t ts, uint8_t src) {
  if (clockSynced() || ts < CLOCK_SANE_MIN) return;
  int32_t offset = ts - millis() / 1000;
  for (int i = 0; i < num_votes && i < 4; i++) {
    if (vote_src[i] != src && abs(offset - clock_votes[i]) < 300) {
      setClock(ts, CLOCK_MESH);
      return;
    }
  }
  vote_src[num_votes % 4] = src;
  clock_votes[num_votes++ % 4] = offset;
}

void ChatMesh::onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) {
  clockVote(sender_timestamp, from.id.pub_key[0]);
  Msg* m = msgAdd();
  convOfContact(m->conv, from);
  m->ts = now();
  m->sender_ts = sender_timestamp;
  m->unread = 1;
  StrHelper::strncpy(m->text, text, sizeof(m->text));
}

void ChatMesh::onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char* text) {
  uint8_t src = 0;
  for (const char* p = text; *p && *p != ':'; p++) src = src * 31 + *p;   // sender name ("name: text")
  clockVote(timestamp, src);
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

// ---- channels ----

int ChatMesh::joinChannel(const char* name, const uint8_t* secret, int secret_len) {
  ChannelDetails ch;
  int free_idx = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (!getChannel(i, ch)) continue;
    if (ch.name[0] && memcmp(ch.channel.secret, secret, secret_len) == 0) return i;   // already joined
    if (!ch.name[0] && free_idx < 0) free_idx = i;
  }
  if (free_idx < 0) return -1;
  memset(&ch, 0, sizeof(ch));
  memcpy(ch.channel.secret, secret, secret_len);
  StrHelper::strncpy(ch.name, name, sizeof(ch.name));
  setChannel(free_idx, ch);
  channelsSave(*this);
  return free_idx;
}

void ChatMesh::leaveChannel(int idx) {
  ChannelDetails ch;
  if (!getChannel(idx, ch)) return;
  uint8_t conv[CONV_ID_LEN];
  convOfChannel(conv, ch.channel);
  msgsDeleteConv(conv);
  memset(&ch, 0, sizeof(ch));   // an empty name marks a free slot (skipped by searchChannelsByHash)
  setChannel(idx, ch);
  channelsSave(*this);
}

// ---- contacts ----

void ChatMesh::deleteContact(ContactInfo& c) {
  uint8_t conv[CONV_ID_LEN];
  convOfContact(conv, c);
  char path[24];
  blobPath(path, c.id.pub_key);
  SPIFFS.remove(path);
  removeContact(c);   // invalidates c
  msgsDeleteConv(conv);
  storeMarkContactsDirty();
}

void ChatMesh::onContactOverwrite(const uint8_t* pub_key) {
  char path[24];
  blobPath(path, pub_key);
  SPIFFS.remove(path);
}

bool ChatMesh::addHeard(int i) {
  if (i < 0 || i >= num_heard || !addContact(heard[i])) return false;
  num_heard--;
  memmove(&heard[i], &heard[i + 1], (num_heard - i) * sizeof(ContactInfo));
  storeMarkContactsDirty();
  return true;
}

// ---- repeater discovery (same request as companion_radio's discover screen, but with full keys) ----

void ChatMesh::startDiscovery() {
  uint8_t data[6];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ;   // low bit 0 = reply with full public key
  data[1] = 1 << ADV_TYPE_REPEATER;       // filter: repeaters
  getRNG()->random(&data[2], 4);
  memcpy(&disc_tag, &data[2], 4);
  num_discovered = 0;
  disc_until = millis() + DISCOVER_MILLIS;
  mesh::Packet* pkt = createControlData(data, sizeof(data));
  if (pkt) sendZeroHop(pkt);
}

bool ChatMesh::addDiscovered(int i) {
  if (i < 0 || i >= num_discovered) return false;
  ContactInfo c;
  memset(&c, 0, sizeof(c));
  c.id = mesh::Identity(discovered[i].pub_key);
  mesh::Utils::toHex(c.name, c.id.pub_key, 4);   // real name arrives with its next advert
  c.type = ADV_TYPE_REPEATER;
  c.out_path_len = 0;   // it answered a zero-hop request: direct neighbour
  c.lastmod = now();
  if (!addContact(c)) return false;
  storeMarkContactsDirty();
  return true;
}

int ChatMesh::channelByConv(const uint8_t conv[]) {
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (getChannel(i, ch) && ch.name[0] && memcmp(&conv[1], ch.channel.secret, CONV_ID_LEN - 1) == 0) return i;
  }
  return -1;
}
