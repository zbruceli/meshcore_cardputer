#!/usr/bin/env python3
"""Renders README screenshots of the Cardputer UI with demo data (no device needed).

Mirrors the drawing rules in src/ui.cpp (240x135, 16 px status bar, six 16 px rows, hint bar) and uses the
device's own 8x16 font from M5GFX. The big frequency digits are drawn as seven-segment glyphs, like Font7.
Colours come from the THEMES table in src/ui.cpp; screens use the default (amber), themes.png shows every scheme.
Usage: python3 scripts/render_screens.py   (after `pio run`, which downloads M5GFX)  ->  docs/screens/*.png
"""
import os, re
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_H = os.path.join(ROOT, ".pio/libdeps/cardputer_adv_lora1262/M5GFX/src/lgfx/Fonts/Ascii8x16.h")
OUT = os.path.join(ROOT, "docs/screens")
SCALE = 2

W, H, ROW_H, BODY_Y, HINT_Y, COLS = 240, 135, 16, 18, 119, 30


def rgb565(v):
    v = int(v, 16)
    r, g, b = v >> 11, (v >> 5) & 0x3F, v & 0x1F
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


THEMES = [(m[0], [rgb565(x) for x in m[1:]]) for m in re.findall(
    r'\{\s*"([^"]+)",\s*0x(\w{4}),\s*0x(\w{4}),\s*0x(\w{4}),\s*0x(\w{4})\s*\}',
    open(os.path.join(ROOT, "src/ui.cpp")).read())]


def set_theme(i):   # C_BG, C_FG, C_DIM, C_ALERT
    global BG, FG, DIM, ALERT
    BG, FG, DIM, ALERT = THEMES[i][1]


set_theme(0)

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
        self.d.rectangle([0, 0, W - 1, ROW_H - 1], fill=FG)
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

    def row(self, r, left, right=None, sel=False, color=None):
        color = color or FG
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
        self.text(">", 0, y, FG)
        self.text(s, 8, y, FG)
        self.d.rectangle([8 + len(s) * 8, y + 13, 8 + len(s) * 8 + 6, y + 14], fill=FG)

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

def chat():   # channel chat
    s = Screen()
    s.status("#bayarea")
    s.row(0, "14:20 Kai", color=DIM)
    s.row(1, "Anyone hiking Mission Peak")
    s.row(2, "tonight?")
    s.row(3, "14:24 me [heard 3]", color=DIM)
    s.row(4, "@[Kai] count me in!")
    s.compose(5, "see you at the trailhea")
    s.hints("ENT send  Fn+R @  Fn;. scroll")
    return s


chat().save("chat")

s = Screen()   # DM with delivery mark
s.status("@Alice")
s.row(0, "14:02 Alice", color=DIM)
s.row(1, "Did you get the new firmware?")
s.row(2, "14:05 me [ok]", color=DIM)
s.row(3, "Yes, works with the app too")
s.row(4, "14:06 me [..]", color=DIM)
s.compose(5, "")
s.hints("ENT quick Fn;. scroll ` back")
s.save("dm")

s = Screen()   # quick replies: Enter on an empty compose line
s.status("@Alice")
for r, t in enumerate(["3 No", "4 Thanks!", "5 On my way", "6 Running late", "7 Where are you?",
                       "0 My location (GPS)"]):
    s.row(r, t, sel=(r == 5))
s.hints("ENT use   0-8 pick   ` back")
s.save("quick")

s = Screen()   # SET > Quick replies
s.status("SET Replies")
for r, t in enumerate(["1 Thanks!", "2 On my way", "3 Running late", "4 Where are you?", "5 At the trailhead",
                       "6 All good here"]):
    s.row(r, t, sel=(r == 4))
s.hints("ENT edit  Del x2 remove ` back")
s.save("quick_set")

s = Screen()   # 2 CON contacts
s.status("2 CON")
for r, (name, right, color) in enumerate([("*Alice", "D  now", FG), ("Kai", "3   6m", FG),
                                          ("Bob", "2   1h", FG), ("[R]Bay Lounge", "1   2h", FG),
                                          ("+Dana", "?   3h", DIM), ("Eve", "?   1d", FG)]):
    s.row(r, name, right, sel=(r == 0), color=color)
s.hints("ENT info  type to filter")
s.save("contacts")

s = Screen()   # contact details
s.status("Alice")
for r, (k, v) in enumerate([("Name", "Alice"), ("Type", "Chat *fav"), ("ID", "a3f19c27e0b4d812"),
                            ("Path", "direct"), ("Heard", "now"), ("GPS", "2.3km NE 37.775,-122.419")]):
    s.row(r, k, v)
s.hints("ENT msg F fav R rst S shr Xdel")
s.save("contact")

s = Screen()   # 3 RPT with a discovery running
s.status("3 RPT")
for r, (name, right, color) in enumerate([("Hilltop", "+6/+4", FG), ("Ridge-2", "+1/-2", FG),
                                          ("+3fa2c1d0", "-5/-7", DIM), ("Downtown", "-4dB  12m", FG),
                                          ("Bay Bridge", "-9dB   1h", FG), ("Twin Peaks", "3   2h", FG)]):
    s.row(r, name, right, sel=(r == 0), color=color)
s.hints("Listening 42s: 3 replies")
s.save("repeaters")

s = Screen()   # 4 RAD
s.status("4 RAD")
s.seven("910.525", W - 26, BODY_Y, FG)
s.text("MHz", W - 24, BODY_Y + 32, DIM)
s.row(3, "SF7 BW62.5 CR5 22dBm")
s.row(4, "RSSI -98 SNR  6.5 NF-112")
s.row(5, "RX1203 TX87  AIR0.4% 4.05V")
s.hints("ENT advert  Z zero-hop adv")
s.save("radio")

s = Screen()   # 5 SET, Bluetooth rows
s.status("5 SET")
for r, (k, v) in enumerate([("Overwrite old", "< on >"), ("Bluetooth", "< on >"), ("App PIN", "482913"),
                            ("Brightness", "< 3/4 >"), ("Colours", "< Amber >"), ("Screen off", "< 30s >")]):
    s.row(r, k, v, sel=(r == 2))
s.hints("ENT edit   , / change")
s.save("settings")

s = Screen()   # 6 TST packets
s.status("6 TST PKTS")
for r, (left, right, tx) in enumerate([("14:31:58 GRP_TXT  F3", "-6.5 3f", False),
                                       ("14:31:57 GRP_TXT  F2", "2.0 a1", False),
                                       ("14:31:52 GRP_TXT  F0", "TX", True),
                                       ("14:31:40 ADVERT   F4", "-9.2 c7", False),
                                       ("14:31:33 ACK      D0", "7.5", False),
                                       ("14:31:31 TXT      D2", "TX", True)]):
    s.row(r, left, right, sel=(r == 0), color=DIM if tx else None)
s.hints("ENT details     , / page")
s.save("test_packets")

s = Screen()   # 6 TST stats
s.status("6 TST STATS")
for r, t in enumerate(["RX 1203  CRC err 31 (2.5%)", "Flood  in 1102   out 52", "Direct in 101    out 35",
                       "Air TX 0.4% 1h 0.6% RX 9%", "Rptrs 7  NF -112  heap 101K", "Loop max 24ms  for 3h12m"]):
    s.row(r, t)
s.hints("R reset         , / page")
s.save("test_stats")

s = Screen()   # 6 TST RSSI graph
s.status("6 TST RSSI")
s.row(0, "RSSI -109  NF -112 dBm")
import random
random.seed(7)
top, bot = BODY_Y + ROW_H + 2, HINT_Y - 4
for x in range(W):
    v, pkt = -112 + random.randint(-3, 3), x in (40, 41, 42, 133, 134, 200, 201, 202, 203)
    if pkt:
        v = -84 if x < 100 else -97 if x < 160 else -71
    elif 90 < x < 110:
        v += 14   # a burst of interference
    y = max(top, min(bot, bot - (v + 130)))
    s.d.line([x, y, x, bot], fill=FG if pkt else DIM)
for x in range(0, W, 4):
    s.img.putpixel((x, bot - (-112 + 130)), FG)
s.hints("bright = packet  , / page")
s.save("test_rssi")

s = Screen()   # 6 TST RUN: tests that transmit
s.status("6 TST RUN")
for r, (k, v) in enumerate([("Hear me", "3/3 3rpt +4"), ("Echo #test", "2/3 avg 2.5"), ("Trace", "back ok"),
                            ("SDR burst", "")]):
    s.row(r, k, v, sel=(r == 1))
s.row(4, "Msg on #test x3, 30s apart,", color=DIM)
s.row(5, "count repeats ([heard N])", color=DIM)
s.hints("ENT run         , / page")
s.save("test_run")

s = Screen()   # trace result: each node and how it heard the previous one
s.status("6 TST TRACE")
for r, (k, v) in enumerate([("Ridge-2", "+5.5dB"), ("Hilltop", "+2.0dB"), ("Ridge-2", "+7.0dB"), ("me", "+6.2dB")]):
    s.row(r, k, v)
s.hints("ENT again        ` back")
s.save("test_trace")

s = Screen()   # 6 TST LOG: wardrive logger running
s.status("6 TST LOG")
s.row(0, "Logging 0:12:31", "842 rec")
s.row(1, "GPS ~5m 37.77490,-122.41940")
for r, (k, v) in enumerate([("Hilltop", "+6.5 +14"), ("Ridge-2", "-3.0  +5"), ("Summit", "-7.8  +0")]):
    s.row(2 + r, k, v)
s.row(5, "Ping 42s ago: 2 rpt", color=DIM)
s.hints("` stop   D ping   , / page")
s.save("test_log")

s = Screen()   # 3 RPT > M: radar of contacts with a location (north up, us in the middle)
s.status("MAP")
s.row(0, "Hilltop", "4.2km NE")
top, bot = BODY_Y + ROW_H, HINT_Y - 2
cy, r, cx = (top + bot) // 2, (bot - top) // 2 - 2, W // 2
for rad in (r, r // 2):
    s.d.ellipse([cx - rad, cy - rad, cx + rad, cy + rad], outline=DIM)
s.d.line([cx - 3, cy, cx + 3, cy], fill=FG)
s.d.line([cx, cy - 3, cx, cy + 3], fill=FG)
s.text("N", 2, top, DIM)
s.d.line([13, top + 2, 13, top + 12], fill=DIM)
s.d.line([13, top + 2, 10, top + 5], fill=DIM)
s.d.line([13, top + 2, 16, top + 5], fill=DIM)
s.text("5km", W - 8 * 3 - 2, bot - ROW_H + 2, DIM)
pk = r / 5.0   # px per km at the 5 km zoom
for dx, dy, rpt, bright in [(3.0, 2.9, True, True), (-2.1, 1.0, True, True), (1.2, -3.6, True, False),
                            (-0.6, -0.8, False, True), (4.4, -1.5, False, True), (-7.5, 3.2, True, False)]:
    x, y = cx + round(dx * pk), cy - round(dy * pk)
    col = FG if bright else DIM
    if rpt:
        s.d.rectangle([x - 1, y - 1, x + 1, y + 1], fill=col)
    else:
        s.d.ellipse([x - 2, y - 2, x + 2, y + 2], outline=col)
x, y = cx + round(3.0 * pk), cy - round(2.9 * pk)
s.d.rectangle([x - 4, y - 4, x + 4, y + 4], outline=FG)
s.hints("ENT info ;. pick ,/ zoom")
s.save("radar")

# every colour scheme, labelled, 4 per row
GAP, LABEL = 8, 20
cols = 4
rows = (len(THEMES) + cols - 1) // cols
sheet = Screen()
sheet.img = Image.new("RGB", (cols * (W + GAP) - GAP, rows * (H + LABEL + GAP) - GAP), (40, 40, 40))
for i, (name, _) in enumerate(THEMES):
    set_theme(i)
    x, y = (i % cols) * (W + GAP), (i // cols) * (H + LABEL + GAP)
    sheet.img.paste(chat().img, (x, y))
    sheet.text(name, x + (W - len(name) * 8) // 2, y + H + 2, (220, 220, 220))
sheet.img.resize((sheet.img.width * SCALE, sheet.img.height * SCALE), Image.Resampling.NEAREST).save(
    os.path.join(OUT, "themes.png"))
set_theme(0)

print("wrote", sorted(os.listdir(OUT)))
