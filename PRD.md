# PRD — MeshCore Cardputer

What we build and why. Status lives in `roadmap_plan.md`; code, hardware facts and rules in `CLAUDE.md`; user
instructions in `README.md`.

## Summary

MeshCore chat firmware for the **M5Stack Cardputer-Adv + Cap LoRa-1262**. Its 240×135 screen and keyboard work on
their own, like a pocket pager for the mesh; the official MeshCore phone app can pair over Bluetooth at the same time.

## Goals / non-goals

- **Goals:** usable without a phone (first boot to a Public message in under a minute); full interop with the
  MeshCore network by reusing MeshCore's own code; legible outdoors (amber on black); long battery life; boot < 3 s,
  keypress to redraw < 50 ms.
- **Non-goals:** maps, images, OTA, WiFi; acting as a repeater or room server; other boards or LoRa modules.

## Users

Event-goer saying hi on Public · neighbour on `#bayarea` or a private channel · mesh builder checking which
repeaters hear them · two friends DMing with delivery confirmation · phone-app user who also wants a keyboard device.

## Requirements

- **Setup:** region → name → time zone → confirm. Regions are the official presets (`api.meshcore.nz`) within
  868–923 MHz, none preselected. **Nothing is transmitted before setup completes.** Confirm sends an advert.
- **Messages:** one inbox for channels and DMs, newest activity first, unread counts. Channels: Public, `#hashtag`
  (key = first 16 bytes of `sha256("#name")`, lowercased), private (name + 16/32-byte key); max 8. DMs: `[..]`
  `[ok]` `[x]`, 3 attempts (the last floods). Channel sends show `[heard N]` repeater echoes. 96 messages kept;
  when full, the busiest conversation loses its oldest. Beep and toast for new messages.
- **Contacts:** auto-added from adverts by type (default chat + repeaters, MeshCore's advice), overwrite oldest
  when full (favourites kept), un-added adverts listed with `+`; favourite, delete, reset path, share, filter.
  Max 100. Lists sort favourites first, then by latest message or advert.
- **Repeaters:** zero-hop discovery (`CTL_TYPE_NODE_DISCOVER_REQ`), replies for 60 s with SNR both ways; known
  repeaters by when last heard, with the SNR of their last relay; unknown ones can be added.
- **Radio info:** big frequency readout, SF/BW/CR/power, RSSI/SNR/noise floor, packet counts, airtime, battery.
- **Settings:** region or manual radio parameters, TX power, path hash size, auto-add, Bluetooth on/off and app
  PIN, brightness, screen timeout, beep, time zone, clock, factory reset.
- **Clock:** no RTC chip: GPS (silenced for now) → phone app → two agreeing mesh senders → manual. `--:--` until set.
- **Phone app:** the official MeshCore app pairs as with any companion radio (`MeshCore-<name>`, 6-digit PIN,
  random each boot unless set from the app); shares contacts, channels and node settings. Messages sent from one
  side don't appear in the other's history; incoming ones show on both.
- **UI:** amber `#FF9500` / dim `#7A4800` on black, red only for errors, 8×16 font, 16 px status bar · 6 rows ·
  16 px hint bar; tabs `1 MSG` `2 CON` `3 RPT` `4 RAD` `5 SET`; typing in a chat goes straight to the compose line.
- **Power:** dim at half the screen timeout, off at the timeout (default 30 s), CPU 80 MHz while off. Phase 2:
  light sleep, power off, ≥ 24 h idle listening.

## Constraints

No PSRAM: fixed-size arrays, ≥ 60 KB free heap (Bluetooth takes ~100 KB). The mesh engine is MeshCore's own
companion code, compiled unmodified; protocol, crypto and routing are never re-implemented.

## References

[MeshCore](https://github.com/meshcore-dev/MeshCore) (`examples/companion_radio`, `docs/`) ·
[radio presets](https://api.meshcore.nz/api/v1/config) · [Ripple UI](https://blog.meshcore.io/2026/04/30/ripple-9.7) ·
[prior Cardputer port](https://github.com/Stachugit/MeshCore-Cardputer-ADV) ·
[onboarding inspiration](https://bayme.sh/docs/getting-started/) ·
[Cap LoRa-1262](https://docs.m5stack.com/en/cap/Cap_LoRa-1262)
