# CLAUDE.md

Standalone MeshCore chat client firmware for **M5Stack Cardputer-Adv + Cap LoRa-1262**. Screen + keyboard only,
no phone. Read `PRD.md` (what/why) and `roadmap_plan.md` (phases, architecture) before working.

## Principles

- **KISS.** No frameworks, no abstraction layers, no generic widgets unless used in 3+ places.
  Screens are an `enum` + `switch`. Plain structs and fixed-size arrays.
- **Reuse MeshCore, don't reimplement it.** Protocol, crypto, routing, packet formats come from the
  MeshCore library. When in doubt, copy the behaviour from `examples/companion_radio/MyMesh.cpp`.
- **Build only the current phase.** Don't add hooks "for later".
- **Memory:** no PSRAM. No dynamic allocation after `setup()`; avoid `String`, use `char[]` + `snprintf`.
- **Power:** redraw only when dirty, no busy loops, nothing that blocks `loop()` for more than a few ms.

## Stack

- PlatformIO, Arduino framework, `espressif32` (same platform version as MeshCore's `[esp32_base]`).
- `meshcore-dev/MeshCore` pinned by **commit hash** in `lib_deps` (built via its `build_as_lib.py`).
- `m5stack/M5Cardputer` (M5GFX + keyboard). Display drawn via one full-screen `M5Canvas` sprite.
- Flash FS for persistence (same FS as MeshCore's ESP32 helpers).

## Layout

```
platformio.ini
src/main.cpp         setup/loop, power tick
src/target.*         hardware glue (name required by MeshCore): Cap I/O expander P0 = RF on, radio, battery
src/presets.h        region presets (868–923 MHz only)
src/chat_mesh.*      ChatMesh : BaseChatMesh
src/store.*          prefs, contacts, channels, messages + persistence
src/ui.*             primitives + screen state machine
src/ui_screens.cpp   per-screen draw()/key()
```

## Hardware facts

- LoRa SX1262: NSS 5, DIO1 4, RST 3, BUSY 6, SCK 40, MOSI 14, MISO 39; DIO2 as RF switch; TCXO (3.3 V per prior port — verify).
- Cap I/O expander PI4IOE5V6408 @ 0x43, I2C SDA 8 / SCL 9. Board I2C SDA 2 / SCL 1.
- Battery ADC G10 (×2 divider). G0 = user button (send advert).
- Display 240×135. Band 868–923 MHz only.
- Prior working port to crib pin/init code from: https://github.com/Stachugit/MeshCore-Cardputer-ADV
  (`variants/m5stack_cardputer/`).

## UI rules

- Colours: bg `#000000`, amber `#FF9500`, dim `#7A4800`, selection = inverse, red `#FF3030` only for errors.
- Layout: 14 px status bar · 6-line body · 14 px key-hint bar. Minimum glyph height 8 px; body ~12–16 px.
- Keys: `Tab`/`Fn+1..5` tabs, `Fn`+`; . , /` arrows, `Enter` select/send, `` ` `` back, `Del` backspace, G0 advert.
- Pager feel: in a conversation, typing goes straight into the compose line.

## Commands

```bash
pio run                       # build
pio run -t upload             # flash (USB)
pio device monitor            # serial log (115200)
```

## Working rules

- Never transmit before onboarding has set a region preset.
- After changing radio/mesh code, verify interop against the official MeshCore app (see roadmap exit criteria).
- Check free heap (printed on boot and on the RAD screen) when adding data structures.
- Mark items `(done)` in `roadmap_plan.md` when finished; update `PRD.md` only when scope changes.
