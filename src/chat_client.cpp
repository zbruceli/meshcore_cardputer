#include <helpers/TxtDataHelpers.h>
#include "chat_client.h"
#include "target.h"

#define CLOCK_SANE_MIN  1767225600   // 1 Jan 2026: ignore adverts from nodes with an unset clock

ChatClient client;

void onRadioRx(const uint8_t* raw, int len, float snr) { client.packetIn(raw, len, snr); }
void onRadioTx(const uint8_t* raw, int len) { client.packetOut(raw, len); }

// ---- node settings (shared with the phone app) ----

void ChatClient::applyRadio() {
  radio_driver.setParams(node().freq, node().bw, node().sf, node().cr);
  radio_driver.setTxPower(node().tx_power_dbm);
}

#define AUTO_ADD_OVERWRITE  0x01   // MyMesh's AUTO_ADD_* bits
#define AUTO_ADD_CHAT       0x02
#define AUTO_ADD_REPEATER   0x04

int ChatClient::addMode() {
  if ((node().manual_add_contacts & 1) == 0) return ADD_ALL;
  uint8_t types = node().autoadd_config & (AUTO_ADD_CHAT | AUTO_ADD_REPEATER);
  return types == AUTO_ADD_CHAT ? ADD_CHAT : types == 0 ? ADD_OFF : ADD_CHAT_RPT;
}

void ChatClient::setAddMode(int mode, bool overwrite) {
  node().manual_add_contacts = mode == ADD_ALL ? 0 : 1;
  node().autoadd_config = (mode == ADD_CHAT_RPT ? AUTO_ADD_CHAT | AUTO_ADD_REPEATER : mode == ADD_CHAT ? AUTO_ADD_CHAT : 0) |
                          (overwrite ? AUTO_ADD_OVERWRITE : 0);
  saveNode();
}

// ---- every packet in and out (radio hooks) ----

static const char* PKT_TYPES[] = { "REQ", "RESP", "TXT", "ACK", "ADVERT", "GRP_TXT", "GRP_DATA", "ANON_REQ",
                                   "PATH", "TRACE", "MULTI", "CTRL", "?", "?", "?", "RAW" };

// The last hash in a flood packet's path is the repeater that transmitted it to us.
void ChatClient::noteRepeaterSnr(const uint8_t* hash, uint8_t len, float snr) {
  RptSnr* r = NULL;
  for (int i = 0; i < MAX_RPT_SNR && !r; i++) {
    if (rpt_snr[i].len == len && memcmp(rpt_snr[i].hash, hash, len) == 0) r = &rpt_snr[i];
  }
  if (!r) r = &rpt_snr[rpt_snr_next++ % MAX_RPT_SNR];
  memcpy(r->hash, hash, len);
  r->len = len;
  r->snr4 = (int8_t)(snr * 4);
  r->at = now();
}

bool ChatClient::repeaterSnr(const uint8_t* pub_key, int8_t& snr4, uint32_t& at) {
  for (int i = 0; i < MAX_RPT_SNR; i++) {   // a 1-byte hash can match more than one repeater: best effort
    if (rpt_snr[i].len && memcmp(rpt_snr[i].hash, pub_key, rpt_snr[i].len) == 0) {
      snr4 = rpt_snr[i].snr4;
      at = rpt_snr[i].at;
      return true;
    }
  }
  return false;
}

void ChatClient::packetIn(const uint8_t* raw, int len, float snr) {
  mesh::Packet pkt;
  if (!pkt.readFrom(raw, len)) return;
  int hops = pkt.getPathHashCount();
  if (pkt.isRouteFlood() && hops > 0) {
    uint8_t hs = pkt.getPathHashSize();
    noteRepeaterSnr(&pkt.path[(hops - 1) * hs], hs, snr);
  }
  if (pkt.getPayloadType() == PAYLOAD_TYPE_GRP_TXT && echo_ts) {   // a repeater re-sending our channel msg?
    uint8_t hash[MAX_HASH_SIZE];
    pkt.calculatePacketHash(hash);   // excludes the path, so it's the same at every hop
    for (int i = msgCount() - 1; i >= 0 && memcmp(hash, echo_hash, MAX_HASH_SIZE) == 0; i--) {
      Msg* m = msgAt(i);
      if (m->out && m->sender_ts == echo_ts && memcmp(m->conv, echo_conv, CONV_ID_LEN) == 0) {
        if (m->attempt < 99) m->attempt++;
        msgsChanged();
        msg_seq++;
        break;
      }
    }
  }
  if (!Serial) return;   // USB not connected: don't spend time printing
  Serial.printf("RX %-8s %s hops %d len %d snr %.1f\n", PKT_TYPES[pkt.getPayloadType() & 0x0F],
                pkt.isRouteFlood() ? "flood " : "direct", hops, len, snr);
}

void ChatClient::packetOut(const uint8_t* raw, int len) {
  mesh::Packet pkt;
  if (!pkt.readFrom(raw, len)) return;
  if (pkt.getPayloadType() == PAYLOAD_TYPE_GRP_TXT && echo_wait_tx) {   // ours (the app's go out with it unset)
    pkt.calculatePacketHash(echo_hash);
    echo_wait_tx = false;
  }
  if (!Serial) return;
  Serial.printf("TX %-8s %s hops %d len %d\n", PKT_TYPES[pkt.getPayloadType() & 0x0F],
                pkt.isRouteFlood() ? "flood " : "direct", pkt.getPathHashCount(), len);
}

void ChatClient::logStatus() {
  if (!Serial) return;
  Serial.printf("rx %u tx %u rxerr %u | rssi %.0f nf %d | heap %u\n",
                (unsigned)radio_driver.getPacketsRecv(), (unsigned)radio_driver.getPacketsSent(),
                (unsigned)radio_driver.getPacketsRecvErrors(), radio_driver.getCurrentRSSI(),
                radio_driver.getNoiseFloor(), (unsigned)ESP.getFreeHeap());
}

void ChatClient::loop() {
  checkDMRetries();
  // the phone app sets the clock directly: notice the jump
  int32_t offset = (int32_t)(now() - millis() / 1000);
  if (clock_offset && abs(offset - clock_offset) > 60 && now() > CLOCK_SANE_MIN) clock_src = CLOCK_APP;
  clock_offset = offset;
}

// ---- receive ----

void ChatClient::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  bool added = the_mesh.lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE) != NULL;
  if (Serial) Serial.printf("advert: %s type %d new %d added %d\n", contact.name, contact.type, is_new, added);
  clockVote(contact.last_advert_timestamp, contact.id.pub_key[0]);
  if ((path_len & 63) == 0 && contact.type == ADV_TYPE_REPEATER) {   // zero-hop advert: heard it directly
    noteRepeaterSnr(contact.id.pub_key, 3, radio_driver.getLastSNR());
  }
  if (is_new && !added) {   // heard, but not added
    int i = 0;
    while (i < num_heard && !heard[i].id.matches(contact.id)) i++;
    if (i == num_heard) {
      if (num_heard == MAX_HEARD) i = 0; else num_heard++;
    }
    memmove(&heard[i], &heard[i + 1], (num_heard - 1 - i) * sizeof(ContactInfo));   // move to the end
    heard[num_heard - 1] = contact;
    heard[num_heard - 1].lastmod = now();
  }
}

void ChatClient::onControlDataRecv(const mesh::Packet* packet) {
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
  d.at = now();
  noteRepeaterSnr(key, 3, packet->getSNR());   // heard it directly, just now
}

void ChatClient::setClock(uint32_t epoch, uint8_t src) {
  the_mesh.getRTCClock()->setCurrentTime(epoch);
  clock_src = src;
  clock_offset = (int32_t)(epoch - millis() / 1000);
}

// Until the GPS or the app sets the clock, learn it from timestamps heard on the mesh. A single node (often a
// repeater) can have a wrong clock, so only set it once timestamps from two different senders agree within 5 min.
void ChatClient::clockVote(uint32_t ts, uint8_t src) {
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

void ChatClient::onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type,
                               uint32_t sender_timestamp, const char* text) {
  if (txt_type != TXT_TYPE_PLAIN) return;   // CLI replies, room server posts: the app shows those
  clockVote(sender_timestamp, from.id.pub_key[0]);
  uint8_t conv[CONV_ID_LEN];
  convOfContact(conv, from);
  for (int i = msgCount() - 1; i >= 0; i--) {   // a retry of a message we already have (it was ACKed again)
    Msg* d = msgAt(i);
    if (!d->out && d->sender_ts == sender_timestamp && memcmp(d->conv, conv, CONV_ID_LEN) == 0 &&
        strcmp(d->text, text) == 0) return;
  }
  Msg* m = msgAdd();
  memcpy(m->conv, conv, CONV_ID_LEN);
  m->ts = now();
  m->sender_ts = sender_timestamp;
  m->unread = 1;
  StrHelper::strncpy(m->text, text, sizeof(m->text));
}

void ChatClient::onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel, const char* text) {
  Msg* m = msgAdd();
  convOfChannel(m->conv, channel.channel);
  m->ts = m->sender_ts = now();   // the engine doesn't pass the sender's timestamp on
  m->unread = 1;
  StrHelper::strncpy(m->text, text, sizeof(m->text));
}

// An ACK may belong to any attempt, and over many flood hops it can arrive after we've retried or given up.
void ChatClient::onACKRecv(uint32_t ack_crc) {
  if (Serial) Serial.printf("ack %08x\n", (unsigned)ack_crc);
  for (int a = 0; a < MAX_SENT_ACKS; a++) {
    SentAck& sa = sent_acks[a];
    if (sa.ack == 0 || sa.ack != ack_crc) continue;
    for (int i = msgCount() - 1; i >= 0; i--) {
      Msg* m = msgAt(i);
      if (m->out && m->sender_ts == sa.sender_ts && memcmp(m->conv, sa.conv, CONV_ID_LEN) == 0) {
        if (m->status != ST_ACKED) {
          m->status = ST_ACKED;
          msgsChanged();
          msg_seq++;   // redraw
        }
        break;
      }
    }
    sa.ack = 0;
    return;
  }
}

// ---- send ----

bool ChatClient::sendAdvert(bool flood) {
  if (flood) return the_mesh.advert();
  mesh::Packet* pkt = the_mesh.createSelfAdvert(node().node_name);
  if (!pkt) return false;
  the_mesh.sendZeroHop(pkt);
  return true;
}

bool ChatClient::sendChannel(int channel_idx, const char* text) {
  ChannelDetails ch;
  if (!the_mesh.getChannel(channel_idx, ch)) return false;

  Msg* m = msgAdd();
  convOfChannel(m->conv, ch.channel);
  m->ts = m->sender_ts = now();
  m->out = 1;
  snprintf(m->text, sizeof(m->text), "%s: %s", node().node_name, text);
  bool ok = the_mesh.sendGroupMessage(m->sender_ts, ch.channel, node().node_name, text, strlen(text));
  m->status = ok ? ST_SENT : ST_FAILED;
  if (ok) {   // its hash is taken when it's actually transmitted (packetOut)
    echo_ts = m->sender_ts;
    memcpy(echo_conv, m->conv, CONV_ID_LEN);
    echo_wait_tx = true;
  }
  return ok;
}

void ChatClient::sendDM(ContactInfo& to, const char* text) {
  Msg* m = msgAdd();
  convOfContact(m->conv, to);
  m->ts = m->sender_ts = now();
  m->out = 1;
  StrHelper::strncpy(m->text, text, sizeof(m->text));
  sendDMAttempt(*m, to);
}

void ChatClient::sendDMAttempt(Msg& m, ContactInfo& to) {
  uint32_t est_timeout;
  int rc = the_mesh.sendMessage(to, m.sender_ts, m.attempt, m.text, m.ack, est_timeout);
  if (rc == MSG_SEND_FAILED) {
    m.status = ST_FAILED;
  } else {
    m.status = ST_SENDING;
    m.deadline = millis() + est_timeout;
    if (Serial) Serial.printf("DM attempt %d %s, expect ack %08x, timeout %u ms\n", m.attempt,
                              rc == MSG_SEND_SENT_FLOOD ? "flood" : "direct", (unsigned)m.ack, (unsigned)est_timeout);
    SentAck& sa = sent_acks[sent_ack_next++ % MAX_SENT_ACKS];
    sa.ack = m.ack;
    sa.sender_ts = m.sender_ts;
    memcpy(sa.conv, m.conv, CONV_ID_LEN);
  }
  msgsChanged();
}

void ChatClient::checkDMRetries() {
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
      the_mesh.resetPathTo(*to);   // last try: the direct path may be stale, flood instead
      saveContacts();
    }
    sendDMAttempt(*m, *to);
  }
}

// ---- channels ----

int ChatClient::joinChannel(const char* name, const uint8_t* secret, int secret_len) {
  ChannelDetails ch;
  int free_idx = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (!the_mesh.getChannel(i, ch)) continue;
    if (ch.name[0] && memcmp(ch.channel.secret, secret, secret_len) == 0) return i;   // already joined
    if (!ch.name[0] && free_idx < 0) free_idx = i;
  }
  if (free_idx < 0) return -1;
  memset(&ch, 0, sizeof(ch));
  memcpy(ch.channel.secret, secret, secret_len);
  StrHelper::strncpy(ch.name, name, sizeof(ch.name));
  the_mesh.setChannel(free_idx, ch);
  store.saveChannels(&the_mesh);
  return free_idx;
}

void ChatClient::leaveChannel(int idx) {
  ChannelDetails ch;
  if (!the_mesh.getChannel(idx, ch)) return;
  uint8_t conv[CONV_ID_LEN];
  convOfChannel(conv, ch.channel);
  msgsDeleteConv(conv);
  memset(&ch, 0, sizeof(ch));   // an empty name marks a free slot (skipped by searchChannelsByHash)
  the_mesh.setChannel(idx, ch);
  store.saveChannels(&the_mesh);
}

int ChatClient::channelByConv(const uint8_t conv[]) {
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (the_mesh.getChannel(i, ch) && ch.name[0] && memcmp(&conv[1], ch.channel.secret, CONV_ID_LEN - 1) == 0) return i;
  }
  return -1;
}

// ---- contacts ----

void ChatClient::saveContacts() { store.saveContacts(&the_mesh); }

void ChatClient::deleteContact(ContactInfo& c) {
  uint8_t conv[CONV_ID_LEN];
  convOfContact(conv, c);
  store.deleteBlobByKey(c.id.pub_key, PUB_KEY_SIZE);
  the_mesh.removeContact(c);   // invalidates c
  msgsDeleteConv(conv);
  saveContacts();
}

bool ChatClient::addHeard(int i) {
  if (i < 0 || i >= num_heard || !the_mesh.addContact(heard[i])) return false;
  num_heard--;
  memmove(&heard[i], &heard[i + 1], (num_heard - i) * sizeof(ContactInfo));
  saveContacts();
  return true;
}

// ---- repeater discovery (same request as companion_radio's discover screen, but with full keys) ----

void ChatClient::startDiscovery() {
  uint8_t data[6];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ;   // low bit 0 = reply with full public key
  data[1] = 1 << ADV_TYPE_REPEATER;       // filter: repeaters
  the_mesh.getRNG()->random(&data[2], 4);
  memcpy(&disc_tag, &data[2], 4);
  num_discovered = 0;
  disc_until = millis() + DISCOVER_MILLIS;
  mesh::Packet* pkt = the_mesh.createControlData(data, sizeof(data));
  if (pkt) the_mesh.sendZeroHop(pkt);
}

bool ChatClient::addDiscovered(int i) {
  if (i < 0 || i >= num_discovered) return false;
  ContactInfo c;
  memset(&c, 0, sizeof(c));
  c.id = mesh::Identity(discovered[i].pub_key);
  mesh::Utils::toHex(c.name, c.id.pub_key, 4);   // real name arrives with its next advert
  c.type = ADV_TYPE_REPEATER;
  c.out_path_len = 0;   // it answered a zero-hop request: direct neighbour
  c.lastmod = now();
  if (!the_mesh.addContact(c)) return false;
  saveContacts();
  return true;
}
