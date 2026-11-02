#include <mbedtls/base64.h>
#include "ui.h"
#include "target.h"
#include "chat_mesh.h"
#include "presets.h"

static const Screen TABS[] = { S_INBOX, S_CONTACTS, S_REPEATERS, S_RADIO, S_SETTINGS };

static Screen screen;
static Screen chat_back;               // where Esc goes from a chat
static Screen contact_back;            // where Esc goes from contact details
static int sel, top;                   // list cursor
static char input[MAX_TEXT_LEN + 1];   // compose line / text editor
static uint8_t cur_conv[CONV_ID_LEN];  // open chat, or selected contact (as a DM conv id)
static int chat_scroll;                // lines scrolled up from the newest
static int reply_back;                 // Fn+R: how many distinct senders back the @mention is
static bool from_settings;             // region screen opened from SET
static bool armed;                     // destructive key pressed once, press again to confirm
static char filter[16];                // contacts: typed name filter

void go(Screen s) {
  screen = s;
  sel = top = 0;
  armed = false;
}

bool screenShowsConv(const uint8_t conv[]) {
  return screen == S_CHAT && memcmp(conv, cur_conv, CONV_ID_LEN) == 0;
}

static void openChat(const uint8_t conv[], Screen back) {
  memcpy(cur_conv, conv, CONV_ID_LEN);
  chat_back = back;
  chat_scroll = 0;
  reply_back = 0;
  input[0] = 0;
  screen = S_CHAT;
}

// channels show their own name ("Public", "#hashtag", private name), DMs "@name"
static void convName(char* dest, int sz, const uint8_t conv[]) {
  if (conv[0] == CONV_CHANNEL) {
    ChannelDetails ch;
    int idx = the_mesh.channelByConv(conv);
    snprintf(dest, sz, "%s", idx >= 0 && the_mesh.getChannel(idx, ch) ? ch.name : "?");
  } else {
    ContactInfo* c = the_mesh.contactByConv(conv);
    if (c) snprintf(dest, sz, "@%s", c->name);
    else snprintf(dest, sz, "@%02x%02x%02x", conv[1], conv[2], conv[3]);
  }
}

static void openChannel(int idx, Screen back) {
  ChannelDetails ch;
  uint8_t conv[CONV_ID_LEN];
  the_mesh.getChannel(idx, ch);
  convOfChannel(conv, ch.channel);
  openChat(conv, back);
}

// ================================================================ text editor (name, channel, frequency, clock)

enum { F_NAME, F_CH_NAME, F_CH_KEY, F_FREQ, F_CLOCK };
struct EditField { const char* title; const char* prompt; const char* help; uint8_t max_len; };
static const EditField FIELDS[] = {
  { "SET Name",      "Your name on the mesh:",       "Others see this name.",    31 },
  { "Add channel",   "#hashtag or private name:",    "#name: key from the name", 30 },
  { "Add channel",   "Secret key (base64 or hex):",  "16 or 32 byte key",        64 },
  { "SET Frequency", "Frequency in MHz:",            "868.000 to 923.000",       10 },
  { "SET Clock",     "Local date and time:",         "YYYY-MM-DD HH:MM",         16 },
};
static int edit_field;
static Screen edit_back;
static int edit_back_sel;
static char new_channel[32];   // name typed in F_CH_NAME, waiting for its key

static void startEdit(int field, const char* initial, Screen back) {
  StrHelper::strncpy(input, initial, sizeof(input));
  edit_field = field;
  edit_back = back;
  edit_back_sel = sel;
  go(S_EDIT);
}

static void endEdit() {
  go(edit_back);
  sel = edit_back_sel;   // back on the same row
}

static int hexDecode(uint8_t* dest, const char* src, int max) {
  int n = strlen(src) / 2;
  if (n > max || (int)strlen(src) != n * 2) return 0;
  for (int i = 0; i < n; i++) {
    char b[3] = { src[i * 2], src[i * 2 + 1], 0 };
    if (!isxdigit(b[0]) || !isxdigit(b[1])) return 0;
    dest[i] = strtoul(b, NULL, 16);
  }
  return n;
}

static void joinChannel(const char* name, const uint8_t* secret, int len) {
  int idx = the_mesh.joinChannel(name, secret, len);
  if (idx < 0) { toast("Channel list full (8)"); go(S_INBOX); return; }
  toast("Joined channel");
  openChannel(idx, S_INBOX);
}

static void keyEditEnter() {
  switch (edit_field) {
    case F_NAME:
      if (!input[0]) return;
      StrHelper::strncpy(prefs.name, input, sizeof(prefs.name));
      if (!prefs.onboarded) { go(S_TZ); return; }
      prefsSave();
      break;

    case F_CH_NAME:
      if (strlen(input) < 2) return;
      if (input[0] == '#') {   // hashtag channel: key = first 16 bytes of sha256("#name"), lowercase like the apps
        for (char* p = input; *p; p++) *p = tolower(*p);
        uint8_t secret[16];
        mesh::Utils::sha256(secret, sizeof(secret), (const uint8_t*)input, strlen(input));
        joinChannel(input, secret, sizeof(secret));
      } else {
        StrHelper::strncpy(new_channel, input, sizeof(new_channel));
        startEdit(F_CH_KEY, "", S_INBOX);
      }
      return;

    case F_CH_KEY: {
      uint8_t secret[32];
      int len = hexDecode(secret, input, sizeof(secret));
      size_t blen = 0;
      if (len == 0 && mbedtls_base64_decode(secret, sizeof(secret), &blen, (const uint8_t*)input, strlen(input)) == 0) len = blen;
      if (len != 16 && len != 32) { toast("Key must be 16 or 32 bytes"); return; }
      joinChannel(new_channel, secret, len);
      return;
    }

    case F_FREQ: {
      float f = atof(input);
      if (f < 868.0f || f > 923.0f) { toast("Out of range 868-923"); return; }
      prefs.freq = f;
      prefs.region = REGION_CUSTOM;
      prefsSave();
      the_mesh.applyRadio();
      toast("Radio retuned");
      break;
    }

    case F_CLOCK: {
      int y, mo, d, h, mi;
      if (sscanf(input, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) != 5 || y < 2025 || y > 2100 ||
          mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) {
        toast("Format: YYYY-MM-DD HH:MM");
        return;
      }
      struct tm t = {};
      t.tm_year = y - 1900;
      t.tm_mon = mo - 1;
      t.tm_mday = d;
      t.tm_hour = h;
      t.tm_min = mi;
      the_mesh.setClock(mktime(&t) - prefs.tz_hours * 3600, CLOCK_MANUAL);   // no TZ is set: mktime is UTC
      toast("Clock set");
      break;
    }
  }
  endEdit();
}

static void drawEdit() {
  const EditField& f = FIELDS[edit_field];
  drawStatus(prefs.onboarded ? f.title : "SETUP 2/4");
  drawRow(0, false, f.prompt);
  drawInput(2, input, 2);
  drawRow(4, false, f.help, NULL, C_DIM);
  drawHints("ENT ok   ` back");
}

static void keyEdit(const Key& k) {
  editText(input, FIELDS[edit_field].max_len, k);
  if (k.code == K_ENTER) keyEditEnter();
  else if (k.code == K_BACK && !prefs.onboarded) go(S_REGION);
  else if (k.code == K_BACK) endEdit();
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
  drawStatus(from_settings ? "SET Region" : "SETUP 1/4");
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
      sel = 1;
    } else {
      startEdit(F_NAME, prefs.name, S_REGION);
    }
  } else if (k.code == K_BACK && from_settings) {
    go(S_SETTINGS);
    sel = 1;
  }
}

static void drawTz() {
  char s[16];
  drawStatus("SETUP 3/4");
  drawRow(0, false, "Your time zone:");
  snprintf(s, sizeof(s), "< UTC%+d >", prefs.tz_hours);
  drawRow(2, false, s);
  drawRow(4, false, "e.g. US Pacific -8, summer -7", NULL, C_DIM);
  drawRow(5, false, "Clock sets from GPS or mesh.", NULL, C_DIM);
  drawHints(", / change  ENT ok  ` back");
}

static void keyTz(const Key& k) {
  if (k.code == K_LEFT || k.code == K_RIGHT) {
    prefs.tz_hours = constrain(prefs.tz_hours + (k.code == K_RIGHT ? 1 : -1), -12, 14);
  } else if (k.code == K_ENTER) {
    go(S_CONFIRM);
  } else if (k.code == K_BACK) {
    startEdit(F_NAME, prefs.name, S_REGION);
  }
}

static void drawConfirm() {
  char s[COLS + 1];
  drawStatus("SETUP 4/4");
  drawRow(0, false, "Name", prefs.name);
  drawRow(1, false, "Region", PRESETS[prefs.region].title);
  snprintf(s, sizeof(s), "%.3f MHz  %d dBm", prefs.freq, prefs.tx_power);
  drawRow(2, false, s);
  snprintf(s, sizeof(s), "SF%d  BW%g  CR%d", prefs.sf, prefs.bw, prefs.cr);
  drawRow(3, false, s);
  snprintf(s, sizeof(s), "UTC%+d", prefs.tz_hours);
  drawRow(4, false, "Time zone", s);
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
    openChannel(0, S_INBOX);
  } else if (k.code == K_BACK) {
    go(S_TZ);
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
    if (armed && top + r == sel) drawRow(r, true, "X again to delete", NULL, C_RED);
    else drawRow(r, top + r == sel, name, right);
  }
  drawHints("ENT open  A add chan  X del");
}

static void keyInbox(const Key& k) {
  buildConvs();
  listMove(sel, top, nconvs, k);
  bool x = k.code == K_CHAR && (k.ch == 'x' || k.ch == 'X');
  if (!x) armed = false;

  if (k.code == K_ENTER && nconvs) {
    openChat(convs[sel].conv, S_INBOX);
  } else if (k.code == K_CHAR && (k.ch == 'a' || k.ch == 'A')) {
    startEdit(F_CH_NAME, "#", S_INBOX);
  } else if (x && nconvs) {
    const uint8_t* conv = convs[sel].conv;
    int idx = conv[0] == CONV_CHANNEL ? the_mesh.channelByConv(conv) : -1;
    if (idx == 0) {
      toast("Public can't be removed");
    } else if (!armed) {
      armed = true;
    } else {
      armed = false;
      if (idx > 0) the_mesh.leaveChannel(idx); else msgsDeleteConv(conv);
      toast(idx > 0 ? "Left channel" : "Thread deleted");
    }
  }
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
      ContactInfo* c = the_mesh.contactByConv(m->conv);
      snprintf(sender, sizeof(sender), "%s", c ? c->name : "?");
    }

    Line msg_lines[12];
    char hdr[48], t[8];
    fmtTime(t, m->ts);
    static const char* STATUS[] = { "", " [..]", "", " [ok]", " [x]" };
    char st[16];
    if (is_channel && m->out && m->status == ST_SENT) {   // no ACKs on channels: show repeaters heard re-sending it
      if (m->attempt) snprintf(st, sizeof(st), " [heard %d]", m->attempt); else strcpy(st, " [sent]");
    } else {
      strcpy(st, STATUS[m->status]);
    }
    snprintf(hdr, sizeof(hdr), "%s %s%s", t, sender, st);
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
  int compose = inputRows(input, 3);   // compose line grows up to 3 rows, history gets the rest
  const int visible = BODY_ROWS - compose;
  int n = buildChatLines(visible + chat_scroll);
  if (chat_scroll > n - visible) chat_scroll = max(0, n - visible);
  for (int r = 0; r < visible; r++) {
    int idx = chat_scroll + (visible - 1 - r);
    if (idx < n) drawRow(r, false, lines[idx].text, NULL, lines[idx].color);
  }
  if (n == 0) drawRow(1, false, "No messages yet.", NULL, C_DIM);
  drawInput(visible, input, compose);
  drawHints(cur_conv[0] == CONV_CHANNEL ? "ENT send  Fn+R @  Fn;. scroll" : "ENT send  Fn;. scroll ` back");
}

// Puts "@[sender] " (the MeshCore apps' mention format) in front of the compose line. Each press goes one
// distinct sender further back, newest first, and wraps around.
static void mentionSender(int max_len) {
  char names[8][32];
  int n = 0;
  for (int i = msgCount() - 1; i >= 0 && n < 8; i--) {
    Msg* m = msgAt(i);
    const char* p = strstr(m->text, ": ");
    if (m->out || !p || memcmp(m->conv, cur_conv, CONV_ID_LEN) != 0) continue;
    int l = min((int)(p - m->text), 31);
    memcpy(names[n], m->text, l);
    names[n][l] = 0;
    bool seen = false;
    for (int j = 0; j < n; j++) seen |= strcmp(names[j], names[n]) == 0;
    if (!seen) n++;
  }
  if (n == 0) { toast("No one to reply to yet"); return; }

  const char* body = input;   // drop a previous mention
  if (strncmp(input, "@[", 2) == 0 && strstr(input, "] ")) body = strstr(input, "] ") + 2;
  char s[MAX_TEXT_LEN + 1];
  snprintf(s, sizeof(s), "@[%s] %s", names[reply_back % n], body);
  s[max_len] = 0;
  strcpy(input, s);
  reply_back++;
}

static void keyChat(const Key& k) {
  bool is_channel = cur_conv[0] == CONV_CHANNEL;
  int max_len = is_channel ? MAX_TEXT_LEN - strlen(prefs.name) - 2 : MAX_TEXT_LEN;
  if (is_channel && k.code == K_CHAR && k.fn && (k.ch == 'r' || k.ch == 'R')) {
    mentionSender(max_len);
    return;
  }
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
    reply_back = 0;
  }
}

// ================================================================ contacts / repeaters

// A list item is a stored contact, an advert heard but not added ("+"), or a discovery reply.
#define IT_HEARD  0x8000
#define IT_DISC   0x4000
struct ContactItem { uint16_t idx; uint32_t key; };   // sorted by key, highest first
static ContactItem citems[MAX_CONTACTS + MAX_DISCOVERED + MAX_HEARD];
static int ncitems;

static bool matchesFilter(const char* name) {
  int fl = strlen(filter);
  if (fl == 0) return true;
  for (const char* p = name; *p; p++) if (strncasecmp(p, filter, fl) == 0) return true;
  return false;
}

static void sortItems() {
  qsort(citems, ncitems, sizeof(ContactItem), [](const void* a, const void* b) {
    uint32_t ka = ((const ContactItem*)a)->key, kb = ((const ContactItem*)b)->key;
    return ka == kb ? 0 : (ka < kb ? 1 : -1);
  });
}

static void addHeardItems(bool repeaters) {
  for (int i = 0; i < the_mesh.num_heard; i++) {
    const ContactInfo& h = the_mesh.heard[i];
    if ((h.type == ADV_TYPE_REPEATER) != repeaters || !matchesFilter(h.name)) continue;
    citems[ncitems++] = { (uint16_t)(IT_HEARD | i), h.lastmod >> 1 };
  }
}

// contacts: favourites first, then by last heard
static void buildContacts() {
  ncitems = 0;
  ContactInfo c;
  for (int i = 0; i < the_mesh.getTotalContactSlots(); i++) {
    if (!the_mesh.getContactByIdx(i, c) || c.type == ADV_TYPE_NONE || c.type == ADV_TYPE_REPEATER) continue;
    if (matchesFilter(c.name)) citems[ncitems++] = { (uint16_t)i, (c.isFav() ? 0x80000000 : 0) | (c.lastmod >> 1) };
  }
  addHeardItems(false);
  sortItems();
}

// repeaters: discovery replies first (strongest first), then the rest by last heard
static void buildRepeaters() {
  ncitems = 0;
  for (int i = 0; i < the_mesh.num_discovered; i++) {
    citems[ncitems++] = { (uint16_t)(IT_DISC | i), 0x80000000 | (uint32_t)(the_mesh.discovered[i].snr_in4 + 128) };
  }
  ContactInfo c;
  for (int i = 0; i < the_mesh.getTotalContactSlots(); i++) {
    if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_REPEATER) continue;
    bool replied = false;
    for (int d = 0; d < the_mesh.num_discovered; d++) {
      replied |= memcmp(the_mesh.discovered[d].pub_key, c.id.pub_key, PUB_KEY_SIZE) == 0;
    }
    if (!replied) citems[ncitems++] = { (uint16_t)i, c.lastmod >> 1 };
  }
  addHeardItems(true);
  sortItems();
}

static void fmtHops(char* dest, const ContactInfo& c) {
  if (c.out_path_len == OUT_PATH_UNKNOWN) strcpy(dest, "?");
  else if ((c.out_path_len & 63) == 0) strcpy(dest, "D");
  else sprintf(dest, "%d", c.out_path_len & 63);
}

static void drawContactRow(int r, const ContactItem& it) {
  char left[36], right[16], hops[4], age[8];
  uint16_t color = C_AMBER;
  if (it.idx & IT_DISC) {
    const Discovered& d = the_mesh.discovered[it.idx & 0xFF];
    ContactInfo* c = the_mesh.lookupContactByPubKey(d.pub_key, PUB_KEY_SIZE);
    if (c) {
      snprintf(left, sizeof(left), "%s", c->name);
    } else {
      left[0] = '+';
      mesh::Utils::toHex(&left[1], d.pub_key, 4);
      color = C_DIM;
    }
    snprintf(right, sizeof(right), "%+d/%+d", d.snr_in4 / 4, d.snr_out4 / 4);   // SNR dB heard by us / by them
  } else {
    ContactInfo c;
    if (it.idx & IT_HEARD) {
      c = the_mesh.heard[it.idx & 0xFF];
      color = C_DIM;
    } else {
      the_mesh.getContactByIdx(it.idx, c);
    }
    const char* tag = it.idx & IT_HEARD ? "+" : c.isFav() ? "*" : "";
    const char* type = c.type == ADV_TYPE_ROOM ? "[R]" : c.type == ADV_TYPE_SENSOR ? "[S]" : "";
    snprintf(left, sizeof(left), "%s%s%s", tag, type, c.name);
    int8_t snr4;
    uint32_t at;
    if (c.type == ADV_TYPE_REPEATER && the_mesh.repeaterSnr(c.id.pub_key, snr4, at)) {   // signal of its last relay to us
      fmtAge(age, at);
      snprintf(right, sizeof(right), "%+ddB %4s", snr4 / 4, age);
    } else {
      fmtHops(hops, c);
      fmtAge(age, c.lastmod);
      snprintf(right, sizeof(right), "%s %4s", hops, age);
    }
  }
  drawRow(r, top + r == sel, left, right, color);
}

static void drawContacts(bool repeaters) {
  if (repeaters) buildRepeaters(); else buildContacts();
  listMove(sel, top, ncitems, Key{K_NONE});
  char title[24];
  if (repeaters || !filter[0]) strcpy(title, repeaters ? "3 RPT" : "2 CON");
  else snprintf(title, sizeof(title), "CON /%s", filter);
  drawStatus(title);
  for (int r = 0; r < BODY_ROWS && top + r < ncitems; r++) drawContactRow(r, citems[top + r]);

  if (ncitems == 0 && filter[0] && !repeaters) {
    drawRow(1, false, "No match.  ` clears filter", NULL, C_DIM);
  } else if (ncitems == 0 && repeaters) {
    drawRow(1, false, "No repeaters heard yet.", NULL, C_DIM);
    drawRow(2, false, "D: ask nearby repeaters", NULL, C_DIM);
    drawRow(3, false, "to reply (60 s).", NULL, C_DIM);
  } else if (ncitems == 0) {
    drawRow(1, false, "No contacts yet.", NULL, C_DIM);
    drawRow(2, false, "They appear when their", NULL, C_DIM);
    drawRow(3, false, "adverts are received.", NULL, C_DIM);
  }
  char hint[COLS + 1];
  int replies = the_mesh.num_discovered;
  if (repeaters && the_mesh.discovering())
    snprintf(hint, sizeof(hint), "Listening %2ds: %d repl%s", the_mesh.discoverSecsLeft(), replies, replies == 1 ? "y" : "ies");
  else if (repeaters && the_mesh.discoveryRan())
    snprintf(hint, sizeof(hint), "D again  last: %d repl%s", replies, replies == 1 ? "y" : "ies");
  else if (repeaters) strcpy(hint, "ENT info  D discover  +=new");
  else strcpy(hint, filter[0] ? "ENT info  ` clear filter" : "ENT info  type to filter");
  drawHints(hint);
}

static void keyContacts(const Key& k, bool repeaters) {
  if (repeaters) buildRepeaters(); else buildContacts();
  listMove(sel, top, ncitems, k);

  if (!repeaters) {   // type-to-filter
    int fl = strlen(filter);
    if (k.code == K_CHAR && isalpha(k.ch) && fl < (int)sizeof(filter) - 1) {
      filter[fl] = k.ch;
      filter[fl + 1] = 0;
      sel = top = 0;
    } else if (k.code == K_DEL && fl) {
      filter[fl - 1] = 0;
    } else if (k.code == K_BACK) {
      filter[0] = 0;
    }
  } else if (k.code == K_CHAR && (k.ch == 'd' || k.ch == 'D')) {
    the_mesh.startDiscovery();
    toast("Asking nearby repeaters");
  }
  if (k.code != K_ENTER || !ncitems) return;

  // Enter: open details; "+" items are added to contacts first
  uint16_t idx = citems[sel].idx;
  uint8_t key[PUB_KEY_SIZE];
  bool added = true;
  if (idx & IT_HEARD) {
    memcpy(key, the_mesh.heard[idx & 0xFF].id.pub_key, PUB_KEY_SIZE);
    added = the_mesh.addHeard(idx & 0xFF);
  } else if (idx & IT_DISC) {
    memcpy(key, the_mesh.discovered[idx & 0xFF].pub_key, PUB_KEY_SIZE);
    if (!the_mesh.lookupContactByPubKey(key, PUB_KEY_SIZE)) added = the_mesh.addDiscovered(idx & 0xFF);
  } else {
    ContactInfo c;
    the_mesh.getContactByIdx(idx, c);
    memcpy(key, c.id.pub_key, PUB_KEY_SIZE);
  }
  if (!added) { toast("Contacts full"); return; }
  ContactInfo* c = the_mesh.lookupContactByPubKey(key, PUB_KEY_SIZE);
  if (!c) return;
  convOfContact(cur_conv, *c);
  contact_back = screen;
  go(S_CONTACT);
}

static void drawContact() {
  ContactInfo* c = the_mesh.contactByConv(cur_conv);
  if (!c) { go(contact_back); return; }
  static const char* TYPES[] = { "?", "Chat", "Repeater", "Room", "Sensor" };
  char s[COLS + 1], hex[17];
  drawStatus(c->name);
  drawRow(0, false, "Name", c->name);
  snprintf(s, sizeof(s), "%s%s", TYPES[c->type <= ADV_TYPE_SENSOR ? c->type : 0], c->isFav() ? " *fav" : "");
  drawRow(1, false, "Type", s);
  mesh::Utils::toHex(hex, c->id.pub_key, 8);
  drawRow(2, false, "ID", hex);
  if (c->out_path_len == OUT_PATH_UNKNOWN) strcpy(s, "flood (unknown)");
  else if ((c->out_path_len & 63) == 0) strcpy(s, "direct");
  else snprintf(s, sizeof(s), "%d hops", c->out_path_len & 63);
  drawRow(3, false, "Path", s);
  fmtAge(s, c->lastmod);
  int8_t snr4;
  uint32_t at;
  if (c->type == ADV_TYPE_REPEATER && the_mesh.repeaterSnr(c->id.pub_key, snr4, at)) {
    char age[8];
    fmtAge(age, at);
    snprintf(s, sizeof(s), "SNR %.1fdB %s", snr4 / 4.0f, age);
    drawRow(4, false, "Signal", s);
  } else {
    drawRow(4, false, "Heard", s);
  }
  if (armed) {
    drawRow(5, true, "X again to delete", NULL, C_RED);
  } else if (c->gps_lat || c->gps_lon) {
    snprintf(s, sizeof(s), "%.4f,%.4f", c->gps_lat / 1e6, c->gps_lon / 1e6);
    drawRow(5, false, "GPS", s);
  }
  drawHints(c->type == ADV_TYPE_CHAT ? "ENT msg F fav R rst S shr Xdel" : "F fav  R reset  S share  X del");
}

static void keyContact(const Key& k) {
  ContactInfo* c = the_mesh.contactByConv(cur_conv);
  if (k.code == K_BACK || !c) { go(contact_back); return; }
  char ch = k.code == K_CHAR ? toupper(k.ch) : 0;
  if (ch != 'X') armed = false;

  if (k.code == K_ENTER && c->type == ADV_TYPE_CHAT) {
    openChat(cur_conv, S_CONTACT);
  } else if (ch == 'F') {
    c->flags ^= 1;
    storeMarkContactsDirty();
    toast(c->isFav() ? "Favourite" : "Not a favourite");
  } else if (ch == 'R') {
    the_mesh.resetPathTo(*c);
    storeMarkContactsDirty();
    toast("Path reset: next msg floods");
  } else if (ch == 'S') {
    toast(the_mesh.shareContactZeroHop(*c) ? "Shared (zero-hop advert)" : "No advert stored yet");
  } else if (ch == 'X') {
    if (!armed) { armed = true; return; }
    the_mesh.deleteContact(*c);
    toast("Contact deleted");
    go(contact_back);
  }
}

// ================================================================ radio

static void fmtCount(char* dest, uint32_t n) {   // at most 4 chars: 9999, 12k, 999k, 12M
  if (n < 10000) sprintf(dest, "%u", (unsigned)n);
  else if (n < 1000000) sprintf(dest, "%uk", (unsigned)(n / 1000));
  else sprintf(dest, "%uM", (unsigned)(n / 1000000));
}

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
  float air = the_mesh.getTotalAirTime() * 100.0f / max(1UL, millis());   // our TX share of time since boot
  char rx[8], tx[8];
  fmtCount(rx, radio_driver.getPacketsRecv());
  fmtCount(tx, radio_driver.getPacketsSent());
  snprintf(s, sizeof(s), "RX%-4s TX%-4s AIR%.1f%% %.2fV", rx, tx, air, board.getBattMilliVolts() / 1000.0f);
  drawRow(5, false, s);
  drawHints("ENT advert  Z zero-hop adv");
}

static void keyRadio(const Key& k) {
  if (k.code == K_ENTER) toast(the_mesh.sendAdvert(true) ? "Advert sent (flood)" : "Advert failed");
  else if (k.code == K_CHAR && (k.ch == 'z' || k.ch == 'Z'))
    toast(the_mesh.sendAdvert(false) ? "Advert sent (zero-hop)" : "Advert failed");
}

// ================================================================ settings

enum { SET_NAME, SET_REGION, SET_FREQ, SET_BW, SET_SF, SET_CR, SET_TX, SET_HASH, SET_AUTOADD, SET_OVERWRITE,
       SET_BRIGHT, SET_SCREEN, SET_BEEP, SET_TZ, SET_CLOCK, SET_ID, SET_VERSION, SET_RESET, SET_COUNT };
static const float BWS[] = { 7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250, 500 };
#define NUM_BWS (int)(sizeof(BWS) / sizeof(BWS[0]))
struct AddMode { uint8_t mask; const char* label; };
static const AddMode ADD_MODES[] = {
  { ADD_CHAT_RPT, "chat+rpt" },
  { 0x1E, "all" },
  { 1 << ADV_TYPE_CHAT, "chat" },
  { 0, "off" },
};
#define NUM_ADD_MODES (int)(sizeof(ADD_MODES) / sizeof(ADD_MODES[0]))

static void fmtClock(char* dest, int sz) {   // local "YYYY-MM-DD HH:MM", the clock editor's format
  time_t t = the_mesh.now() + prefs.tz_hours * 3600;
  struct tm tm;
  gmtime_r(&t, &tm);
  strftime(dest, sz, "%Y-%m-%d %H:%M", &tm);
}

static int addModeIdx() {
  for (int i = 0; i < NUM_ADD_MODES; i++) if (ADD_MODES[i].mask == prefs.auto_add) return i;
  return 0;
}

static void drawSettings() {
  listMove(sel, top, SET_COUNT, Key{K_NONE});
  drawStatus("5 SET");
  for (int r = 0; r < BODY_ROWS && top + r < SET_COUNT; r++) {
    int i = top + r;
    char v[24] = "";
    const char* label = "";
    switch (i) {
      case SET_NAME:    label = "Name"; snprintf(v, sizeof(v), "%.16s", prefs.name); break;
      case SET_REGION:  label = "Region";
        snprintf(v, sizeof(v), "%.18s", prefs.region < NUM_PRESETS ? PRESETS[prefs.region].title : "Custom"); break;
      case SET_FREQ:    label = "Frequency"; snprintf(v, sizeof(v), "%.3f", prefs.freq); break;
      case SET_BW:      label = "Bandwidth"; snprintf(v, sizeof(v), "< %g >", prefs.bw); break;
      case SET_SF:      label = "Spreading"; snprintf(v, sizeof(v), "< SF%d >", prefs.sf); break;
      case SET_CR:      label = "Coding rate"; snprintf(v, sizeof(v), "< 4/%d >", prefs.cr); break;
      case SET_TX:      label = "TX power"; snprintf(v, sizeof(v), "< %d dBm >", prefs.tx_power); break;
      case SET_HASH:    label = "Path hash"; snprintf(v, sizeof(v), "< %d byte >", prefs.path_hash_size); break;
      case SET_AUTOADD: label = "Auto-add"; snprintf(v, sizeof(v), "< %s >", ADD_MODES[addModeIdx()].label); break;
      case SET_OVERWRITE: label = "Overwrite old"; strcpy(v, prefs.overwrite ? "< on >" : "< off >"); break;
      case SET_BRIGHT:  label = "Brightness"; snprintf(v, sizeof(v), "< %d/4 >", prefs.brightness + 1); break;
      case SET_SCREEN:  label = "Screen off";
        if (SCREEN_OFF_SECS[prefs.screen_off] == 0) strcpy(v, "< never >");
        else snprintf(v, sizeof(v), "< %ds >", SCREEN_OFF_SECS[prefs.screen_off]);
        break;
      case SET_BEEP:    label = "Beep"; strcpy(v, prefs.beep ? "< on >" : "< off >"); break;
      case SET_TZ:      label = "Time zone"; snprintf(v, sizeof(v), "< UTC%+d >", prefs.tz_hours); break;
      case SET_CLOCK: {
        static const char* SRC[] = { "Clock", "Clock (mesh)", "Clock (set)", "Clock (gps)" };
        label = SRC[the_mesh.clockSource()];
        if (the_mesh.clockSynced()) fmtClock(v, sizeof(v)); else strcpy(v, "not set");
        break;
      }
      case SET_ID:      label = "My ID"; mesh::Utils::toHex(v, the_mesh.selfPubKey(), 4); break;
      case SET_VERSION: label = "Firmware"; snprintf(v, sizeof(v), "v%s", FW_VERSION); break;
      case SET_RESET:   label = armed ? "ENT again to WIPE ALL" : "Factory reset"; break;
    }
    drawRow(r, i == sel, label, v, i == SET_RESET && armed ? C_RED : C_AMBER);
  }
  drawHints("ENT edit   , / change");
}

static void keySettings(const Key& k) {
  listMove(sel, top, SET_COUNT, k);
  if (k.code != K_UP && k.code != K_DOWN && sel != SET_RESET) armed = false;
  int d = k.code == K_RIGHT || k.code == K_ENTER ? 1 : k.code == K_LEFT ? -1 : 0;

  if (k.code == K_ENTER) {
    char s[24];
    switch (sel) {
      case SET_NAME: startEdit(F_NAME, prefs.name, S_SETTINGS); return;
      case SET_FREQ:
        snprintf(s, sizeof(s), "%.3f", prefs.freq);
        startEdit(F_FREQ, s, S_SETTINGS);
        return;
      case SET_CLOCK:
        if (the_mesh.clockSynced()) fmtClock(s, sizeof(s)); else strcpy(s, "2026-");
        startEdit(F_CLOCK, s, S_SETTINGS);
        return;
      case SET_REGION:
        from_settings = true;
        go(S_REGION);
        sel = prefs.region < NUM_PRESETS ? prefs.region : 0;
        listMove(sel, top, NUM_PRESETS, Key{K_NONE});
        return;
      case SET_RESET:
        if (armed) storeWipe();
        armed = true;
        return;
    }
  }
  if (d == 0) return;
  switch (sel) {
    case SET_BW: {
      int i = 0;
      while (i < NUM_BWS - 1 && BWS[i] < prefs.bw - 0.01f) i++;
      prefs.bw = BWS[constrain(i + d, 0, NUM_BWS - 1)];
      break;
    }
    case SET_SF:     prefs.sf = constrain(prefs.sf + d, 5, 12); break;
    case SET_CR:     prefs.cr = constrain(prefs.cr + d, 5, 8); break;
    case SET_TX:     prefs.tx_power = constrain(prefs.tx_power + d, 2, MAX_LORA_TX_POWER); break;
    case SET_HASH:   prefs.path_hash_size = constrain(prefs.path_hash_size + d, 1, 3); break;
    case SET_AUTOADD: prefs.auto_add = ADD_MODES[(addModeIdx() + NUM_ADD_MODES + d) % NUM_ADD_MODES].mask; break;
    case SET_OVERWRITE: prefs.overwrite = !prefs.overwrite; break;
    case SET_BRIGHT: prefs.brightness = (prefs.brightness + 4 + d) % 4; applyBrightness(); break;
    case SET_SCREEN: prefs.screen_off = (prefs.screen_off + 6 + d) % 6; break;
    case SET_BEEP:   prefs.beep = !prefs.beep; break;
    case SET_TZ:     prefs.tz_hours = constrain(prefs.tz_hours + d, -12, 14); break;
    default: return;
  }
  if (sel == SET_BW || sel == SET_SF || sel == SET_CR) prefs.region = REGION_CUSTOM;
  if (sel >= SET_BW && sel <= SET_TX) the_mesh.applyRadio();
  prefsSave();
}

// ================================================================ dispatch

void screenDraw() {
  switch (screen) {
    case S_REGION:    drawRegion(); break;
    case S_EDIT:      drawEdit(); break;
    case S_TZ:        drawTz(); break;
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
  bool typing = screen == S_CHAT || screen == S_EDIT;

  // tab switching: Tab cycles, digits jump (Fn+digit in a chat). Not while onboarding or editing a field.
  if (prefs.onboarded && screen != S_EDIT) {
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
    case S_EDIT:      keyEdit(k); break;
    case S_TZ:        keyTz(k); break;
    case S_CONFIRM:   keyConfirm(k); break;
    case S_INBOX:     keyInbox(k); break;
    case S_CHAT:      keyChat(k); break;
    case S_CONTACTS:  keyContacts(k, false); break;
    case S_REPEATERS: keyContacts(k, true); break;
    case S_CONTACT:   keyContact(k); break;
    case S_RADIO:     keyRadio(k); break;
    case S_SETTINGS:  keySettings(k); break;
  }
  if (screen != S_REGION) from_settings = false;
}
