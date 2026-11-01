# Roadmap & Plan

Principle: ship the smallest thing that works on real hardware, then grow. Each phase ends with a flashable,
tested build. Requirements referenced here are defined in `PRD.md`.

## Architecture (fixed for all phases)

```
platformio.ini          one env: cardputer_adv_lora1262; MeshCore pinned by commit in lib_deps
src/
  main.cpp              setup()/loop(): board init, mesh.loop(), ui.loop(), power tick
  target.h/.cpp         Cardputer-Adv + Cap LoRa init (MeshCore includes <target.h> by this name)
  presets.h             region preset table (copied from api.meshcore.nz, 868–923 MHz only)
  chat_mesh.h/.cpp      ChatMesh : BaseChatMesh — callbacks push into store, send helpers, discovery
  store.h/.cpp          prefs, contacts, channels, message ring; load/save on flash FS
  ui.h/.cpp             drawing primitives (status bar, list, hint bar, text wrap) + screen state machine
  ui_screens.cpp        one draw()/key() pair per screen (split further only if > ~800 lines)
```

Rules: no UI framework (no LVGL), no event bus, no plugin system. Screens are an `enum` + `switch`.
Draw into one full-screen `M5Canvas` sprite and push it (no flicker), redraw only when state is dirty.

## MVP — "talk on Public, DM a friend"

Goal: a phone-free device that can join a mesh and chat. Target ~2 weeks.

1. **Project skeleton**: PlatformIO env, M5Cardputer lib, MeshCore lib dep, board init, "hello" screen, serial log.
2. **Radio up**: SX1262 via MeshCore `CustomSX1262Wrapper` with Cap pins + I/O expander P0 high, DIO2 RF switch,
   TCXO; verify RX of adverts printed to serial.
3. **Identity + store**: generate/persist keypair, prefs (name, radio params), contacts, channels.
4. **ChatMesh**: subclass `BaseChatMesh` (start from `simple_secure_chat`), Public channel, DM send with ACK/retry,
   advert send on boot after onboarding.
5. **UI shell**: palette, font, status bar, hint bar, tab switching, list widget, text input line.
6. **Onboarding**: region → name → confirm (PRD 5.1). No TX before completion.
7. **Screens**: MSG (inbox → conversation), CON (list → details → message), RAD (radio info + send advert), SET (basic).
8. **Basic power**: backlight dim/off timeout, CPU 80 MHz when screen off, key-to-wake.

**Exit criteria**
- Exchange Public and DM messages (with ✓✓ ACK) with an official MeshCore phone app on the same preset.
- Survives reboot with identity, contacts, settings and recent messages intact.
- Free heap ≥ 60 KB after boot with 50 contacts loaded.

## Phase 1 — "full client"

1. Private channels: `#hashtag` and name + PSK join, delete; unread counts per channel.
2. Contact management: favourite, delete, reset path, share (zero-hop), filter-by-typing, auto-add toggle.
3. **Repeater discovery** screen (`CTL_TYPE_NODE_DISCOVER_REQ/RESP`, 60 s window, sort by SNR).
4. Full settings: manual radio params, path hash size, location sharing, TZ offset, manual clock.
5. Notifications: toast + beep, mute toggle; per-conversation message history persisted.
6. Radio info extras: noise floor, RX/TX counters, airtime.

**Exit criteria**: join a hashtag channel the phone app also joined and chat; discovery shows the same repeaters
as the phone app; all settings round-trip through a reboot.

## Phase 2 — "all-day battery & power user"

1. Automatic light sleep when screen is off; wake on LoRa DIO1 and keyboard interrupt (verify TCA8418 INT pin).
2. Power-off (deep sleep) + wake on G0; low-battery warning / cutoff.
3. RX boosted gain toggle (power profile setting); measure and document battery life.
4. Repeater login (guest/admin) + status view; ping/trace path to a contact.
5. GPS (Cap ATGM336H): fix display on RAD screen, optional location in adverts, sets clock from GPS.

**Exit criteria**: ≥ 24 h idle listening measured; no missed messages in light sleep across a 1 h test.

## Phase 3 — "nice to have" (only if asked for)

- Room server login + post/read.
- QR code to share own contact (`meshcore://` format from MeshCore `docs/qr_codes.md`).
- Optional BLE companion mode (reuse MeshCore `companion_radio` frames) for users who also want a phone.
- SD card message export; theme accent choices (Ripple-style small palette).

## Risks & open questions

| Risk | Mitigation |
|---|---|
| RAM without PSRAM | Fixed-size arrays, small limits (100 contacts, 8 channels, message ring), measure heap each phase |
| Keyboard interrupt pin / light-sleep wake on Cardputer-Adv unknown | Spike early in Phase 2; fall back to polling at low CPU freq |
| MeshCore API churn | Pin commit; bump deliberately, re-run interop test |
| Regional legality | Onboarding blocks TX until a preset is chosen; 433 MHz presets hidden (hardware is 868–923 only) |
| Hashtag key derivation must match the apps | Verify against official app with a known channel before shipping P1 |

## Testing approach

- Manual on-device test checklist per phase (kept in `docs/test_checklist.md` once the code exists).
- Interop reference: official MeshCore phone app + one companion radio on the same preset.
- Serial log (`MESH_DEBUG`) for protocol issues; heap/airtime printed on RAD screen.
