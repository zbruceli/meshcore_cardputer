# PRD — MeshCore Cardputer (standalone)

What we are building and why. Status and sequencing live in `roadmap_plan.md`; hardware facts and code layout in
`CLAUDE.md`.

## 1. Summary

Standalone MeshCore chat client firmware for the **M5Stack Cardputer-Adv + Cap LoRa-1262**. The built-in
240×135 screen and keyboard work without a phone (a pocket pager for the mesh); the official MeshCore phone app can
also pair with it over Bluetooth.

## 2. Goals / non-goals

**Goals**
- Usable without a phone: first boot → region → name → chatting on Public in under a minute.
- Interoperate with the official MeshCore network (apps, companion radios, Ripple devices, repeaters) by reusing
  the MeshCore library, not re-implementing the protocol.
- Legible outdoors: amber on black, large type, high contrast.
- Long battery life: screen off when idle, radio always listening; light sleep later.
- Fast: boot to UI in < 3 s, keypress to redraw < 50 ms.

**Non-goals (for now)**
- Maps, images, OTA, WiFi. Acting as a repeater or room server. Other Cardputers or LoRa modules.

## 3. Users & scenarios

1. **Hiker / event attendee** — powers on, picks "USA", types a name, says hi on Public, gets replies.
2. **Neighbourhood member** — joins `#bayarea` (or a private channel by key) and chats there.
3. **Mesh builder** — discovers which repeaters hear them and at what SNR.
4. **Two friends** — exchange adverts and DM each other with delivery confirmation.

## 4. Features

Priority: **M** = MVP, **P1** = Phase 1, **P2** = Phase 2.

### 4.1 Onboarding (M)
- First boot or after factory reset: **Region → Name → Time zone → Confirm**.
- Region list = official presets (`api.meshcore.nz`, compiled in) filtered to the hardware's 868–923 MHz.
  Nothing is preselected: the user must choose (legal compliance). **No transmission before onboarding completes.**
- Name prefilled with MeshCore's default (pubkey hex). Confirm sends a flood advert and opens Public.
- Keypair generated on first boot and persisted.

### 4.2 Messaging (M; P1 channels)
- Unified inbox: channels first, then DM threads by recency, with unread counts.
- Conversation: `HH:MM sender` header + wrapped text, newest at the bottom, compose line pinned below.
- DMs: `[..]` sending, `[ok]` acknowledged, `[x]` failed after 3 attempts (last attempt floods).
- New message: beep (unless muted) and a toast when that conversation isn't open.
- **P1**: join `#hashtag` (key = first 16 bytes of `sha256("#name")`, name lowercased) or a private channel by
  name + 16/32-byte key (base64 or hex); leave channels; delete DM threads. Max 8 channels; Public is fixed.
- History: 96 messages persisted across reboots; when full, the busiest conversation loses its oldest message.

### 4.3 Contacts (M; P1 management)
- Added from received adverts. List: favourites first, then last heard; type tag, hops (`D` direct / N / `?`), age.
- Details: name, type, ID, path, last heard, GPS if advertised.
- **P1**: favourite, delete, reset path, share (re-broadcast their advert zero-hop), type-to-filter.
- **P1** auto-add by node type — default chat + repeaters (MeshCore's "Auto Add Selected" advice, so repeater
  names show in paths); also all / chat / off. "Overwrite oldest" when full (default on, favourites kept).
  Adverts that weren't added are listed with `+` and can be added by hand. Max 100 contacts.

### 4.4 Repeater discovery (P1)
- Zero-hop `CTL_TYPE_NODE_DISCOVER_REQ` (repeaters), replies collected for 60 s, sorted by SNR, showing SNR
  heard by us / by them. Known repeaters listed below. A reply from an unknown repeater can be added as a contact.
- **P2**: repeater login (guest/admin) + status (uptime, battery, noise floor, counters).

### 4.5 Radio info (M)
- Frequency (large, VFO style), SF/BW/CR/TX power, last RSSI/SNR, noise floor, RX/TX packets, TX airtime %,
  battery voltage. Send advert flood (`Enter`, or G0 anywhere) or zero-hop (`Z`).

### 4.6 Settings (M basic, P1 full)
- **M**: name, region, TX power, brightness, screen-off timeout, beep, factory reset.
- **P1**: frequency, bandwidth, SF, CR (any change → region "Custom"), path hash size, auto-add types,
  overwrite oldest, time zone, clock (manual set; shows its source).
- **P2**: power profile, GPS location sharing in adverts.

### 4.7 Clock (P1)
- No RTC chip. Source priority: **GPS** (Cap ATGM336H, valid fix, re-applied hourly) → **mesh** (two different
  senders agreeing within 5 min) → **manual**. Shows `--:--` until set. Time zone is a fixed UTC offset (no DST).
- GPS position is not used yet (P2).

### 4.8 UI
- Palette: black background, amber `#FF9500`, dim amber `#7A4800`, inverse selection, red `#FF3030` only for
  errors / low battery. 8×16 monospace font (30 columns), large digits for the frequency. No animations.
- Layout: 16 px status bar (title · unread · signal · battery · clock) · 6 body rows · 16 px key-hint bar.
- Tabs `1 MSG` · `2 CON` · `3 RPT` · `4 RAD` · `5 SET`. Pager feel: typing in a chat goes straight to the compose line.

### 4.9 Power (M basic, P2 full)
- **M**: dim at half the screen-off timeout, screen off at the timeout (default 30 s), CPU 80 MHz while off,
  any key wakes (that key is not acted on).
- **P2**: light sleep while the screen is off (wake on LoRa DIO1 / keyboard), power off (deep sleep, wake on G0),
  low-battery warning and cut-off, RX boosted-gain toggle, GPS power-down. Target ≥ 24 h idle listening.

## 5. Protocol & interoperability

- MeshCore library (`meshcore-dev/MeshCore`, pinned commit). The mesh engine is MeshCore's official companion
  firmware engine (`examples/companion_radio`: `MyMesh`, `DataStore`), compiled unmodified, which also speaks the
  companion protocol to the phone app over BLE (`SerialBLEInterface`). The device UI is its listener (`ChatClient`).
- Packet types: ADVERT, TXT_MSG, ACK, GRP_TXT, PATH, CONTROL (discovery). Later: REQ/RESPONSE/ANON_REQ.

## 6. Constraints

- No PSRAM: fixed-size arrays only; ≥ 60 KB free heap with contacts loaded.
- Persistence on SPIFFS: the engine's files (identity, `/prefs.json`, `/contacts3`, `/channels2`, advert blobs) plus
  the UI's prefs and message log.

## 7. Success metrics

- Public, hashtag and DM (with ACK) round-trips with the official MeshCore app on the same preset.
- Discovery lists the same repeaters a companion app sees.
- Boot ≤ 3 s; keypress-to-redraw ≤ 50 ms; idle listening ≥ 24 h (P2).
- A new user completes onboarding and a first message with no instructions.

## 8. References

- MeshCore: https://github.com/meshcore-dev/MeshCore (`examples/companion_radio`, `docs/`)
- Radio presets: https://api.meshcore.nz/api/v1/config
- Ripple UI (tabs, discover list, channel join): https://blog.meshcore.io/2026/04/30/ripple-9.7
- Prior Cardputer port (pins, init; needs a phone for setup): https://github.com/Stachugit/MeshCore-Cardputer-ADV
- Onboarding flow inspiration: https://bayme.sh/docs/getting-started/
- Cap LoRa-1262: https://docs.m5stack.com/en/cap/Cap_LoRa-1262

## 9. Phone app over Bluetooth (added after Phase 1)

- The official MeshCore app pairs with the Cardputer as with any companion radio (name `MeshCore-<name>`, 6-digit
  PIN shown in Settings, random each boot unless set from the app). Bluetooth can be turned off in Settings.
- Keyboard and app work at the same time on one shared set of contacts, channels and radio settings (`NodePrefs`).
- Limits: messages sent from one side don't appear in the other's history; incoming messages appear on both.
