#include "ui.h"
#include "target.h"
#include "chat_mesh.h"
#include "presets.h"

static const Screen TABS[] = { S_INBOX, S_CONTACTS, S_REPEATERS, S_RADIO, S_SETTINGS };

static Screen screen;
static Screen chat_back;               // where Esc goes from a chat
static Screen contact_back;            // where Esc goes from contact details
static int sel, top;                   // list cursor
static char input[MAX_TEXT_LEN + 1];   // compose line / name editor
static uint8_t cur_conv[CONV_ID_LEN];  // open chat, or selected contact (as a DM conv id)
static int chat_scroll;                // lines scrolled up from the newest
static bool from_settings;             // region/name screens opened from SET
static bool confirm_reset;

void go(Screen s) {
  screen = s;
  sel = top = 0;
  confirm_reset = false;
}

bool screenShowsConv(const uint8_t conv[]) {
  return screen == S_CHAT && memcmp(conv, cur_conv, CONV_ID_LEN) == 0;
}

static void openChat(const uint8_t conv[], Screen back) {
  memcpy(cur_conv, conv, CONV_ID_LEN);
  chat_back = back;
  chat_scroll = 0;
  input[0] = 0;
  screen = S_CHAT;
}

static void convName(char* dest, int sz, const uint8_t conv[]) {
  if (conv[0] == CONV_CHANNEL) {
    ChannelDetails ch;
    int idx = the_mesh.channelByConv(conv);
    snprintf(dest, sz, "#%s", idx >= 0 && the_mesh.getChannel(idx, ch) ? ch.name : "?");
  } else {
    ContactInfo* c = the_mesh.contactByConv(conv);
    if (c) snprintf(dest, sz, "%s", c->name);
    else snprintf(dest, sz, "?%02x%02x%02x", conv[1], conv[2], conv[3]);
  }
}

// ================================================================ onboarding / region

static void applyPreset(int i) {
  const RegionPreset& p = PRESETS[i];
  prefs.region = i;
  prefs.freq = p.freq;
  prefs.bw = p.bw;
  prefs.sf = p.sf;
  prefs.cr = p.cr;
  prefs.path_hash_size = p.path_hash_size;
}

static void drawRegion() {
  drawStatus(from_settings ? "SET Region" : "SETUP 1/3");
  for (int r = 0; r < BODY_ROWS && top + r < NUM_PRESETS; r++) {
    char f[10];
    snprintf(f, sizeof(f), "%.3f", PRESETS[top + r].freq);
    drawRow(r, top + r == sel, PRESETS[top + r].title, f);
  }
  drawHints(from_settings ? "Pick region ENT ok  ` back" : "Pick your region   ENT ok");
}

static void keyRegion(const Key& k) {
  listMove(sel, top, NUM_PRESETS, k);
  if (k.code == K_ENTER) {
    applyPreset(sel);
    if (from_settings) {
      prefsSave();
      the_mesh.applyRadio();
      toast("Radio retuned");
      go(S_SETTINGS);
      sel = top = 1;
    } else {
      StrHelper::strncpy(input, prefs.name, sizeof(prefs.name));
      go(S_NAME);
    }
  } else if (k.code == K_BACK && from_settings) {
    go(S_SETTINGS);
    sel = top = 1;
  }
}

static void drawName() {
  drawStatus(from_settings ? "SET Name" : "SETUP 2/3");
  drawRow(0, false, "Your name on the mesh:");
  drawInput(2, input);
  drawRow(4, false, "Others see this name.", NULL, C_DIM);
  drawHints("ENT ok   ` back");
}

static void keyName(const Key& k) {
  editText(input, sizeof(prefs.name) - 1, k);
  if (k.code == K_ENTER && input[0]) {
    StrHelper::strncpy(prefs.name, input, sizeof(prefs.name));
    if (from_settings) {
      prefsSave();
      go(S_SETTINGS);
    } else {
      go(S_CONFIRM);
    }
  } else if (k.code == K_BACK) {
    go(from_settings ? S_SETTINGS : S_REGION);
  }
}

static void drawConfirm() {
  char s[COLS + 1];
  drawStatus("SETUP 3/3");
  drawRow(0, false, "Name", prefs.name);
  drawRow(1, false, "Region", PRESETS[prefs.region].title);
  snprintf(s, sizeof(s), "%.3f MHz  %d dBm", prefs.freq, prefs.tx_power);
  drawRow(2, false, s);
  snprintf(s, sizeof(s), "SF%d  BW%g  CR%d", prefs.sf, prefs.bw, prefs.cr);
  drawRow(3, false, s);
  drawRow(5, false, "ENT: go on air, say hello");
  drawHints("ENT start   ` back");
}

static void keyConfirm(const Key& k) {
  if (k.code == K_ENTER) {
    prefs.onboarded = 1;
    prefsSave();
    the_mesh.applyRadio();
    the_mesh.sendAdvert(true);
    toast("On air! Advert sent");
    uint8_t conv[CONV_ID_LEN];
    ChannelDetails pub;
    the_mesh.getChannel(0, pub);
    convOfChannel(conv, pub.channel);
    openChat(conv, S_INBOX);
  } else if (k.code == K_BACK) {
    go(S_NAME);
  }
}

// ================================================================ inbox

struct ConvItem { uint8_t conv[CONV_ID_LEN]; uint32_t last_ts; uint8_t unread; };
#define MAX_CONVS 32
static ConvItem convs[MAX_CONVS];
static int nconvs;

static ConvItem* findConv(const uint8_t conv[]) {
  for (int i = 0; i < nconvs; i++) if (memcmp(convs[i].conv, conv, CONV_ID_LEN) == 0) return &convs[i];
  return NULL;
}

// channels first (fixed order), then DM threads, newest first
static void buildConvs() {
  nconvs = 0;
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS && nconvs < MAX_CONVS; i++) {
    if (!the_mesh.getChannel(i, ch) || !ch.name[0]) continue;
    memset(&convs[nconvs], 0, sizeof(ConvItem));
    convOfChannel(convs[nconvs++].conv, ch.channel);
  }
  int nchannels = nconvs;
  for (int i = 0; i < msgCount(); i++) {
    Msg* m = msgAt(i);
    ConvItem* c = findConv(m->conv);
    if (!c && nconvs < MAX_CONVS) {
      c = &convs[nconvs++];
      memcpy(c->conv, m->conv, CONV_ID_LEN);
      c->unread = 0;
    }
    if (!c) continue;
    c->last_ts = m->ts;
    c->unread += m->unread;
  }
  qsort(&convs[nchannels], nconvs - nchannels, sizeof(ConvItem), [](const void* a, const void* b) {
    uint32_t ta = ((const ConvItem*)a)->last_ts, tb = ((const ConvItem*)b)->last_ts;
    return ta == tb ? 0 : (ta < tb ? 1 : -1);
  });
}

static void drawInbox() {
  buildConvs();
  listMove(sel, top, nconvs, Key{K_NONE});
  drawStatus("1 MSG");
  for (int r = 0; r < BODY_ROWS && top + r < nconvs; r++) {
    ConvItem& c = convs[top + r];
    char name[34], right[8];
    convName(name, sizeof(name), c.conv);
    if (c.unread) snprintf(right, sizeof(right), "*%d", c.unread);
    else if (c.last_ts) fmtAge(right, c.last_ts);
    else right[0] = 0;
    drawRow(r, top + r == sel, name, right);
  }
  drawHints("ENT open  TAB next  1-5 tab");
}

static void keyInbox(const Key& k) {
  buildConvs();
  listMove(sel, top, nconvs, k);
  if (k.code == K_ENTER && nconvs) openChat(convs[sel].conv, S_INBOX);
}

// ================================================================ chat

#define MAX_LINES 48
struct Line { char text[COLS + 1]; uint16_t color; };
static Line lines[MAX_LINES];   // newest first

// Word-wraps src into lines of COLS chars. Returns the number of lines.
static int wrap(const char* src, Line out[], int max, uint16_t color) {
  char s[MAX_TEXT_LEN + 1];
  toAscii(s, src, MAX_TEXT_LEN);
  int n = 0, len = strlen(s), pos = 0;
  while (pos < len && n < max) {
    int take = len - pos;
    if (take > COLS) {
      take = COLS;
      for (int i = COLS; i > COLS / 2; i--) if (s[pos + i] == ' ') { take = i; break; }
    }
    memcpy(out[n].text, &s[pos], take);
    out[n].text[take] = 0;
    out[n++].color = color;
    pos += take;
    while (s[pos] == ' ') pos++;
  }
  return n;
}

// Fills lines[] (newest first) with header + body lines of the open conversation.
static int buildChatLines(int need) {
  int n = 0;
  bool is_channel = cur_conv[0] == CONV_CHANNEL;
  for (int i = msgCount() - 1; i >= 0 && n < need; i--) {
    Msg* m = msgAt(i);
    if (memcmp(m->conv, cur_conv, CONV_ID_LEN) != 0) continue;

    char sender[32];
    const char* body = m->text;
    if (m->out) {
      strcpy(sender, "me");
      if (is_channel) { const char* p = strstr(m->text, ": "); if (p) body = p + 2; }
    } else if (is_channel) {
      const char* p = strstr(m->text, ": ");
      int l = p ? min((int)(p - m->text), 31) : 0;
      memcpy(sender, m->text, l);
      sender[l] = 0;
      if (p) body = p + 2;
    } else {
      convName(sender, sizeof(sender), m->conv);
    }

    Line msg_lines[12];
    char hdr[48], t[8];
    fmtTime(t, m->ts);
    static const char* STATUS[] = { "", " [..]", "", " [ok]", " [x]" };
    snprintf(hdr, sizeof(hdr), "%s %s%s", t, sender, STATUS[m->status]);
    int k = wrap(hdr, msg_lines, 1, m->status == ST_FAILED ? C_RED : C_DIM);
    k += wrap(body, &msg_lines[k], 11, C_AMBER);
    while (k > 0 && n < MAX_LINES) lines[n++] = msg_lines[--k];
  }
  return n;
}

static void markConvRead() {
  bool marked = false;
  for (int i = 0; i < msgCount(); i++) {
    Msg* m = msgAt(i);
    if (m->unread && memcmp(m->conv, cur_conv, CONV_ID_LEN) == 0) { m->unread = 0; marked = true; }
  }
  if (marked) msgsChanged();
}

static void drawChat() {
  char title[34];
  convName(title, sizeof(title), cur_conv);
  markConvRead();
  drawStatus(title);
  const int visible = BODY_ROWS - 1;   // last row is the compose line
  int n = buildChatLines(visible + chat_scroll);
  if (chat_scroll > n - visible) chat_scroll = max(0, n - visible);
  for (int r = 0; r < visible; r++) {
    int idx = chat_scroll + (visible - 1 - r);
    if (idx < n) drawRow(r, false, lines[idx].text, NULL, lines[idx].color);
  }
  if (n == 0) drawRow(1, false, "No messages yet.", NULL, C_DIM);
  drawInput(BODY_ROWS - 1, input);
  drawHints("ENT send  Fn;. scroll ` back");
}

static void keyChat(const Key& k) {
  bool is_channel = cur_conv[0] == CONV_CHANNEL;
  int max_len = is_channel ? MAX_TEXT_LEN - strlen(prefs.name) - 2 : MAX_TEXT_LEN;
  if (editText(input, max_len, k)) return;

  if (k.code == K_UP) chat_scroll++;
  else if (k.code == K_DOWN && chat_scroll > 0) chat_scroll--;
  else if (k.code == K_BACK) go(chat_back);
  else if (k.code == K_ENTER && input[0]) {
    if (is_channel) {
      int idx = the_mesh.channelByConv(cur_conv);
      if (idx < 0 || !the_mesh.sendChannel(idx, input)) toast("Send failed");
    } else {
      ContactInfo* c = the_mesh.contactByConv(cur_conv);
      if (c) the_mesh.sendDM(*c, input);
      else toast("Contact no longer exists");
    }
    input[0] = 0;
    chat_scroll = 0;
  }
}

// ================================================================ contacts / repeaters

struct ContactItem { uint16_t idx; uint32_t lastmod; };
static ContactItem citems[MAX_CONTACTS];
static int ncitems;

static void buildContacts(bool repeaters) {
  ncitems = 0;
  ContactInfo c;
  for (int i = 0; i < the_mesh.getTotalContactSlots() && ncitems < MAX_CONTACTS; i++) {
    if (!the_mesh.getContactByIdx(i, c) || c.type == ADV_TYPE_NONE) continue;
    if ((c.type == ADV_TYPE_REPEATER) != repeaters) continue;
    citems[ncitems++] = { (uint16_t)i, c.lastmod };
  }
  qsort(citems, ncitems, sizeof(ContactItem), [](const void* a, const void* b) {
    uint32_t ta = ((const ContactItem*)a)->lastmod, tb = ((const ContactItem*)b)->lastmod;
    return ta == tb ? 0 : (ta < tb ? 1 : -1);
  });
}

static void fmtHops(char* dest, const ContactInfo& c) {
  if (c.out_path_len == OUT_PATH_UNKNOWN) strcpy(dest, "?");
  else if ((c.out_path_len & 63) == 0) strcpy(dest, "D");
  else sprintf(dest, "%d", c.out_path_len & 63);
}

static void drawContacts(bool repeaters) {
  buildContacts(repeaters);
  listMove(sel, top, ncitems, Key{K_NONE});
  drawStatus(repeaters ? "3 RPT" : "2 CON");
  ContactInfo c;
  for (int r = 0; r < BODY_ROWS && top + r < ncitems; r++) {
    the_mesh.getContactByIdx(citems[top + r].idx, c);
    char left[36], right[12], hops[4], age[8];
    const char* tag = c.type == ADV_TYPE_ROOM ? "[R]" : c.type == ADV_TYPE_SENSOR ? "[S]" : "";
    snprintf(left, sizeof(left), "%s%s", tag, c.name);
    fmtHops(hops, c);
    fmtAge(age, c.lastmod);
    snprintf(right, sizeof(right), "%s %4s", hops, age);
    drawRow(r, top + r == sel, left, right);
  }
  if (ncitems == 0) {
    drawRow(1, false, repeaters ? "No repeaters heard yet." : "No contacts yet.", NULL, C_DIM);
    drawRow(2, false, "They appear when their", NULL, C_DIM);
    drawRow(3, false, "adverts are received.", NULL, C_DIM);
  }
  drawHints("ENT info  hops: D=direct");
}

static void keyContacts(const Key& k, bool repeaters) {
  buildContacts(repeaters);
  listMove(sel, top, ncitems, k);
  if (k.code == K_ENTER && ncitems) {
    ContactInfo c;
    the_mesh.getContactByIdx(citems[sel].idx, c);
    convOfContact(cur_conv, c);
    contact_back = screen;
    screen = S_CONTACT;
  }
}

static void drawContact() {
  ContactInfo* c = the_mesh.contactByConv(cur_conv);
  if (!c) { go(contact_back); return; }
  static const char* TYPES[] = { "?", "Chat", "Repeater", "Room", "Sensor" };
  char s[COLS + 1], hex[17];
  drawStatus(c->name);
  drawRow(0, false, "Name", c->name);
  drawRow(1, false, "Type", TYPES[c->type <= ADV_TYPE_SENSOR ? c->type : 0]);
  mesh::Utils::toHex(hex, c->id.pub_key, 8);
  drawRow(2, false, "ID", hex);
  if (c->out_path_len == OUT_PATH_UNKNOWN) strcpy(s, "flood (unknown)");
  else if ((c->out_path_len & 63) == 0) strcpy(s, "direct");
  else snprintf(s, sizeof(s), "%d hops", c->out_path_len & 63);
  drawRow(3, false, "Path", s);
  fmtAge(s, c->lastmod);
  drawRow(4, false, "Heard", s);
  if (c->gps_lat || c->gps_lon) {
    snprintf(s, sizeof(s), "%.4f,%.4f", c->gps_lat / 1e6, c->gps_lon / 1e6);
    drawRow(5, false, "GPS", s);
  }
  drawHints(c->type == ADV_TYPE_CHAT ? "ENT message   ` back" : "` back");
}

static void keyContact(const Key& k) {
  ContactInfo* c = the_mesh.contactByConv(cur_conv);
  if (k.code == K_BACK || !c) {
    go(contact_back);
  } else if (k.code == K_ENTER && c->type == ADV_TYPE_CHAT) {
    openChat(cur_conv, S_CONTACT);
  }
}

// ================================================================ radio

static void drawRadio() {
  char s[COLS + 1];
  drawStatus("4 RAD");
  if (!radio_ok) {
    canvas.setTextColor(C_RED);
    canvas.drawString("NO RADIO", 0, BODY_Y + 8, &fonts::Font4);
    drawRow(3, false, "Check the Cap LoRa-1262", NULL, C_RED);
    drawRow(4, false, "is seated, then reboot.", NULL, C_RED);
    drawHints("");
    return;
  }
  // ham-rig style frequency readout
  snprintf(s, sizeof(s), "%.3f", prefs.freq);
  canvas.setTextColor(C_AMBER);
  int fx = max(0, SCR_W - 26 - (int)canvas.textWidth(s, &fonts::Font7));   // leave room for "MHz"
  canvas.drawString(s, fx, BODY_Y, &fonts::Font7);
  canvas.setFont(&fonts::AsciiFont8x16);
  canvas.setTextColor(C_DIM);
  canvas.drawString("MHz", SCR_W - 24, BODY_Y + 32);

  snprintf(s, sizeof(s), "SF%d BW%g CR%d %ddBm", prefs.sf, prefs.bw, prefs.cr, prefs.tx_power);
  drawRow(3, false, s);
  snprintf(s, sizeof(s), "RSSI%4.0f SNR%5.1f NF%4d",
           radio_driver.getLastRSSI(), radio_driver.getLastSNR(), radio_driver.getNoiseFloor());
  drawRow(4, false, s);
  snprintf(s, sizeof(s), "RX%-5u TX%-5u BAT %.2fV", (unsigned)radio_driver.getPacketsRecv(),
           (unsigned)radio_driver.getPacketsSent(), board.getBattMilliVolts() / 1000.0f);
  drawRow(5, false, s);
  drawHints("ENT advert  Z zero-hop adv");
}

static void keyRadio(const Key& k) {
  if (k.code == K_ENTER) toast(the_mesh.sendAdvert(true) ? "Advert sent (flood)" : "Advert failed");
  else if (k.code == K_CHAR && (k.ch == 'z' || k.ch == 'Z'))
    toast(the_mesh.sendAdvert(false) ? "Advert sent (zero-hop)" : "Advert failed");
}

// ================================================================ settings

enum { SET_NAME, SET_REGION, SET_TX, SET_BRIGHT, SET_SCREEN, SET_BEEP, SET_TZ, SET_ID, SET_RESET, SET_COUNT };

static void drawSettings() {
  drawStatus("5 SET");
  for (int r = 0; r < BODY_ROWS && top + r < SET_COUNT; r++) {
    int i = top + r;
    char v[24] = "";
    const char* label = "";
    switch (i) {
      case SET_NAME:   label = "Name"; snprintf(v, sizeof(v), "%.16s", prefs.name); break;
      case SET_REGION: label = "Region";
        snprintf(v, sizeof(v), "%.18s", prefs.region < NUM_PRESETS ? PRESETS[prefs.region].title : "Custom"); break;
      case SET_TX:     label = "TX power"; snprintf(v, sizeof(v), "< %d dBm >", prefs.tx_power); break;
      case SET_BRIGHT: label = "Brightness"; snprintf(v, sizeof(v), "< %d/4 >", prefs.brightness + 1); break;
      case SET_SCREEN: label = "Screen off";
        if (SCREEN_OFF_SECS[prefs.screen_off] == 0) strcpy(v, "< never >");
        else snprintf(v, sizeof(v), "< %ds >", SCREEN_OFF_SECS[prefs.screen_off]);
        break;
      case SET_BEEP:   label = "Beep"; strcpy(v, prefs.beep ? "< on >" : "< off >"); break;
      case SET_TZ:     label = "Time zone"; snprintf(v, sizeof(v), "< UTC%+d >", prefs.tz_hours); break;
      case SET_ID:     label = "My ID"; mesh::Utils::toHex(v, the_mesh.selfPubKey(), 4); break;
      case SET_RESET:  label = confirm_reset ? "ENT again to WIPE ALL" : "Factory reset"; break;
    }
    drawRow(r, i == sel, label, v, i == SET_RESET && confirm_reset ? C_RED : C_AMBER);
  }
  drawHints("ENT edit   , / change");
}

static void keySettings(const Key& k) {
  listMove(sel, top, SET_COUNT, k);
  if (k.code != K_UP && k.code != K_DOWN && sel != SET_RESET) confirm_reset = false;
  int d = k.code == K_RIGHT || k.code == K_ENTER ? 1 : k.code == K_LEFT ? -1 : 0;

  switch (sel) {
    case SET_NAME:
      if (k.code == K_ENTER) {
        StrHelper::strncpy(input, prefs.name, sizeof(prefs.name));
        from_settings = true;
        go(S_NAME);
      }
      return;
    case SET_REGION:
      if (k.code == K_ENTER) {
        from_settings = true;
        go(S_REGION);
        sel = prefs.region < NUM_PRESETS ? prefs.region : 0;
        listMove(sel, top, NUM_PRESETS, Key{K_NONE});
      }
      return;
    case SET_RESET:
      if (k.code == K_ENTER) {
        if (confirm_reset) storeWipe();
        confirm_reset = true;
      }
      return;
  }
  if (d == 0) return;
  switch (sel) {
    case SET_TX:
      prefs.tx_power = constrain(prefs.tx_power + d, 2, MAX_LORA_TX_POWER);
      radio_driver.setTxPower(prefs.tx_power);
      break;
    case SET_BRIGHT: prefs.brightness = (prefs.brightness + 4 + d) % 4; applyBrightness(); break;
    case SET_SCREEN: prefs.screen_off = (prefs.screen_off + 6 + d) % 6; break;
    case SET_BEEP:   prefs.beep = !prefs.beep; break;
    case SET_TZ:     prefs.tz_hours = constrain(prefs.tz_hours + d, -12, 14); break;
    default: return;
  }
  prefsSave();
}

// ================================================================ dispatch

void screenDraw() {
  switch (screen) {
    case S_REGION:    drawRegion(); break;
    case S_NAME:      drawName(); break;
    case S_CONFIRM:   drawConfirm(); break;
    case S_INBOX:     drawInbox(); break;
    case S_CHAT:      drawChat(); break;
    case S_CONTACTS:  drawContacts(false); break;
    case S_REPEATERS: drawContacts(true); break;
    case S_CONTACT:   drawContact(); break;
    case S_RADIO:     drawRadio(); break;
    case S_SETTINGS:  drawSettings(); break;
  }
}

void screenKey(const Key& k) {
  bool onboarding = !prefs.onboarded;
  bool typing = screen == S_CHAT || screen == S_NAME;

  // tab switching: Tab cycles, digits jump (Fn+digit while typing)
  if (!onboarding && !(screen == S_NAME && from_settings)) {
    int cur = -1;
    for (int i = 0; i < 5; i++) if (TABS[i] == screen) cur = i;
    if (k.code == K_TAB) {
      go(TABS[(cur + 1) % 5]);
      return;
    }
    if (k.code == K_CHAR && k.ch >= '1' && k.ch <= '5' && (k.fn || !typing)) {
      go(TABS[k.ch - '1']);
      return;
    }
  }
  if (!typing && k.code == K_CHAR && !k.fn) {   // arrow keys work without Fn when not typing
    const char* arrows = ";.,/";
    const char* p = strchr(arrows, k.ch);
    if (p) { screenKey(Key{(uint8_t)(K_UP + (p - arrows)), 0, true}); return; }
  }

  switch (screen) {
    case S_REGION:    keyRegion(k); break;
    case S_NAME:      keyName(k); break;
    case S_CONFIRM:   keyConfirm(k); break;
    case S_INBOX:     keyInbox(k); break;
    case S_CHAT:      keyChat(k); break;
    case S_CONTACTS:  keyContacts(k, false); break;
    case S_REPEATERS: keyContacts(k, true); break;
    case S_CONTACT:   keyContact(k); break;
    case S_RADIO:     keyRadio(k); break;
    case S_SETTINGS:  keySettings(k); break;
  }
  if (screen != S_NAME && screen != S_REGION) from_settings = false;
}
