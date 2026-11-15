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

Checks: ✅ DMs both ways with `[ok]` (T-Deck Plus) · ✅ app on the same device sends/receives on channels ·
✅ settings and data survive reboots · ✅ ~104 KB heap free with Bluetooth on · ✅ TX power matches a T-Deck
(SDR, hillside health check 84–87 %) · ✅ release image after full erase, with and without the Cap ·
🟡 discovery vs. the phone app's repeater list.

## Next, by priority

Ranked by importance against effort. Every release must pass the upgrade rules in `CLAUDE.md`
(**Upgrades and data**) and the upgrade test below.

| # | Item | Value | Effort |
|---|---|---|---|
| 1 | **Upgrade safety:** atomic saves (temp file + rename) for `/prefs` and `/msgs`; upgrade test from v0.3.2 with real data | Must-have now that v0.3.2 is out | ½ day |
| 2 | **Colour schemes:** 7 four-colour themes (amber default, green, VFD cyan, red, blue LCD, classic LCD, paper) | High, visible | ½–1 day |
| 3 | **GPS clock setting:** `GPS < off / on >`, RMC only, silent after fix, re-sync every 6 h | Medium: clock without mesh or app | ½–1 day |
| 4 | **Test tab, part 1:** hidden `6 TST` tab, live packet log, stats, RSSI graph (no TX) | High for field use and support | 1½ days |
| 5 | **Low battery and power off:** warning, cut-off, deep sleep on long G0 press, wake on G0 | High | 1 day |
| 6 | **Test tab, part 2:** hear-me (zero-hop), echo test on `#test`, trace path, SDR burst | Medium-high | 1½ days |
| 7 | **Light sleep** with the screen off, wake on LoRa DIO1 / keyboard (G11); measure ≥ 24 h idle, no missed messages | High, but riskiest (BLE, radio IRQ) | 2–3 days |
| 8 | **GPS location:** fix on RAD, optional location in adverts | Low-medium | ½ day after #3 |
| 9 | **Known limitations:** app-sent messages missing from device history (and back); BLE name updates only after reboot | Low-medium | 1 day, needs engine hooks |

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
   phone app still paired with the same PIN behaviour. Then a fresh-erase install still onboards.

## Risks

| Risk | Mitigation |
|---|---|
| An upgrade loses user data | Upgrade rules in `CLAUDE.md`; upgrade test above; keep migration code for every released format |
| RAM (no PSRAM; Bluetooth takes ~100 KB) | Fixed-size arrays; check free heap after each change |
| Weak link indoors: repeaters often can't hear us | Not the firmware (SDR: TX matches a T-Deck); test by a window or outdoors |
| MeshCore API or file-format churn | Commit pinned; bump deliberately, re-test with the app and the upgrade test |
| Unpinned libraries change behaviour in CI | Pin every library version (M5Unified 0.3.0 rotated the screen in v0.3.1) |
| Board stays in download mode after flashing | Power-cycle without holding G0 |
