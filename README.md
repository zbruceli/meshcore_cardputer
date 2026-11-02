# MeshCore Cardputer

A standalone [MeshCore](https://github.com/meshcore-dev/MeshCore) chat client for the
**M5Stack Cardputer-Adv** with the **Cap LoRa-1262** module. Its own screen and keyboard replace the phone app,
like a pocket pager for the mesh: no Bluetooth pairing, no companion app.

Amber-on-black, pager / ham-rig style UI on the 240×135 display. Built on the official MeshCore library, so it
talks to MeshCore phone apps, companion radios, Ripple devices and repeaters.

> **Status:** Phase 1 runs on the hardware. DM delivery confirmations and hashtag channels shared with the phone
> app are built but not yet confirmed against another user. See [roadmap_plan.md](roadmap_plan.md).

## Features

- **Onboarding without a phone:** pick a region preset → name → time zone → on air. Nothing is transmitted
  until a region is chosen.
- **Messaging:** Public channel, `#hashtag` channels, private channels (name + key), direct messages with
  delivery confirmation and retries. Unread counts, beep and on-screen notice for new messages.
- **Contacts:** auto-added from adverts (chat users and repeaters by default), favourites, delete, reset path,
  share, type-to-filter, overwrite-oldest when full.
- **Repeater discovery:** ask nearby repeaters to reply and see them sorted by signal (SNR both ways).
- **Radio info:** big frequency readout, RSSI / SNR / noise floor, packet counts, airtime, battery.
- **Settings:** region or manual radio parameters, TX power, path hash size, auto-add, brightness, screen
  timeout, beep, time zone, clock.
- **Clock:** from the Cap's GPS when it has a fix, otherwise learned from the mesh, or set by hand.
- **Power:** screen dims and turns off when idle, CPU slows down while it's off.

## Hardware

- [M5Stack Cardputer-Adv](https://docs.m5stack.com/en/core/Cardputer-Adv) (ESP32-S3, 8 MB flash)
- [Cap LoRa-1262](https://docs.m5stack.com/en/cap/Cap_LoRa-1262) (SX1262 + GPS). **868–923 MHz only**, so only
  presets in that band are offered (e.g. USA, EU/UK Narrow, Australia, NZ; no 433 MHz).

Make sure the antenna is attached before you finish onboarding.

## Build and flash

Needs [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).

```bash
git clone https://github.com/zbruceli/meshcore_cardputer.git
cd meshcore_cardputer
pio run -t upload       # build and flash over USB-C
pio device monitor      # optional: serial log at 115200
```

If the screen stays dark after flashing, the board is still in download mode: switch it off and on without
holding G0.

## Using it

Tabs: **1 MSG** (inbox) · **2 CON** (contacts) · **3 RPT** (repeaters) · **4 RAD** (radio) · **5 SET** (settings).
The bottom line of every screen shows the keys you can use there.

| Key | Action |
|---|---|
| `Tab`, `1`–`5` | Switch tabs (`Fn`+digit while typing a message) |
| `;` `.` `,` `/` | Up, down, left, right (hold `Fn` while typing) |
| `Enter` | Open / select / send |
| `` ` `` | Back |
| `Del` | Delete a character |
| G0 button | Send a flood advert (announce yourself) |

Per screen:

- **In a chat:** just type; the compose line wraps up to 3 rows. `Enter` sends. In a channel, `Fn+R` starts your
  message with `@[name]` for the latest sender (press again for earlier senders), the apps' mention format.
- **1 MSG:** `A` add a channel: type `#name` for a hashtag channel, or a name and then the key (base64 or hex)
  for a private one. `X` twice leaves a channel or deletes a DM thread.
- **2 CON:** type letters to filter. `Enter` opens a contact (`+` entries are added first). In a contact:
  `Enter` message, `F` favourite, `R` reset path, `S` share, `X` twice to delete.
- **3 RPT:** `D` asks nearby repeaters to reply for 60 s. Replies show SNR heard by you / by them.
- **4 RAD:** `Enter` sends a flood advert, `Z` a zero-hop advert.
- **5 SET:** `,` `/` change a value, `Enter` edits text fields. Factory reset needs `Enter` twice.

Status bar: screen title, unread count, signal of the last packet, battery %, clock (`--:--` until set).

Tips:
- Others only see you as a contact after they hear your advert: press G0 once after setup.
- A DM shows `[..]` while sending, `[ok]` when acknowledged, `[x]` if it failed.
- Time zone is a fixed UTC offset: change it in SET when daylight saving starts or ends.

## Project docs

- [PRD.md](PRD.md): requirements and scope
- [roadmap_plan.md](roadmap_plan.md): status and next phases
- [CLAUDE.md](CLAUDE.md): code layout, hardware facts, working rules

## Credits

- [MeshCore](https://github.com/meshcore-dev/MeshCore): protocol, crypto, routing and radio drivers
- [MeshCore-Cardputer-ADV](https://github.com/Stachugit/MeshCore-Cardputer-ADV): prior port, used for pins and
  hardware init
- [M5Cardputer](https://github.com/m5stack/M5Cardputer) / M5GFX: display and keyboard

## License

[MIT](LICENSE)
