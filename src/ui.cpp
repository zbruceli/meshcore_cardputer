#include "ui.h"
#include "target.h"
#include "chat_mesh.h"

const uint8_t BRIGHTNESS_LEVELS[4] = { 32, 80, 160, 255 };
const uint16_t SCREEN_OFF_SECS[6] = { 15, 30, 60, 120, 300, 0 };

M5Canvas canvas(&M5Cardputer.Display);

static enum { DISP_ON, DISP_DIM, DISP_OFF } disp = DISP_ON;
static uint32_t last_activity = 0, last_tick = 0, last_batt_read = 0;
static uint32_t seen_added = 0, seen_seq = 0;
static bool dirty = true;
static char toast_text[COLS + 1];
static uint32_t toast_until = 0;
static uint8_t batt_pct = 0;

// ---------------------------------------------------------------- primitives

void drawStatus(const char* title) {
  canvas.fillRect(0, 0, SCR_W, ROW_H, C_AMBER);
  canvas.setTextColor(C_BG);
  char t[16];
  toAscii(t, title, 15);
  canvas.drawString(t, 2, 0);

  int unread = 0;
  for (int i = 0; i < msgCount(); i++) unread += msgAt(i)->unread;
  if (unread) {
    char u[6];
    snprintf(u, sizeof(u), "*%d", unread > 99 ? 99 : unread);
    canvas.drawString(u, 124, 0);
  }

  // RSSI of the last packet heard, as 4 bars
  int bars = 0;
  if (radio_ok && radio_driver.getPacketsRecv() > 0) {
    float rssi = radio_driver.getLastRSSI();
    bars = rssi >= -90 ? 4 : rssi >= -100 ? 3 : rssi >= -110 ? 2 : rssi >= -120 ? 1 : 0;
  }
  for (int b = 0; b < 4; b++) {
    int h = 3 + b * 3;
    if (b < bars) canvas.fillRect(150 + b * 4, 14 - h, 3, h, C_BG);
    else canvas.fillRect(150 + b * 4, 13, 3, 1, C_BG);
  }

  char s[12];
  snprintf(s, sizeof(s), "%3d%%", batt_pct);
  canvas.setTextColor(batt_pct <= 10 ? C_RED : C_BG);
  canvas.drawString(s, 166, 0);
  canvas.setTextColor(C_BG);
  fmtTime(s, the_mesh.now());
  canvas.drawString(s, 200, 0);
}

void drawHints(const char* hints) {
  canvas.drawFastHLine(0, HINT_Y - 2, SCR_W, C_DIM);
  if (toast_until && (int32_t)(millis() - toast_until) < 0) {
    canvas.fillRect(0, HINT_Y, SCR_W, ROW_H, C_AMBER);
    canvas.setTextColor(C_BG);
    canvas.drawString(toast_text, 0, HINT_Y);
  } else {
    canvas.setTextColor(C_DIM);
    canvas.drawString(hints, 0, HINT_Y);
  }
}

void drawRow(int row, bool sel, const char* left, const char* right, uint16_t color) {
  int y = BODY_Y + row * ROW_H;
  if (sel) canvas.fillRect(0, y, SCR_W, ROW_H, color);
  canvas.setTextColor(sel ? C_BG : color);
  int right_len = right ? strlen(right) : 0;
  char l[COLS + 1];
  toAscii(l, left, COLS - (right_len ? right_len + 1 : 0));
  canvas.drawString(l, 0, y);
  if (right_len) canvas.drawString(right, SCR_W - right_len * 8, y);
}

void listMove(int& sel, int& top, int count, const Key& k) {
  if (count == 0) { sel = top = 0; return; }
  if (k.code == K_UP) sel = (sel + count - 1) % count;
  if (k.code == K_DOWN) sel = (sel + 1) % count;
  if (sel >= count) sel = count - 1;
  if (sel < top) top = sel;
  if (sel >= top + BODY_ROWS) top = sel - BODY_ROWS + 1;
}

bool editText(char* buf, int max_len, const Key& k) {
  int len = strlen(buf);
  if (k.code == K_DEL && len > 0) {
    buf[len - 1] = 0;
    return true;
  }
  if (k.code == K_CHAR && len < max_len && k.ch >= ' ' && k.ch <= '~') {
    buf[len] = k.ch;
    buf[len + 1] = 0;
    return true;
  }
  return false;
}

void drawInput(int row, const char* buf) {
  int y = BODY_Y + row * ROW_H;
  canvas.setTextColor(C_AMBER);
  canvas.drawString(">", 0, y);
  int len = strlen(buf);
  const char* tail = len > COLS - 2 ? buf + len - (COLS - 2) : buf;   // keep the cursor in view
  canvas.drawString(tail, 8, y);
  int cx = 8 + strlen(tail) * 8;
  if ((millis() / 500) & 1) canvas.fillRect(cx, y + 13, 7, 2, C_AMBER);
}

void toast(const char* text) {
  toAscii(toast_text, text, COLS);
  toast_until = millis() + 3000;
  dirty = true;
}

void fmtTime(char* dest, uint32_t ts) {
  int32_t t = (int32_t)(ts % 86400) + prefs.tz_hours * 3600;
  t = (t + 86400) % 86400;
  sprintf(dest, "%02d:%02d", (int)(t / 3600), (int)(t / 60 % 60));
}

void fmtAge(char* dest, uint32_t ts) {
  uint32_t now = the_mesh.now();
  uint32_t secs = now > ts ? now - ts : 0;
  if (ts == 0) strcpy(dest, "-");
  else if (secs < 60) strcpy(dest, "now");
  else if (secs < 3600) sprintf(dest, "%um", (unsigned)(secs / 60));
  else if (secs < 86400) sprintf(dest, "%uh", (unsigned)(secs / 3600));
  else sprintf(dest, "%ud", (unsigned)(secs / 86400));
}

// The 8x16 font is ASCII only: show each UTF-8 sequence (emoji etc.) as a single '?'.
void toAscii(char* dest, const char* src, int max_len) {
  int n = 0;
  for (const uint8_t* p = (const uint8_t*)src; *p && n < max_len; p++) {
    if (*p < 0x80) dest[n++] = (*p < ' ') ? ' ' : *p;
    else if ((*p & 0xC0) == 0xC0) dest[n++] = '?';   // lead byte; continuation bytes are skipped
  }
  dest[n] = 0;
}

// ---------------------------------------------------------------- input

// Maps Cardputer keys to UI keys. Arrows are Fn + ; . , /  and ` is Esc (back).
static bool readKey(Key& k) {
  static char prev_word[8];
  static Key held;
  static uint32_t held_since = 0, last_repeat = 0;
  auto& kb = M5Cardputer.Keyboard;

  if (kb.isChange()) {
    auto& ks = kb.keysState();
    k = { K_NONE, 0, ks.fn };
    if (ks.enter) k.code = K_ENTER;
    else if (ks.del) k.code = K_DEL;
    else if (ks.tab) k.code = K_TAB;
    else {
      // newest printable key = one that wasn't down before (handles typing rollover)
      for (char c : ks.word) {
        if (!strchr(prev_word, c)) { k.ch = c; break; }
      }
      if (k.ch) {
        k.code = K_CHAR;
        if (k.ch == '`') k.code = K_BACK;
        else if (ks.fn && k.ch == ';') k.code = K_UP;
        else if (ks.fn && k.ch == '.') k.code = K_DOWN;
        else if (ks.fn && k.ch == ',') k.code = K_LEFT;
        else if (ks.fn && k.ch == '/') k.code = K_RIGHT;
      }
    }
    int n = 0;
    for (char c : ks.word) if (n < (int)sizeof(prev_word) - 1) prev_word[n++] = c;
    prev_word[n] = 0;

    held = k;
    held_since = last_repeat = millis();
    return k.code != K_NONE;
  }

  // auto-repeat for delete and arrows
  bool repeatable = held.code == K_DEL || (held.code >= K_UP && held.code <= K_RIGHT);
  uint32_t t = millis();
  if (repeatable && kb.isPressed() && t - held_since > 400 && t - last_repeat > 70) {
    last_repeat = t;
    k = held;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- display power

static void setDisplay(int state) {
  if (state == disp) return;
  if (state == DISP_OFF) {
    M5Cardputer.Display.setBrightness(0);
    M5Cardputer.Display.sleep();
    setCpuFrequencyMhz(80);
  } else {
    if (disp == DISP_OFF) {
      setCpuFrequencyMhz(240);
      M5Cardputer.Display.wakeup();
      dirty = true;
    }
    uint8_t b = BRIGHTNESS_LEVELS[prefs.brightness];
    M5Cardputer.Display.setBrightness(state == DISP_DIM ? b / 4 + 1 : b);
  }
  disp = (decltype(disp))state;
}

static void wake() {
  last_activity = millis();
  setDisplay(DISP_ON);
}

void applyBrightness() {
  disp = DISP_DIM;   // force a re-apply
  setDisplay(DISP_ON);
}

static void powerTick() {
  uint32_t secs = SCREEN_OFF_SECS[prefs.screen_off];
  if (secs == 0) return;
  uint32_t idle = millis() - last_activity;
  if (idle > secs * 1000) setDisplay(DISP_OFF);
  else if (idle > secs * 500) setDisplay(DISP_DIM);
}

// ---------------------------------------------------------------- main loop

void uiBegin() {
  M5Cardputer.Display.setRotation(1);
  canvas.setColorDepth(16);
  canvas.createSprite(SCR_W, 135);
  canvas.setFont(&fonts::AsciiFont8x16);
  batt_pct = board.battPercent();
  seen_added = msg_added;
  go(prefs.onboarded ? S_INBOX : S_REGION);
  if (!radio_ok) toast("RADIO FAIL: check LoRa Cap");
  wake();
}

void uiLoop() {
  M5Cardputer.update();
  uint32_t t = millis();

  Key k;
  if (readKey(k)) {
    bool was_off = disp == DISP_OFF;
    wake();
    if (!was_off) {   // the key that wakes the screen is not acted on
      screenKey(k);
      dirty = true;
    }
  }

  if (M5Cardputer.BtnA.wasPressed() && prefs.onboarded) {   // G0
    wake();
    toast(the_mesh.sendAdvert(true) ? "Advert sent (flood)" : "Advert failed");
  }

  if (msg_added != seen_added) {
    seen_added = msg_added;
    Msg* m = msgAt(msgCount() - 1);
    if (!m->out) {
      if (prefs.beep) M5Cardputer.Speaker.tone(2800, 60);
      if (!screenShowsConv(m->conv)) {
        char name[32];
        ContactInfo* c = m->conv[0] == CONV_DM ? the_mesh.contactByConv(m->conv) : NULL;
        if (c) snprintf(name, sizeof(name), "%s: ", c->name); else name[0] = 0;
        char t2[COLS * 2];
        snprintf(t2, sizeof(t2), "%s%s", name, m->text);
        toast(t2);
      }
      wake();
    }
  }
  if (msg_seq != seen_seq) {
    seen_seq = msg_seq;
    dirty = true;
  }

  if (t - last_tick > 500) {   // clock, signal, cursor blink
    last_tick = t;
    dirty = true;
    if (t - last_batt_read > 10000 || last_batt_read == 0) {
      last_batt_read = t;
      batt_pct = board.battPercent();
    }
  }

  if (dirty && disp != DISP_OFF) {
    canvas.fillSprite(C_BG);
    screenDraw();
    canvas.pushSprite(0, 0);
    dirty = false;
  }

  powerTick();
  delay(disp == DISP_OFF ? 20 : 5);
}
