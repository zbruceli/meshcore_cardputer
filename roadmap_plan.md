# Roadmap

Ship the smallest thing that works on the hardware, then grow; each step ends in a flashable, device-tested build.
Scope is in `PRD.md`, code and rules in `CLAUDE.md`.

✅ done and checked on the device · 🟡 built, not fully verified · ⬜ to do · ⏸ shelved

## Released

- **v0.2.0 — MVP + Phase 1 ✅:** setup without TX before a region, Public / hashtag / private channels, DMs with
  ACK marks and retries, contact management, repeater discovery, full settings, clock (mesh votes / manual),
  `@[name]` replies, wrapping compose line.
- **v0.3.0 — phone app over Bluetooth ✅:** MeshCore's official companion engine (`MyMesh` + `DataStore`,
  unmodified) runs the mesh; the device UI is its listener; one-time migration of v0.2 data. App and keyboard work
  at once on shared contacts, channels and settings. Lists sort by latest activity. Channel echoes `[heard N]`.
- **v0.3.1 / v0.3.2 ✅:** boots without the Cap (`RADIO FAIL`), startup screen during first-boot formatting,
  pinned M5Unified/M5GFX. Web installer on GitHub Pages; listed on [M5Burner](https://burner.m5stack.com/U3W4DS).
- **v0.4.0 ✅:** items 1–5, 9 and 10 below: safe saves, colour schemes, GPS clock and location, GPS standby, test tab,
  quick replies. Released after indoor device checks; upgrade tested with esptool and M5Burner; the hillside
  field test is still to do.
- **v0.4.1 ✅:** the phone app's GPS switch (custom var `gps`) maps to SET > GPS. First build sent for M5Burner
  review (v0.4.0 was never listed there).

Checks: ✅ DMs both ways with `[ok]` (T-Deck Plus) · ✅ app on the same device sends/receives on channels ·
✅ settings and data survive reboots · ✅ ~104 KB heap free with Bluetooth on · ✅ TX power matches a T-Deck
(SDR, hillside health check 84–87 %) · ✅ release image after full erase, with and without the Cap ·
🟡 discovery vs. the phone app's repeater list.

## Next, by priority

Ranked by importance against effort. Every release must pass the upgrade rules in `CLAUDE.md`
(**Upgrades and data**) and the upgrade test below.

| # | Item | Value | Effort |
|---|---|---|---|
| 1 | ✅ **Upgrade safety:** atomic saves (temp file + rename, recovered at boot) for `/prefs` and `/msgs`; upgrade test over v0.3.2 with real data passed (esptool at 0x0). Unreleased; M5Burner path checked at the next release | Must-have now that v0.3.2 is out | ½ day |
| 2 | ✅ **Colour schemes:** 7 four-colour themes (amber default, green, VFD cyan, red, blue LCD, classic LCD, paper) | High, visible | ½–1 day |
| 3 | ✅ **GPS clock setting:** `GPS clock < off / on >` (now the "clock" mode of #10), RMC only, silent after fix, re-sync every 6 h | Medium: clock without mesh or app | ½–1 day |
| 4 | ✅ **Test tab, part 1:** hidden `6 TST` tab, live packet log, stats, RSSI graph (no TX) | High for field use and support | 1½ days |
| 5 | ✅ **GPS standby:** the GPS drew ~30 mA even with GPS clock off; now in CASIC standby (`$PCAS12`) when off and for 6 h between re-syncs. Screen-off idle 150 → 120 mA (meter) | High: ~+25 % battery for everyone | ½ day |
| 6 | **Low battery and power off:** warning, cut-off, deep sleep on long G0 press, wake on G0 | High | 1 day |
| 7 | **Light sleep** with the screen off, wake on LoRa DIO1 / keyboard (G11); measured 150 → ~40 mA possible; Bluetooth-off case first; measure ≥ 24 h idle, no missed messages | High, but riskiest (BLE, radio IRQ) | 2–3 days |
| 8 | **Test tab, part 2:** hear-me (zero-hop), echo test on `#test`, trace path, SDR burst | Medium-high | 1½ days |
| 9 | ✅ **Quick replies:** 8 saved messages; `Enter` on an empty compose line lists them, `1`–`8` puts one in the compose line, `Enter` sends; edited in SET > Quick replies; stored in `/quick`. Cheap stand-in for voice input, which doesn't fit on this board | Medium: fast one-handed replies | 1 day |
| 10 | ✅ **GPS location:** `GPS < off / clock / location >`; short sessions (fix in ~5–35 s, HDOP ≤ 2), every 10 min in location mode; "0 My location" in quick replies sends a `geo:` link; SET My location + Loc in adverts; distance/direction on contacts | Low-medium | 1 day |
| 11 | **Known limitations:** app-sent messages missing from device history (and back); BLE name updates only after reboot | Low-medium | 1 day, needs engine hooks |

Power audit (2026-11-14, USB meter at 5 V, battery switched out): screen on 200 mA, screen off 150 mA, GPS
standby −30 mA, light sleep floor 60 mA (GPS awake); Bluetooth, speaker codec and RX gain each < 10 mA. To confirm
in hours: a battery run-down test.

**Later (only if asked for):** repeater login and status, room servers, QR contact sharing, SD message export.

**Shelved ⏸:**
- Meshnology W12 port: 128×64 OLED and one button; use MeshCore's official W12 firmware with the phone app.
- LilyGo T-Deck Plus port: feasible, but our keyboard UI can't use its touch screen, and WadaMesh already serves it
  well.

## Upgrade test (every release)

1. Flash the previous release (start with v0.3.2) after a full erase; onboard, add contacts and a private channel,
   exchange messages, change settings (brightness, time zone, Bluetooth, auto-add), pair the phone app.
2. Flash the new release **without erasing**, using the web installer and M5Burner.
3. Check: same identity (My ID), name, region and radio settings, contacts, channels, message history, UI settings,
   phone app connects (after re-pairing, see below) with the same PIN behaviour. Then a fresh-erase install still
   onboards.

v0.3.2 → v0.4.0 (2026-11-24, esptool at 0x0, release images): all data files byte-identical, settings kept. The
phone pairing is lost: the merged image fills 0x9000–0xDFFF (NVS, where the Bluetooth bonds live) with 0xFF, so every
upgrade since v0.3.0 needs re-pairing. Fixed for the web installer (2026-11-24): it writes the four parts at their own
offsets (`scripts/split_image.py`) and skips NVS; tested with esptool writing the same parts (bond and local keys
unchanged) and with the live installer page in Chrome (v0.4.1, phone reconnected without re-pairing). M5Burner takes one image at 0x0, so it keeps the re-pair note. M5Burner (erase + v0.3.2, then v0.4.0 without
erase): same result, data kept, phone re-paired.

## Risks

| Risk | Mitigation |
|---|---|
| An upgrade loses user data | Upgrade rules in `CLAUDE.md`; upgrade test above; keep migration code for every released format |
| An upgrade drops the phone pairing (merged image wipes NVS) | Web installer writes split parts (keeps it); README tells M5Burner/esptool users to re-pair |
| RAM (no PSRAM; Bluetooth takes ~100 KB) | Fixed-size arrays; check free heap after each change |
| Weak link indoors: repeaters often can't hear us | Not the firmware (SDR: TX matches a T-Deck); test by a window or outdoors |
| MeshCore API or file-format churn | Commit pinned; bump deliberately, re-test with the app and the upgrade test |
| Unpinned libraries change behaviour in CI | Pin every library version (M5Unified 0.3.0 rotated the screen in v0.3.1) |
| Board stays in download mode after flashing | Power-cycle without holding G0 |
