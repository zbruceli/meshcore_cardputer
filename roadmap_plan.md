# Roadmap

Ship the smallest thing that works on the hardware, then grow; each step ends in a flashable, device-tested build.
Scope is in `PRD.md`, code and rules in `CLAUDE.md`.

✅ done and checked on the device · 🟡 built, not fully verified · ⬜ to do

## Released

- **v0.2.0 — MVP + Phase 1 ✅:** setup without TX before a region, Public / hashtag / private channels, DMs with
  ACK marks and retries, contact management, repeater discovery, full settings, clock (mesh votes / manual),
  `@[name]` replies, wrapping compose line.
- **v0.3.0 — phone app over Bluetooth ✅:** MeshCore's official companion engine (`MyMesh` + `DataStore`,
  unmodified) runs the mesh; the device UI is its listener; one-time migration of v0.2 data. App and keyboard work
  at once on shared contacts, channels and settings. Lists sort by latest activity.

Checks: ✅ DMs both ways with `[ok]` (T-Deck Plus) · ✅ app on the same device sends/receives on channels ·
✅ settings and data survive reboots · ✅ ~104 KB heap free with Bluetooth on · 🟡 discovery vs. the phone app's list.

## Phase 2 — all-day battery ⬜

1. Light sleep while the screen is off; wake on LoRa DIO1 and keyboard interrupt (TCA8418 INT = G11).
2. Power off (deep sleep), wake on G0; low-battery warning and cut-off.
3. Measure battery life (target ≥ 24 h idle listening; no missed messages in light sleep over 1 h).
4. GPS: fix on RAD, optional location in adverts, power it down when unused (no enable pin; `$PCAS` commands).

## Later (only if asked for)

Repeater login and status, room servers, QR contact sharing, SD message export, colour themes, M5Burner listing.

## Risks

| Risk | Mitigation |
|---|---|
| RAM (no PSRAM; Bluetooth takes ~100 KB) | Fixed-size arrays; check free heap after each change |
| Weak link indoors: repeaters often can't hear us | Not the firmware (SDR: TX matches a T-Deck); test by a window or outdoors |
| MeshCore API churn | Commit pinned; bump deliberately and re-test with the app |
| Board stays in download mode after flashing | Power-cycle without holding G0 |
