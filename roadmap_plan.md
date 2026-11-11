# Roadmap & Plan

Ship the smallest thing that works on real hardware, then grow. Each phase ends with a flashable build tested on
the device. Requirements are in `PRD.md`; code layout and rules in `CLAUDE.md`.

Legend: ✅ done and checked on the device · 🟡 built, not yet verified · ⬜ to do

## MVP — "talk on Public, DM a friend" ✅

✅ Radio up (SX1262 + Cap RF switch), identity, persistence · ✅ onboarding (region → name → time zone → confirm,
no TX before) · ✅ Public send/receive · ✅ inbox, chat, contacts, radio, settings screens · ✅ screen dim/off,
80 MHz when off

**Exit criteria**
- ✅ Public messages both ways with the official app's network
- ✅ DM and contact received from another user · 🟡 our DM's `[ok]` ACK — not yet seen; log shows no ACK
  returning at our weak SNR (~−9 dB). Late ACKs (any attempt, even after `[x]`) are accepted.
- ✅ Survives reboot with identity, contacts, settings, messages
- 🟡 Free heap with 50 contacts (228 KB free at boot with few contacts)

## Phase 1 — "full client" ✅ (on device, interop checks pending)

- ✅ Channels: `#hashtag` / private (name + base64 or hex key) join; leave; delete DM threads; persisted
- ✅ Contacts: favourite, delete, reset path, share, type-to-filter; auto-add by type (chat+repeaters default)
  with overwrite-oldest; un-added adverts listed with `+`
- 🟡 Repeater discovery (`D` on RPT): 60 s, sorted by SNR, add unknown repeaters
- ✅ Settings: frequency / BW / SF / CR, path hash size, auto-add, overwrite, time zone, manual clock
- ✅ Fair message history (busiest conversation evicted first), beep / mute, TX airtime %
- ✅ Clock from GPS (verified), else two agreeing mesh senders, else manual
- ✅ Status bar spacing; compose line wraps (3 rows); `Fn+R` @mention reply in channels
- ✅ Serial log (115200): packet types in/out, DM attempts, ACKs, 10 s radio health line

**Exit criteria**
- 🟡 Hashtag channel shared with the phone app, messages both ways (a message was received on a joined channel)
- 🟡 Discovery shows the same repeaters as the phone app
- ✅ Settings survive a reboot

## Phone app over Bluetooth ✅ (v0.3.0)

- ✅ Mesh engine replaced by MeshCore's official companion engine (`MyMesh` + `DataStore`, unmodified); our UI is
  its listener (`ChatClient`). Phone app pairs over BLE with a per-boot PIN; Bluetooth on/off in SET
- ✅ One-time migration of name, radio settings, auto-add, contacts and channels from the v0.2 files
- ✅ On device: app connects, sees the same contacts/channels, sends and receives; keyboard sends while connected
- Heap with Bluetooth on: ~104 KB free (was ~212 KB)

## Phase 2 — "all-day battery & power user" ⬜

1. Light sleep while the screen is off; wake on LoRa DIO1 and keyboard interrupt (find the TCA8418 INT pin).
2. Power off (deep sleep) + wake on G0; low-battery warning and cut-off.
3. RX boosted-gain toggle; measure and document battery life.
4. Repeater login (guest/admin) + status; ping / trace path to a contact.
5. GPS: fix / satellites on RAD, optional location in adverts (sharing setting), power the GPS down when not
   needed (no enable pin found yet).

**Exit criteria**: ≥ 24 h idle listening measured; no missed messages in light sleep over a 1 h test.

## Phase 3 — "nice to have" (only if asked for) ⬜

- Room server login + post/read.
- QR code to share own contact (`meshcore://`, MeshCore `docs/qr_codes.md`).
- SD card message export; accent colour choices.

## Risks & open questions

| Risk | Mitigation |
|---|---|
| RAM without PSRAM | Fixed-size arrays and small limits; check heap after each phase |
| Light-sleep wake via keyboard on the Cardputer-Adv unknown | Spike early in Phase 2; fall back to polling at 80 MHz |
| MeshCore API churn | Commit pinned; bump deliberately and re-test interop |
| Hashtag keys must match the apps | Names are lowercased before hashing — confirm with the app on a known channel |
| GPS always powered | Look for a standby command / power control in Phase 2 |
| Device sometimes stays in download mode after flashing | Power-cycle without holding G0 |

## Testing

- Manual on-device checks per phase, against the official MeshCore app on the same preset.
- Serial log at 115200 (`pio device monitor`): boot line prints free heap.
