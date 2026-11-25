# CLAUDE.md

MeshCore chat firmware for **M5Stack Cardputer-Adv + Cap LoRa-1262**: screen + keyboard, no phone needed, and the
official MeshCore app can pair over BLE. `README.md` = user docs, `PRD.md` = scope, `roadmap_plan.md` = status and
priorities.

## Principles

- **KISS.** No frameworks, no abstraction layers, no generic widgets unless used in 3+ places.
  Screens are an `enum` + `switch`. Plain structs and fixed-size arrays.
- **Reuse MeshCore, don't reimplement it.** Protocol, crypto, routing, packet formats come from the
  MeshCore library. When in doubt, copy the behaviour from `examples/companion_radio/MyMesh.cpp`.
- **Build only the current phase.** Don't add hooks "for later".
- **Memory:** no PSRAM. No dynamic allocation after `setup()`; avoid `String`, use `char[]` + `snprintf`.
- **Power:** redraw only when dirty, no busy loops, nothing that blocks `loop()` for more than a few ms.

## Stack

- PlatformIO, Arduino, `espressif32@6.11.0` (same as MeshCore's `[esp32_base]`), one env `cardputer_adv_lora1262`.
- `meshcore-dev/MeshCore` pinned by commit in `lib_deps`. Its bundled ed25519 comes in through the `lib/ed25519`
  shim + an `-I` flag. Don't include MeshCore's `base64.hpp` (non-inline definitions, already linked) — use mbedtls.
- `m5stack/M5Cardputer` (M5GFX + keyboard). Everything is drawn into one full-screen `M5Canvas` sprite.
- The mesh engine is MeshCore's official companion engine: `examples/companion_radio/MyMesh.cpp` + `DataStore.cpp`,
  compiled unmodified from the pinned library via `build_src_filter` (don't copy or patch them). `the_mesh` must be
  exactly `MyMesh` (its code refers to `extern MyMesh the_mesh`), so the UI hooks in through `MyMesh::Listener`
  (`ChatClient`) and our radio wrapper subclass (`CardputerRadio`: every raw packet in/out, 16-symbol preamble).
- Phone app over BLE (`SerialBLEInterface` + `MultiSerialInterface`), wired as in companion_radio's `main.cpp`.
  BLE costs ~100 KB heap (~104 KB free after boot).
- SPIFFS: engine files `/identity`, `/prefs.json` (NodePrefs: name, radio, auto-add, BLE PIN; shared with the app),
  `/contacts3`, `/channels2`, `/bl/<key>`; UI files `/prefs` (UI settings, onboarded, region label), `/msgs`,
  `/quick` (quick replies).
  The pre-v0.3 `/contacts`, `/channels` and `/prefs` radio fields are only read once to migrate.

## Layout

```
platformio.ini       build flags carry all pins and radio defaults
src/main.cpp         setup/loop: board, radio, mesh (only after onboarding), GPS clock, store, UI
src/target.*         hardware glue (name required by MeshCore): board init, Cap RF switch, radio, battery, GPS time
src/presets.h        region presets (868–923 MHz only)
src/chat_client.*    ChatClient : MyMesh::Listener — message log, DM retries, channels, discovery, clock votes
src/store.*          UI prefs, message log + lazy saves; reads pre-v0.3 contact/channel files to migrate
src/ui.*             primitives (status/hint bars, rows, lists, input, toast), key reading, display power
src/ui_screens.cpp   per-screen draw()/key() + dispatch
src/wardrive.*       wardrive logger (test tab LOG page): repeaters heard + GPS -> CSV on the SD card
```

## Hardware facts

- LoRa SX1262: NSS 5, DIO1 4, RST 3, BUSY 6, SCK 40, MOSI 14, MISO 39; DIO2 = RF switch; TCXO 1.8 V. 868–923 MHz only.
  Radio set up as the sosprz fork: default `SPIClass` (SPI3) at 1 MHz, 140 mA, plus STDBY_XOSC (TCXO kept running).
- Cap I/O expander PI4IOE5V6408 @ 0x43 on internal I2C (SDA 8 / SCL 9, owned by M5Unified): P0 high = RF path on
  (P0 "BYPASS" -> Stamp LoRa-1262 pin "SW"; without it TX is about -15 dBm, per d4rkmen/plai #11).
  Arduino `Wire` is moved to the Grove port (SDA 2 / SCL 1).
- Cap GPS ATGM336H-6N on UART1: ESP RX 15, TX 13, 115200 8N1. No power-enable pin: powered (~30 mA) and
  streaming NMEA unless told otherwise (`$PCAS03`). `$PCAS12,<s>` = standby (6 h accepted), wakes on timer or any
  serial command, keeps settings, fix in ~4 s with a good sky view (20–40 s by a window). Runs only in sessions
  (GGA + RMC) for the clock (6 h), location mode (10 min) or a "my location" request, standby otherwise; its
  wake-up banner starts the next session (`gpsLoop()` in main.cpp). `prefs.gps` = off / clock / location.
- SD card shares the LoRa SPI bus (CS 12, kept high). Mounted (`SD`, FAT32 only, exFAT is off in the framework)
  only while the wardrive logger runs: ~11 KB heap. Don't drive G46 (not a Cap power enable, per newer ports).
- With the GPS tracking, the radio's noise floor reads ~6 dB higher (−111 → −105 dBm indoors).
- "Not heard back" indoors is the location, not the firmware: RTL-SDR (`scripts/sdr_burst.py`) shows our TX within
  ~0.5 dB of a T-Deck Plus, which fails from the same spot too. On a hillside (v0.3.0) both pass the Bay Area health
  check (84–87 % vs 82 %). Judge radio changes on same-spot alternating SDR runs or several sends, outdoors.
- Battery ADC G10 (×2); under 2.5 V = not measured (USB, switch off). G0 = user button (flood advert; held 3 s =
  power off). Waking from deep sleep with G0 boots normally (checked unplugged: not download mode).
  Display 240×135. Keyboard TCA8418 (auto-detected).
- Prior port to crib from: https://github.com/Stachugit/MeshCore-Cardputer-ADV (`variants/m5stack_cardputer/`).

## UI rules

- Colours: only `C_BG` `C_FG` `C_DIM` `C_ALERT` from the current theme (`THEMES` in `ui.cpp`, default amber
  `#FF9500` / dim `#7A4800` on black); selection = inverse; `C_ALERT` only for errors. Never hard-code a colour.
- Layout: 16 px status bar · 6 rows of 8×16 text (30 columns) · 16 px key-hint bar. Hints must fit 30 chars.
- Keys: `Tab` / `1..5` tabs (`6` if the test tab is on; `Fn+digit` while typing), `; . , /` arrows (`Fn` needed
  while typing), `Enter`, `` ` `` back, `Del`, letters as screen actions. G0 = advert (hold 3 s: power off).
  Destructive actions need the key pressed twice.
- Pager feel: in a conversation, typing goes straight into the compose line.

## Commands

```bash
pio run                       # build
pio run -t upload             # flash (USB); if it stays in download mode, power-cycle without G0
pio device monitor            # serial log (115200)
./scripts/build_release.sh    # merged image dist/meshcore-cardputer-v<FW_VERSION>.bin (flash at 0x0)
python3 scripts/render_screens.py   # README screenshots (docs/screens/) from demo data, same layout as ui.cpp
```

Release: bump `FW_VERSION` (and `FIRMWARE_VERSION`) in `platformio.ini`, write `docs/release-notes/v<FW_VERSION>.md`,
commit, push tag `v<FW_VERSION>`; the release workflow builds the `.bin`, attaches it to a GitHub Release (the
notes file above the generated changelog) and publishes `web/` to GitHub Pages.
Installer-page changes alone: `web/` changes on `main` (or a manual run) republish via `pages.yml` with the latest
release's `.bin`. Both workflows split the merged `.bin` (`scripts/split_image.py`) so the web installer skips NVS
(0x9000–0xDFFF, Bluetooth bonds) and the phone pairing survives; M5Burner/esptool users write the merged image and
re-pair.

## Working rules

- Never transmit before onboarding has set a region preset (`the_mesh.loop()` isn't called until then).
- After changing radio/mesh code, verify interop against the official MeshCore app.
- Check free heap (printed on boot) when adding data structures.
- Keep `roadmap_plan.md` status marks current; update `PRD.md` only when scope changes; `README.md` (and
  `scripts/render_screens.py`) when keys or screens change.

## Upgrades and data

v0.3.2 is in users' hands (web installer, M5Burner). Flashing a release must keep identity, settings, contacts,
channels and messages; only an explicit erase or factory reset may lose them.

- Partition table (`default_8MB.csv`, SPIFFS at 0x670000) and the release image layout (no data partition) never
  change: either would wipe or orphan user data.
- `/prefs` is a raw struct: only **append** fields at the end (shorter files keep defaults), never reorder, resize or
  reuse one. Never bump `PREFS_VERSION`: `prefsLoad()` resets everything on a mismatch.
- `/quick` is 8 × 48 raw chars: frozen too (absent = defaults).
- `/msgs` is raw `Msg` records with no header: `Msg`'s layout is frozen. A new layout goes to a new file
  (`/msgs2`) read from `/msgs` once, as the pre-v0.3 migration does.
- Migrations: run once at boot, idempotent, write the new file before relying on it, keep the old file, never write
  over data that failed to parse. Keep migration code for every released format (v0.2 users may jump straight to
  the latest).
- Engine files (`/identity`, `/prefs.json`, `/contacts3`, `/channels2`) belong to MeshCore: before bumping the
  MeshCore commit, check `DataStore.cpp` and NodePrefs for format changes.
- Every release passes the upgrade test in `roadmap_plan.md`. Downgrades are best effort (older builds read only
  the fields they know).
