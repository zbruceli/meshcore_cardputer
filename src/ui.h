#pragma once
// Pager-style UI: 16px status bar, 6 rows of 8x16 text, 16px key-hint bar. Four colours from a theme.

#include <M5Cardputer.h>

// Colour schemes (RGB565). Selection is inverse (bg on fg), so any 4-colour scheme works on every screen.
struct Theme { const char* name; uint16_t bg, fg, dim, alert; };
extern const Theme THEMES[];
extern const int NUM_THEMES;
extern const Theme* theme;   // the current one, from prefs.theme (themeApply)
void themeApply();

#define C_BG      (theme->bg)
#define C_FG      (theme->fg)      // amber #FF9500 in the default theme
#define C_DIM     (theme->dim)
#define C_ALERT   (theme->alert)   // errors, low battery

#define SCR_W     240
#define ROW_H     16
#define BODY_Y    18
#define BODY_ROWS 6
#define HINT_Y    119
#define COLS      30       // 240 / 8

enum KeyCode { K_NONE, K_CHAR, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_BACK, K_DEL, K_TAB };
struct Key { uint8_t code; char ch; bool fn; };

enum Screen { S_REGION, S_EDIT, S_TZ, S_CONFIRM, S_INBOX, S_CHAT, S_CONTACTS, S_REPEATERS, S_CONTACT, S_RADIO, S_SETTINGS };

extern M5Canvas canvas;
extern const uint8_t BRIGHTNESS_LEVELS[4];
extern const uint16_t SCREEN_OFF_SECS[6];   // 0 = never
extern bool radio_ok;

// ui.cpp: primitives
void drawStatus(const char* title);
void drawHints(const char* hints);
void drawRow(int row, bool sel, const char* left, const char* right = NULL, uint16_t color = C_FG);
void listMove(int& sel, int& top, int count, const Key& k);
bool editText(char* buf, int max_len, const Key& k);
int  inputRows(const char* buf, int max_rows);
void drawInput(int row, const char* buf, int rows = 1);
void toast(const char* text);
void fmtTime(char* dest, uint32_t ts);
void fmtAge(char* dest, uint32_t ts);
void toAscii(char* dest, const char* src, int max_len);

// ui_screens.cpp
void go(Screen s);
void screenDraw();
void screenKey(const Key& k);
bool screenShowsConv(const uint8_t conv[]);

void uiSplash();   // right after board.begin(): something on screen while storage is set up
void uiBegin();
void applyBrightness();
void uiLoop();
