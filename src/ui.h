#pragma once
// Pager-style UI: 16px status bar, 6 rows of 8x16 text, 16px key-hint bar. Amber on black.

#include <M5Cardputer.h>

#define C_BG      0x0000
#define C_AMBER   0xFCA0   // #FF9500
#define C_DIM     0x7A40   // #7A4800
#define C_RED     0xF986   // #FF3030

#define SCR_W     240
#define ROW_H     16
#define BODY_Y    18
#define BODY_ROWS 6
#define HINT_Y    119
#define COLS      30       // 240 / 8

enum KeyCode { K_NONE, K_CHAR, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_BACK, K_DEL, K_TAB };
struct Key { uint8_t code; char ch; bool fn; };

enum Screen { S_REGION, S_NAME, S_TZ, S_CONFIRM, S_INBOX, S_CHAT, S_CONTACTS, S_REPEATERS, S_CONTACT, S_RADIO, S_SETTINGS };

extern M5Canvas canvas;
extern const uint8_t BRIGHTNESS_LEVELS[4];
extern const uint16_t SCREEN_OFF_SECS[6];   // 0 = never
extern bool radio_ok;

// ui.cpp: primitives
void drawStatus(const char* title);
void drawHints(const char* hints);
void drawRow(int row, bool sel, const char* left, const char* right = NULL, uint16_t color = C_AMBER);
void listMove(int& sel, int& top, int count, const Key& k);
bool editText(char* buf, int max_len, const Key& k);
void drawInput(int row, const char* buf);
void toast(const char* text);
void fmtTime(char* dest, uint32_t ts);
void fmtAge(char* dest, uint32_t ts);
void toAscii(char* dest, const char* src, int max_len);

// ui_screens.cpp
void go(Screen s);
void screenDraw();
void screenKey(const Key& k);
bool screenShowsConv(const uint8_t conv[]);

void uiBegin();
void applyBrightness();
void uiLoop();
