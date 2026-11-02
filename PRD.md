# PRD — MeshCore Cardputer (standalone)

## 1. Summary

A standalone MeshCore chat client firmware for the **M5Stack Cardputer-Adv + Cap LoRa-1262**.
The built-in 240×135 screen and QWERTY keyboard replace the phone app entirely: no BLE pairing,
no companion app. Pocket pager for the mesh.

## 2. Goals / Non-goals

**Goals**
- Fully usable without a phone: first boot → region → name → chatting on Public in under a minute.
- Interoperate with the current official MeshCore network (companion apps, Ripple devices, repeaters, room servers)
  by reusing MeshCore's own library code, not re-implementing the protocol.
- Legible outdoors: amber/orange on black, large type, high contrast.
- Long battery life: screen off + light sleep whenever idle, radio always listening.
- Fast: boot to usable UI in < 3 s, every keypress redraws in < 50 ms.

**Non-goals (for now)**
- BLE/USB companion protocol (phone app support). Possible late phase, off by default.
- Maps, images, OTA, WiFi.
- Acting as a repeater or room server.
- Supporting the original (non-Adv) Cardputer or other LoRa modules.

## 3. Target hardware

| Part | Detail |
|---|---|
| MCU | ESP32-S3 (StampS3A), 8 MB flash, **no PSRAM — treat RAM as ~300 KB** |
| Display | 1.14" ST7789V2, 240×135, backlit (PWM) |
| Keyboard | 56-key, Cardputer-Adv uses I2C keyboard controller (TCA8418) — interrupt capable (verify pin) |
| Battery | 1750 mAh Li-ion, ADC on G10 (2:1 divider) |
| Extras | Speaker (beep), G0 button, SD slot (unused until later phase) |
| LoRa | Cap LoRa-1262: SX1262, **868–923 MHz only**, +22 dBm, TCXO, DIO2 RF switch |
| LoRa pins | NSS G5, IRQ/DIO1 G4, RST G3, BUSY G6, SCK G40, MOSI G14, MISO G39 |
| Cap I/O expander | PI4IOE5V6408 @ 0x43 on I2C G8/G9 — P0 must be driven high to enable the RF path |
| GPS (on Cap) | ATGM336H UART 115200 on G13/G15 — optional, later phase |

Band limitation: 433 MHz presets must be hidden in onboarding.

## 4. Users & primary scenarios

1. **Hiker / event attendee** — powers on, picks "USA" preset, types a name, sends "hi" on Public, gets replies.
2. **Neighbourhood mesh member** — joins a private/hashtag channel (`#bayarea`) by typing its name or key.
3. **Mesh builder** — runs "Discover repeaters" to see which repeaters hear them and at what SNR.
4. **Pair of friends** — exchange adverts, DM each other with delivery ticks.

## 5. Features & requirements

Priority: **M** = MVP, **P1** = Phase 1, **P2** = Phase 2. Detailed sequencing lives in `roadmap_plan.md`.

### 5.1 Onboarding (M)
- Shown on first boot, or after factory reset.
- Step 1 — **Region**: list of official presets (from `api.meshcore.nz/api/v1/config`, compiled in) filtered to
  868–923 MHz, plus "Custom". Default highlight by nothing — user must choose (legal compliance).
- Step 2 — **Name**: text field, prefilled with MeshCore default (hex of pubkey prefix), max 31 chars.
- Step 3 — **Confirm**: shows freq/BW/SF/CR/TX power; Enter saves, sends a flood advert, lands on Public channel.
- Keypair generated silently on first boot (MeshCore `LocalIdentity`), persisted.
- Borrowed from bayme.sh getting-started flow: pick region → set name → send first message ("say hi on Public").

### 5.2 Messaging (M: Public + DMs, P1: private channels)
- **Channels**: Public channel preconfigured (MeshCore `PUBLIC_GROUP_PSK`). Channel view = scrolling log
  `HH:MM NAME` header + wrapped text, newest at bottom, compose line pinned at the bottom.
- **DMs** to contacts with status marks: `…` sending, `✓` heard by repeater / sent, `✓✓` ACK'd, `✗` failed after retries
  (MeshCore `sendMessage` + `expected_ack`, retry on timeout like companion_radio).
- Max text length = MeshCore `MAX_TEXT_LEN` minus "name: " prefix for channels; live char counter.
- Unread counters per conversation; new-message toast + short beep (respect mute).
- **P1**: add private channel by (a) `#hashtag` name (key derived the same way the official apps do) or
  (b) name + base64 PSK. Delete channel. Max channels: 8.
- Message history: last ~30 per conversation in RAM, persisted to flash so it survives reboot (M: last 100 total; P1 per-conversation).

### 5.3 Contacts (M: list/DM, P1: management)
- Auto-add from received adverts (MeshCore `BaseChatMesh` behaviour). Type icon: chat / repeater / room / sensor.
- List sorted by last heard; shows name, type, hops ("direct" or N hops), last-heard age.
- Actions: Message, Details (pubkey prefix, path, last SNR, GPS if any), **P1**: Favourite, Delete, Reset path,
  Share (zero-hop advert of contact).
- **P1** auto-add by node type, default chat + repeaters ("Auto Add Selected" per MeshCore guidance, so repeater
  names show up), plus "overwrite oldest" when full (favourites are kept). Adverts not added stay listed to add by hand.
- Search: typing letters in the list filters by name prefix.
- Max contacts: 100 (RAM bound; ~150 B each).

### 5.4 Repeater discovery (P1)
- "Discover" action sends MeshCore control packet `CTL_TYPE_NODE_DISCOVER_REQ` (zero-hop, filter = repeaters).
- Collect `CTL_TYPE_NODE_DISCOVER_RESP` for 60 s: show name (if known), pubkey prefix, SNR in, SNR out, sorted by SNR.
- Selecting a result lets you add it as contact / view details.
- **P2**: repeater login (guest/admin password) + status request (uptime, battery, noise floor, packet counts).

### 5.5 Radio info (M)
- Live panel: frequency, BW, SF, CR, TX power, last RSSI/SNR, noise floor, packets RX/TX, airtime %,
  battery % + voltage, uptime, node name, pubkey prefix.
- "Send advert" (zero-hop / flood) from here and via the G0 button.

### 5.6 Configuration (M basic, P1 full)
- **M**: name, region preset, TX power, screen brightness, screen timeout, beep on/off, factory reset.
- **P1**: manual radio (freq/BW/SF/CR), auto-add contact types, overwrite oldest, path hash size (default 1 byte
  per FAQ guidance), time zone offset, set clock manually. (Advert location sharing moved to P2 with GPS.)
- **P2**: power profile (see 5.8), GPS on/off + interval.
- All settings persisted with MeshCore `NodePrefs`-compatible fields where they overlap.

### 5.7 UI
- Palette: background `#000000`, primary amber `#FF9500`, dim amber `#7A4800` for secondary text/borders,
  inverse (black on amber) for selection & title bar, red `#FF3030` only for errors/low battery.
- Font: one proportional ~12–16 px font for body (≥ 2.0 mm cap height at the panel's ~210 ppi), larger digits for
  frequency readouts (ham-radio "VFO" style). No text smaller than 8 px tall.
- Layout (240×135):
  - **Status bar** 14 px: screen title · unread · RSSI bars · battery · clock (pager style).
  - **Body** ~107 px: 6 lines of list/messages.
  - **Key-hint bar** 14 px: soft-key labels like a pager/ham rig, e.g. `ENT open  TAB tab  ` esc back`.
- Navigation (Ripple-style tabs, ham-rig function keys):
  - Tabs: `1 MSG` (channels + DMs, unified inbox) · `2 CON` (contacts) · `3 RPT` (discover) · `4 RAD` (radio info) · `5 SET`.
  - `Fn`+`1..5` or `Tab` cycles tabs; arrows `;` `.` `,` `/` (with Fn) move; `Enter` select/send; `` ` `` (Esc) back;
    `Del` backspace; G0 = send advert.
  - Typing in a conversation goes straight into the compose line (pager behaviour, no "edit mode" toggle).
- Every screen readable at arm's length in sunlight; no animations.

### 5.8 Power (M basics, P2 full)
- **M**: backlight dims after 15 s, off after 60 s (configurable); any key wakes; CPU at 80 MHz when screen off.
- **P2**: ESP32-S3 automatic light sleep while screen off, woken by LoRa DIO1 or keyboard interrupt;
  RX boosted gain toggle (power saver vs. sensitivity); "Power off" = deep sleep with radio in sleep,
  wake on G0; low-battery warning at 3.5 V, auto power-off at 3.3 V.
- Target: ≥ 24 h idle-listening on one charge (to be measured in P2).

## 6. Protocol & interoperability

- Use the official **MeshCore library** (`meshcore-dev/MeshCore`, pinned commit) as a PlatformIO dependency:
  `mesh::Mesh`, `BaseChatMesh`, `Identity`, `RadioLib` wrappers, `CustomSX1262Wrapper`, `StaticPoolPacketManager`,
  `SimpleMeshTables`.
- Our `ChatMesh` subclasses `BaseChatMesh`, modelled on `examples/simple_secure_chat` (smallest) and
  `examples/companion_radio/MyMesh` (reference for ACK/retry, discovery, channel handling).
- Do **not** implement the companion serial/BLE frame protocol.
- Packet types used: ADVERT, TXT_MSG, ACK, GRP_TXT, PATH, CONTROL (discover), later REQ/RESPONSE/ANON_REQ (repeater login/status).
- Default radio params from the chosen preset; default path hash mode = 1 byte unless preset says otherwise.

## 7. Constraints

- RAM budget: MeshCore tables + 100 contacts + 8 channels + message ring + 240×135×2 B frame buffer (~65 KB) — must fit with ≥ 60 KB free heap.
- Flash FS: LittleFS (or SPIFFS as MeshCore's ESP32 helpers use) for identity, prefs, contacts, channels, messages.
- Legal: never transmit before a region preset is chosen.

## 8. Success metrics

- Message round-trip with the official MeshCore phone app on the same preset (Public + DM with ACK).
- Repeater discovery lists the same repeaters a companion app sees.
- Boot ≤ 3 s; keypress-to-redraw ≤ 50 ms; idle-listening battery ≥ 24 h.
- New user completes onboarding + first message with no instructions.

## 9. References

- MeshCore firmware: https://github.com/meshcore-dev/MeshCore (`examples/companion_radio`, `examples/simple_secure_chat`, `docs/`)
- Radio presets API: https://api.meshcore.nz/api/v1/config (see `docs/radio_presets.md`)
- Ripple firmware UI (tabs, Discover list, channel join, theme/accent): https://blog.meshcore.io/2026/04/30/ripple-9.7
- Prior Cardputer port (pins, I/O expander init, keyboard handling; needs phone for setup): https://github.com/Stachugit/MeshCore-Cardputer-ADV
- Onboarding flow inspiration (Meshtastic-oriented, flow only): https://bayme.sh/docs/getting-started/
- Cap LoRa-1262 hardware: https://docs.m5stack.com/en/cap/Cap_LoRa-1262
