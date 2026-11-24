# MeshCore Cardputer

A [MeshCore](https://github.com/meshcore-dev/MeshCore) chat client for the **M5Stack Cardputer-Adv** with the
**Cap LoRa-1262**. Its screen and keyboard work on their own, like a pocket pager for the mesh, and the
**official MeshCore phone app** can pair with it over Bluetooth at the same time.

The mesh runs on MeshCore's own companion engine, so it talks to MeshCore apps, companion radios, Ripple devices
and repeaters.

| | |
|---|---|
| ![Inbox](docs/screens/msg_inbox.png) | ![Channel chat](docs/screens/chat.png) |
| **1 MSG** inbox: channels and DMs, newest first | Channel chat: `[heard 3]` = repeaters re-sent it |
| ![Contacts](docs/screens/contacts.png) | ![Contact](docs/screens/contact.png) |
| **2 CON** contacts: favourites, then by activity | Contact details and actions |
| ![Repeaters](docs/screens/repeaters.png) | ![Radio](docs/screens/radio.png) |
| **3 RPT** repeaters: discovery replies, SNR in/out | **4 RAD** radio: frequency, signal, packet counts |
| ![Settings](docs/screens/settings.png) | ![Setup](docs/screens/setup_region.png) |
| **5 SET** settings, incl. Bluetooth and app PIN | First-boot setup: pick your region |
| ![Test: packets](docs/screens/test_packets.png) | ![Test: RSSI](docs/screens/test_rssi.png) |
| **6 TST** (optional): live packet log | **6 TST**: live RSSI graph, packets bright |
| ![Quick replies](docs/screens/quick.png) | ![Edit quick replies](docs/screens/quick_set.png) |
| Quick replies: `Enter` on an empty line | Edit them in SET > **Quick replies** |

## Features

- **Setup without a phone:** region → name → time zone → on air. Nothing is sent before a region is chosen.
- **Messages:** Public, `#hashtag` and private channels, direct messages with delivery marks (`[..]` `[ok]`
  `[x]`) and retries, `@[name]` replies, quick replies, unread counts, beep.
- **Contacts and repeaters:** auto-added from adverts, favourites, share, filter by typing, repeater discovery
  with SNR both ways. Lists sort by latest activity.
- **Phone app over Bluetooth:** same contacts, channels and radio settings as the keyboard; both work at once.
- **GPS** (SET > **GPS**: off / clock / location): sets the clock; in location mode a fresh position every 10 min.
  Send your location from any chat, see distance and direction to contacts, optionally share it in adverts. The
  GPS only runs for short fixes and sleeps otherwise.
- **Clock:** from the GPS, the phone app or the mesh; or set it by hand.
- **Radio and power:** region presets (868–923 MHz) or manual settings, airtime and noise floor, screen dims and
  sleeps when idle.
- **Colour schemes:** amber (default), green phosphor, VFD cyan, red, blue LCD, classic LCD and paper.

![Colour schemes](docs/screens/themes.png)

## Hardware

[Cardputer-Adv](https://docs.m5stack.com/en/core/Cardputer-Adv) +
[Cap LoRa-1262](https://docs.m5stack.com/en/cap/Cap_LoRa-1262) (868–923 MHz only, so no 433 MHz presets).
Not for the original Cardputer. Attach the antenna before you finish setup; without the Cap the device boots but
shows `RADIO FAIL: check LoRa Cap`.

## Install

- **M5Burner**: search for *Bruce's MeshCore* (Cardputer, Network & Communication), or burn it from the browser
  at https://burner.m5stack.com/U3W4DS.
- **Web installer**: https://zbruceli.github.io/meshcore_cardputer/ in Chrome or Edge.
- **MeshCore web flasher**: https://flasher.meshcore.io → Custom Firmware → the `.bin` from
  [Releases](https://github.com/zbruceli/meshcore_cardputer/releases).
- **esptool**: `esptool.py --chip esp32s3 write_flash 0x0 meshcore-cardputer-vX.Y.Z.bin`

All of them write a merged image at `0x0`, replacing whatever was on the device (including a launcher). Updates
keep your identity, contacts and messages unless you erase. The phone app has to pair again after an update: on the
phone, forget `MeshCore-<name>` in Bluetooth settings, then connect with the PIN from 5 SET. Afterwards switch the Cardputer off and on; the first
boot after a fresh install takes about 25 s while storage is set up.
If the computer doesn't see it: power switch **off**, hold **G0** while plugging in USB, release after 2–3 s.

## Using it

| Key | Action |
|---|---|
| `Tab`, `1`–`5` | Switch tabs (`Fn`+digit while typing) |
| `;` `.` `,` `/` | Up, down, left, right (`Fn` while typing) |
| `Enter` / `` ` `` / `Del` | Select or send / back / delete |
| G0 button | Send an advert (announce yourself) |

- **Chat:** just type; `Enter` sends. `Fn+R` in a channel starts a reply to the latest sender. `Enter` on an
  empty line lists your quick replies: `1`–`8` (or arrows and `Enter`) puts one in the compose line, `Enter`
  sends it. Edit them in SET > **Quick replies** (`Enter` edit, `Del` twice remove). The last one, `0` **My
  location**, adds a `geo:` link with your GPS position (it waits for a fix if the last one is old; `` ` `` cancels).
- **1 MSG:** `A` add a channel (`#name`, or a name then its key), `X` twice to leave or delete.
- **2 CON:** type to filter. In a contact: `Enter` message, `F` favourite, `R` reset path, `S` share, `X` delete.
- **3 RPT:** `D` asks nearby repeaters to reply (60 s).
- **4 RAD:** `Enter` flood advert, `Z` zero-hop advert.
- **6 TST** (turn on with **Test tab** in SET): `,` `/` switch pages. Packets: last 32 in and out, `Enter` for
  path, sender and repeater. Stats: counters, CRC errors, airtime, `R` resets. RSSI: live signal graph (24 s).
- **5 SET:** `,` `/` change, `Enter` edit. **Colours** picks a scheme (see below). **My location**: `Enter` gets
  a fresh fix. **Loc in adverts** shares your position with the mesh (off by default; same setting as the phone
  app). Factory reset needs `Enter` twice.

**Phone app:** in the MeshCore app, connect to `MeshCore-<your name>` and enter the **App PIN** from 5 SET (new
each boot). Messages you send from one side don't show in the other's history; incoming ones show on both.

Tips: press G0 once after setup so others can add you. A channel message stuck at `[sent]` wasn't heard
re-sent by any repeater. Change the time zone in SET when daylight saving changes.

## Build from source

```bash
git clone https://github.com/zbruceli/meshcore_cardputer.git && cd meshcore_cardputer
pio run -t upload        # PlatformIO; then power-cycle the Cardputer
```

Docs: [PRD.md](PRD.md) (scope) · [roadmap_plan.md](roadmap_plan.md) (status, priorities) ·
[CLAUDE.md](CLAUDE.md) (code, hardware, rules). Screenshots: `python3 scripts/render_screens.py`.

Built on [MeshCore](https://github.com/meshcore-dev/MeshCore) and [M5Cardputer](https://github.com/m5stack/M5Cardputer);
pins from [MeshCore-Cardputer-ADV](https://github.com/Stachugit/MeshCore-Cardputer-ADV). License: [MIT](LICENSE).
