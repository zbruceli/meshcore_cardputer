# CLAUDE.md

Standalone MeshCore chat client firmware for **M5Stack Cardputer-Adv + Cap LoRa-1262**. Screen + keyboard only,
no phone needed (the official MeshCore app can also pair over BLE). `README.md` = user docs, `PRD.md` = what/why, `roadmap_plan.md` = status and next phases.

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
  `/contacts3`, `/channels2`, `/bl/<key>`; UI files `/prefs` (UI settings, onboarded, region label), `/msgs`.
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
```

## Hardware facts

- LoRa SX1262: NSS 5, DIO1 4, RST 3, BUSY 6, SCK 40, MOSI 14, MISO 39; DIO2 = RF switch; TCXO 1.8 V. 868–923 MHz only.
  Radio set up as the sosprz fork: default `SPIClass` (SPI3) at 1 MHz, 140 mA, plus STDBY_XOSC (TCXO kept running).
- Cap I/O expander PI4IOE5V6408 @ 0x43 on internal I2C (SDA 8 / SCL 9, owned by M5Unified): P0 high = RF path on
  (P0 "BYPASS" -> Stamp LoRa-1262 pin "SW"; without it TX is about -15 dBm, per d4rkmen/plai #11).
  Arduino `Wire` is moved to the Grove port (SDA 2 / SCL 1).
- Cap GPS ATGM336H-6N on UART1: ESP RX 15, TX 13, 115200 8N1. No power-enable pin found: always on, streams
  NMEA unless told otherwise (`$PCAS03`). Silenced from boot for now (no GPS clock) while TX is investigated.
- SD card shares the LoRa SPI bus (CS 12, kept high). Don't drive G46 (not a Cap power enable, per newer ports).
- Open problem: we hear repeaters at +2 dB but are almost never heard back, with any radio setting tried (3.3/1.8 V,
  140/240 mA, either SPI bus, GPS silent, XOSC standby, LDO regulator). Official Meshtastic on the same unit fails TX the same way,
  so it isn't our code. RTL-SDR (`scripts/sdr_burst.py`): our TX is within ~0.5 dB of a T-Deck Plus, and the
  T-Deck can't reach the channels from the same window either: it's the location / link budget, not the unit.
- Battery ADC G10 (×2). G0 = user button (flood advert). Display 240×135. Keyboard TCA8418 (auto-detected).
- Prior port to crib from: https://github.com/Stachugit/MeshCore-Cardputer-ADV (`variants/m5stack_cardputer/`).

## UI rules

- Colours: bg `#000000`, amber `#FF9500`, dim `#7A4800`, selection = inverse, red `#FF3030` only for errors.
- Layout: 16 px status bar · 6 rows of 8×16 text (30 columns) · 16 px key-hint bar. Hints must fit 30 chars.
- Keys: `Tab` / `1..5` tabs (`Fn+digit` while typing), `; . , /` arrows (`Fn` needed while typing), `Enter`,
  `` ` `` back, `Del`, letters as screen actions. G0 = advert. Destructive actions need the key pressed twice.
- Pager feel: in a conversation, typing goes straight into the compose line.

## Commands

```bash
pio run                       # build
pio run -t upload             # flash (USB); if it stays in download mode, power-cycle without G0
pio device monitor            # serial log (115200)
./scripts/build_release.sh    # merged image dist/meshcore-cardputer-v<FW_VERSION>.bin (flash at 0x0)
```

Release: bump `FW_VERSION` in `platformio.ini`, commit, push a tag `v<FW_VERSION>`. `.github/workflows/release.yml`
builds the merged `.bin`, attaches it to a GitHub Release and publishes `web/` (ESP Web Tools installer) to
GitHub Pages. M5Burner (which also feeds Launcher) is a manual upload of the same `.bin`.

## Working rules

- Never transmit before onboarding has set a region preset (`the_mesh.loop()` isn't called until then).
- `Prefs` is saved raw: only **append** fields (older, shorter files keep defaults). Don't reorder.
- After changing radio/mesh code, verify interop against the official MeshCore app.
- Check free heap (printed on boot) when adding data structures.
- Keep `roadmap_plan.md` status marks current; update `PRD.md` only when scope changes; `README.md` when keys change.
