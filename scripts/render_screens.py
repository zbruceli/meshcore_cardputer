#!/usr/bin/env python3
"""Renders README screenshots of the Cardputer UI with demo data (no device needed).

Mirrors the drawing rules in src/ui.cpp (240x135, 16 px status bar, six 16 px rows, hint bar) and uses the
device's own 8x16 font from M5GFX. The big frequency digits are drawn as seven-segment glyphs, like Font7.
Usage: python3 scripts/render_screens.py   (after `pio run`, which downloads M5GFX)  ->  docs/screens/*.png
"""
import os, re
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_H = os.path.join(ROOT, ".pio/libdeps/cardputer_adv_lora1262/M5GFX/src/lgfx/Fonts/Ascii8x16.h")
OUT = os.path.join(ROOT, "docs/screens")
SCALE = 2

W, H, ROW_H, BODY_Y, HINT_Y, COLS = 240, 135, 16, 18, 119, 30
BG, AMBER, DIM, RED = (0, 0, 0), (255, 150, 0), (123, 73, 0), (255, 48, 49)   # C_BG, C_AMBER, C_DIM, C_RED

src = open(FONT_H).read()
FONT = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", src[src.index("{"):src.index("};")])]


class Screen:
    def __init__(self):
        self.img = Image.new("RGB", (W, H), BG)
        self.d = ImageDraw.Draw(self.img)

    def text(self, s, x, y, color):
        for ch in s:
            glyph = FONT[ord(ch) * 16: ord(ch) * 16 + 16]
            for r, bits in enumerate(glyph):
                for c in range(8):
                    if bits & (0x80 >> c):
                        self.img.putpixel((x + c, y + r), color)
            x += 8

    def status(self, title, unread=0, bars=3, batt=87, clock="14:32"):
        self.d.rectangle([0, 0, W - 1, ROW_H - 1], fill=AMBER)
        self.text(title[:12], 2, 0, BG)
        if unread:
            self.text("*%d" % unread, 100, 0, BG)
        for b in range(4):
            h = 3 + b * 3
            if b < bars:
                self.d.rectangle([129 + b * 4, 14 - h, 129 + b * 4 + 2, 13], fill=BG)
            else:
                self.d.rectangle([129 + b * 4, 13, 129 + b * 4 + 2, 13], fill=BG)
        self.d.line([149, 3, 149, 12], fill=BG)
        self.text("%3d%%" % batt, 154, 0, BG)
        self.d.line([192, 3, 192, 12], fill=BG)
        self.text(clock, 197, 0, BG)

    def row(self, r, left, right=None, sel=False, color=AMBER):
        y = BODY_Y + r * ROW_H
        if sel:
            self.d.rectangle([0, y, W - 1, y + ROW_H - 1], fill=color)
        fg = BG if sel else color
        room = COLS - (len(right) + 1 if right else 0)
        self.text(left[:room], 0, y, fg)
        if right:
            self.text(right, W - len(right) * 8, y, fg)

    def hints(self, s):
        self.d.line([0, HINT_Y - 2, W - 1, HINT_Y - 2], fill=DIM)
        self.text(s, 0, HINT_Y, DIM)

    def compose(self, r, s):
        y = BODY_Y + r * ROW_H
        self.text(">", 0, y, AMBER)
        self.text(s, 8, y, AMBER)
        self.d.rectangle([8 + len(s) * 8, y + 13, 8 + len(s) * 8 + 6, y + 14], fill=AMBER)

    # seven-segment digits for the RAD frequency readout (Font7 is 48 px tall)
    SEGS = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc",
            "7": "abc", "8": "abcdefg", "9": "abcfgd"}

    def seven(self, s, right_x, y, color, w=26, h=46, t=5, gap=6):
        widths = [8 if ch == "." else w for ch in s]
        x = right_x - sum(widths) - gap * (len(s) - 1)
        for ch, cw in zip(s, widths):
            if ch == ".":
                self.d.rectangle([x + 1, y + h - t, x + t, y + h - 1], fill=color)
            else:
                m = h // 2
                seg = {"a": [x + t, y, x + w - t, y + t - 1], "g": [x + t, y + m - t // 2, x + w - t, y + m + t // 2],
                       "d": [x + t, y + h - t, x + w - t, y + h - 1], "f": [x, y + t, x + t - 1, y + m - 1],
                       "e": [x, y + m + 1, x + t - 1, y + h - t], "b": [x + w - t, y + t, x + w - 1, y + m - 1],
                       "c": [x + w - t, y + m + 1, x + w - 1, y + h - t]}
                for k in self.SEGS[ch]:
                    self.d.rectangle(seg[k], fill=color)
            x += cw + gap

    def save(self, name):
        os.makedirs(OUT, exist_ok=True)
        self.img.resize((W * SCALE, H * SCALE), Image.Resampling.NEAREST).save(os.path.join(OUT, name + ".png"))


def presets():
    s = open(os.path.join(ROOT, "src/presets.h")).read()
    return [(t, float(f)) for t, f in re.findall(r'\{\s*"([^"]+)",\s*([0-9.]+)', s)]


# --- demo data (fictional) ---

s = Screen()   # onboarding: region list
s.status("SETUP 1/4", clock="--:--", bars=0)
ps = presets()
usa = [t for t, _ in ps].index("USA")
for r, (t, f) in enumerate(ps[usa - 3: usa + 3]):
    s.row(r, t, "%.3f" % f, sel=(r == 3))
s.hints("Pick your region   ENT ok")
s.save("setup_region")

s = Screen()   # 1 MSG inbox
s.status("1 MSG", unread=3)
for r, (name, right) in enumerate([("#bayarea", "*2"), ("@Alice", "*1"), ("Public", "4m"), ("@Bob", "1h"),
                                   ("#hiking", "3h"), ("#test", "1d")]):
    s.row(r, name, right, sel=(r == 0))
s.hints("ENT open  A add chan  X del")
s.save("msg_inbox")

s = Screen()   # channel chat
s.status("#bayarea")
s.row(0, "14:20 Kai", color=DIM)
s.row(1, "Anyone hiking Mission Peak")
s.row(2, "tonight?")
s.row(3, "14:24 me [heard 3]", color=DIM)
s.row(4, "@[Kai] count me in!")
s.compose(5, "see you at the trailhea")
s.hints("ENT send  Fn+R @  Fn;. scroll")
s.save("chat")

s = Screen()   # DM with delivery mark
s.status("@Alice")
s.row(0, "14:02 Alice", color=DIM)
s.row(1, "Did you get the new firmware?")
s.row(2, "14:05 me [ok]", color=DIM)
s.row(3, "Yes, works with the app too")
s.row(4, "14:06 me [..]", color=DIM)
s.compose(5, "")
s.hints("ENT send  Fn;. scroll ` back")
s.save("dm")

s = Screen()   # 2 CON contacts
s.status("2 CON")
for r, (name, right, color) in enumerate([("*Alice", "D  now", AMBER), ("Kai", "3   6m", AMBER),
                                          ("Bob", "2   1h", AMBER), ("[R]Bay Lounge", "1   2h", AMBER),
                                          ("+Dana", "?   3h", DIM), ("Eve", "?   1d", AMBER)]):
    s.row(r, name, right, sel=(r == 0), color=color)
s.hints("ENT info  type to filter")
s.save("contacts")

s = Screen()   # contact details
s.status("Alice")
for r, (k, v) in enumerate([("Name", "Alice"), ("Type", "Chat *fav"), ("ID", "a3f19c27e0b4d812"),
                            ("Path", "direct"), ("Heard", "now"), ("GPS", "37.7749,-122.4194")]):
    s.row(r, k, v)
s.hints("ENT msg F fav R rst S shr Xdel")
s.save("contact")

s = Screen()   # 3 RPT with a discovery running
s.status("3 RPT")
for r, (name, right, color) in enumerate([("Hilltop", "+6/+4", AMBER), ("Ridge-2", "+1/-2", AMBER),
                                          ("+3fa2c1d0", "-5/-7", DIM), ("Downtown", "-4dB  12m", AMBER),
                                          ("Bay Bridge", "-9dB   1h", AMBER), ("Twin Peaks", "3   2h", AMBER)]):
    s.row(r, name, right, sel=(r == 0), color=color)
s.hints("Listening 42s: 3 replies")
s.save("repeaters")

s = Screen()   # 4 RAD
s.status("4 RAD")
s.seven("910.525", W - 26, BODY_Y, AMBER)
s.text("MHz", W - 24, BODY_Y + 32, DIM)
s.row(3, "SF7 BW62.5 CR5 22dBm")
s.row(4, "RSSI -98 SNR  6.5 NF-112")
s.row(5, "RX1203 TX87  AIR0.4% 4.05V")
s.hints("ENT advert  Z zero-hop adv")
s.save("radio")

s = Screen()   # 5 SET, Bluetooth rows
s.status("5 SET")
for r, (k, v) in enumerate([("Auto-add", "< chat+rpt >"), ("Overwrite old", "< on >"), ("Bluetooth", "< on >"),
                            ("App PIN", "482913"), ("Brightness", "< 3/4 >"), ("Screen off", "< 30s >")]):
    s.row(r, k, v, sel=(r == 3))
s.hints("ENT edit   , / change")
s.save("settings")

print("wrote", sorted(os.listdir(OUT)))
