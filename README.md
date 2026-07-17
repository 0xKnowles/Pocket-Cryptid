# Ruby

<p align="center">
  <img src="bmp/boot.bmp" width="260" alt="Ruby — the boot splash art shown on first power-on">
</p>

<p align="center">
  <a href="https://github.com/0xKnowles/Pocket-Cryptid/actions/workflows/ci.yml">
    <img src="https://github.com/0xKnowles/Pocket-Cryptid/actions/workflows/ci.yml/badge.svg" alt="CI (build) status">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License">
  </a>
</p>

**Ruby** is open-source custom firmware for the **Xteink X3 and X4** e-ink readers that turns the
hardware into something else entirely: a stealthy, long-battery-life passive RF signal analyzer
with a small digital companion living in the corner of the screen.

It is not a book reader. It does not read EPUBs, sync with KOReader, or talk to OPDS catalogs.
It listens — passively, receive-only — to the WiFi and Bluetooth Low Energy traffic already in
the air around it, catalogs what it hears into an encrypted on-device log, and Ruby reacts with a
changing expression as a visual front-end for that catalog — closer to Pwnagotchi's mood faces
than to a pet that eats XP to level up.

## Installing Ruby

The easiest way to get Ruby onto real hardware is to flash a prebuilt release straight from your
browser — no toolchain install required.

1. Grab the latest `firmware-default-v*.bin` from the
   **[Releases page](https://github.com/0xKnowles/Pocket-Cryptid/releases/latest)**.
2. Plug the X3/X4 into your computer with a USB-C cable.
3. Open **[CrossPoint Reader's flash tool](https://crosspointreader.com/#flash-tools)** in a
   Chromium-based browser (Chrome, Edge, or Opera — the page uses the Web Serial API to talk to
   the device directly, which Firefox and Safari don't support).
4. Select the device's serial port when prompted, point the tool at the `.bin` you downloaded,
   and follow its on-screen flashing steps. It writes directly over USB; nothing leaves your
   machine.
5. Once it finishes, power-cycle the device. It should boot straight to the splash art above and
   land on the dashboard.

This is the same web-based flashing tool used for CrossPlant/CrossInk-family firmware on this
hardware, since Ruby shares its low-level display/power/storage bring-up with that project (see
[Where this comes from](#where-this-comes-from)).

Prefer to build it yourself, audit the source first, or you're developing against it? See
[Building from source](#building-from-source) below.

## Meet Ruby

The creature doesn't level up or evolve — it's one fixed shape, already fully itself. What
changes is its **expression**, redrawn in a small fixed corner box on the dashboard as a live
reaction to what's been heard most recently over the air:

<table>
<tr>
<td align="center"><img src="bmp/excited.bmp" width="120" alt="Ruby, EXCITED"><br><b>EXCITED</b></td>
<td align="center"><img src="bmp/curious.bmp" width="120" alt="Ruby, CURIOUS"><br><b>CURIOUS</b></td>
<td align="center"><img src="bmp/content.bmp" width="120" alt="Ruby, CONTENT"><br><b>CONTENT</b></td>
<td align="center"><img src="bmp/bored.bmp" width="120" alt="Ruby, BORED"><br><b>BORED</b></td>
<td align="center"><img src="bmp/lonely.bmp" width="120" alt="Ruby, LONELY"><br><b>LONELY</b></td>
</tr>
<tr>
<td align="center">A handshake was just<br>captured — rare, the<br>biggest find.</td>
<td align="center">Any new unique device<br>was seen moments ago.</td>
<td align="center">Steady recent<br>RF activity.</td>
<td align="center">Quiet for a while.</td>
<td align="center">Quiet for a<br>long while.</td>
</tr>
</table>

There's no XP, no stages, nothing to "feed" permanently — just a mood, the same idea as
Pwnagotchi's faces, driven entirely by `SignalCatalog`'s counts rather than anything cosmetic.

When the device is asleep, it shows a different, much larger piece of art instead:

<p align="center">
  <img src="bmp/sleep.bmp" width="280" alt="Ruby, asleep">
</p>

## How it works

1. **Capture.** `WifiSniffer` puts the ESP32-C3's radio into 802.11 monitor mode and hops across
   channels, extracting beacon/probe-request SSIDs, MAC addresses, and — when it hears one — the
   presence of a WPA 4-way handshake and which message number it is. `BleScanner` runs a
   **passive** BLE scan (it never sends a SCAN_REQ) and records advertised MACs, names, and
   manufacturer-data length. Neither path associates, pairs, or connects to anything, and BLE
   scanning never transmits. WiFi capture is passive by default too, unless
   [active deauth](#active-deauth) is deliberately turned on — an explicit, off-by-default opt-in
   covered in its own section below.
2. **Catalog.** Every observation is deduplicated by `SignalCatalog` against a bounded in-RAM
   hash set. The first time a given MAC/role is seen, that's a "unique" event: it increments a
   lifetime counter and nudges the creature's expression.
3. **Log.** Independently of deduplication, *every* observation is encrypted (AES-256-GCM via
   mbedtls, one record per packet) and appended to a daily log file on the SD card. The AES key
   is derived from a random seed generated on first boot plus this chip's eFuse MAC address and
   lives only in internal NVS flash — pulling the SD card gets you ciphertext with no key beside
   it. Note this means every sighting is logged, not just first-ever ones — a busy RF
   environment (a handful of phones re-advertising over BLE every few hundred milliseconds, a
   dozen APs beaconing every ~100ms) can write a meaningful volume of records per hour. That's
   intentional for a capture tool, but budget SD card space and export/rotate accordingly. See
   [Exporting captures](#exporting-captures).
4. **React.** The creature (`RubyManager`) doesn't level up or grow — see [Meet Ruby](#meet-ruby)
   above for what each expression means and when it shows.
5. **Display.** The dashboard is mostly static: a header, Ruby's box with its live Mood readout,
   boxed RF stat panels, a footer. The creature lives in a fixed corner box that redraws on its
   own ~1.2s cadence using `HalDisplay::FAST_REFRESH` without ever touching (or `clearScreen()`-ing)
   any pixel outside that box — see `RubySpriteRenderer.h` and `DashboardActivity.h` for the full
   explanation of why that reads as a genuine partial refresh on hardware that has no
   windowed-update API.

## Screens

- **Dashboard** (home) — the creature, its current Mood, whether handshake capture/deauth are on,
  unique AP/client/BLE/handshake counts, capture status, log size, and session uptime.
  `Confirm` → Settings, `Left` → toggle Pause (same
  button pauses and resumes WiFi/BLE capture — no screen change), `Right` → Export/Maintenance,
  `Up` → Recent Devices, `Down` → Log Viewer, `Back` → force a full ghost-clearing refresh.
- **Settings** — toggle WiFi/BLE capture, adjust WiFi channel dwell time, set the ghost-clear
  refresh interval, choose what a short Power-button tap does (Refresh / Screenshot / Pause — a
  long hold is always Sleep, not configurable), turn on
  [raw handshake capture](#raw-handshake-capture-crackable-pcap-export) or
  [active deauth](#active-deauth) (both off by default), manage the whitelist/blacklist (opens
  a live network scan to add/remove targets — see [Active deauth](#active-deauth)), reveal the
  log's AES key, wipe the log.
- **Export/Maintenance** — how to pull captures off the SD card, plus the current session's
  record count and (when any exist) raw-capture file stats and deauth burst/frame counters.
- **Recent Devices** — a live, RAM-only feed of the last 16 WiFi/BLE observations (type, MAC,
  RSSI, SSID/name, time since seen), newest first, read-only. This is separate from both
  `SignalCatalog` (which deliberately never retains which specific MACs it has seen — only dedup
  counts) and the encrypted log (which retains everything, but only ever encrypted at rest).
  Nothing shown here is persisted; it's lost on reboot along with the rest of RAM. To manage the
  active-deauth target lists, use Settings' Whitelist/Blacklist rows instead (a dedicated live
  network scan — see [Active deauth](#active-deauth)).
- **Log Viewer** — browses the encrypted capture log *on the device itself*, no PC required. See
  [On-device log decryption](#on-device-log-decryption) below for how that's possible without any
  key-entry UI. `Left`/`Right` switch between daily log files, `Up`/`Down` page through records
  within a file (5 at a time, newest first).

## Raw handshake capture (crackable `.pcap` export)

Everything above is deliberately metadata-only: the encrypted log records *that* a handshake
happened and which message numbers were seen, never the key material itself. **Raw handshake
capture** is a separate, opt-in capability for a different purpose — auditing the password
strength of networks you own, offline, with tools like [hashcat](https://hashcat.net/hashcat/) or
[hcxpcapngtool](https://github.com/ZerBea/hcxtools).

- **Off by default.** Turn it on at `Settings → Raw handshake capture`. The Settings screen shows
  a warning explaining the tradeoff below before you flip it on.
- When enabled, it captures the WPA 4-way-handshake frames verbatim (ANonce/SNonce/MIC included)
  plus the SSID-bearing beacon for each network involved, and writes them to a standard libpcap
  file at `/.ruby/pcap/YYYYMMDD.pcap` — the same format hashcat/hcxpcapngtool already expect, so
  a captured file can be fed straight in to attempt a crack (e.g. converted to hashcat's `.22000`
  format via `hcxpcapngtool`).
- **These files are plaintext**, unlike everything else Ruby writes to SD. A cracking tool needs
  the exact bytes that went over the air, so this one path can't be routed through the AES-256-GCM
  encrypted log the way every other observation is. `MaintenanceActivity` calls this out
  explicitly whenever raw-capture files exist.
- The Export screen also shows a **PMKID-capable** count — how many captured message-1 frames
  carry the PMKID key-data element, each one independently crackable via hashcat's `-m 22000`
  PMKID mode without needing the rest of the handshake at all.
- Turn it back off when you're not actively auditing — it's meant to be run deliberately, not left
  on as a background default.

By itself, this path only ever *listens* — nothing here transmits or provokes a handshake into
happening; it records what a passive monitor already sees handshakes doing on their own. That's a
real limitation: catching a full 4-way handshake by chance while hopping across 13 channels every
300ms is unreliable in practice. **Active deauth** (below) is the opt-in answer to that.

## Active deauth

> **Status: currently non-functional.** The first attempt at this (WIFI_MODE_STA to get a
> TX-capable interface) crashed the device on every boot on real X3 hardware — see
> [CHANGELOG](CHANGELOG.md) for the root cause. Reverted to the working WIFI_MODE_NULL baseline;
> the toggle and target list below still work, but deauth frames don't actually transmit right
> now. Left documented here as the intended behavior once a safe way to get TX capability on this
> hardware is found.

Passive capture alone often isn't enough to actually catch a handshake — a real 4-way handshake
completes in well under a second, and the radio has to already be parked on the right channel
when it happens. **Active deauth** closes that gap by transmitting real 802.11 deauthentication
frames at a target network, forcing a client to reconnect so the resulting handshake lands in raw
capture above instead of waiting — often in vain — for one to happen on its own.

- **Off by default**, and requires raw handshake capture to also be on — forcing a handshake
  nobody's capturing verbatim would just be disruption for nothing. Turn it on at
  `Settings → Active deauth`.
- **Every target is gated by two genuinely independent lists** (`TargetList`, backed by
  `/.ruby/targets.txt` on the SD card):
  - **Whitelist non-empty** — only the BSSIDs on it are attacked. The blacklist is ignored in
    this case.
  - **Whitelist empty, blacklist non-empty** — every BSSID Ruby sees is attacked *except* the
    ones on the blacklist.
  - **Both empty** (the default) — *nothing* is attacked, even with the feature switched on.
  - Manage both from `Settings → Whitelist` / `Settings → Blacklist` — each opens a live network
    scan (like picking a network to connect to); press `Confirm` on an entry to add or remove it,
    a `[+]` marker shows current membership. Or edit `/.ruby/targets.txt` directly on a PC with
    the SD card out — entries live under `[whitelist]`/`[blacklist]` section headers.
- Each targeted BSSID gets a short burst of deauth frames, then a 30-second cooldown before it's
  attacked again, and attacks stop entirely for a BSSID once its handshake has been captured —
  this isn't meant to be a sustained flood against any one network.
- Frame transmission uses `esp_wifi_80211_tx()`, the same raw-TX primitive most community ESP32
  deauther projects use. It works in practice, but deauth frames aren't among the types ESP-IDF's
  own documentation calls "supported" for that function — this is a widely-used technique, not an
  officially documented one.

**This is real RF interference against whatever it targets.** Transmitting deauthentication
frames at a network you don't own or don't have explicit authorization to test is illegal in most
jurisdictions, regardless of how small the transmitting device is. Only enable this against
networks you own or are explicitly authorized to audit — the both-lists-empty default exists so
that flipping the setting on can never itself put you outside that boundary; you have to
deliberately add a target first.

## Hardware

Same target as upstream CrossPlant/CrossInk: **Xteink X3 or X4**, ESP32-C3 (single-core RISC-V,
~380 KB usable RAM, no PSRAM), 792×528 e-ink panel. The dashboard runs in **landscape**
(792×528 logical) — the panel's own native orientation, needing no rotation math to draw into.
`MappedInputManager` is a direct, orientation-unaware passthrough from logical buttons
(Back/Confirm/Left/Right/Up/Down) to hardware — screen orientation is purely a rendering concern,
so it has no effect on what any button does. What *does* change with orientation is
`Chrome::drawFooterHints`: in landscape, the on-screen button-hint pills move from a horizontal
bar along the bottom to a vertical strip along the right edge, with rotated text — see
`ui/Chrome.cpp` for the coordinate-geometry reasoning behind that specific edge.

The ESP32-C3 has integrated WiFi 802.11 b/g/n and Bluetooth 5 (LE only) — both capture paths run
on the same radio hardware the original firmware used for book downloads and OTA updates.

## Look and feel

The UI font is [Space Mono](https://github.com/googlefonts/spacemono) (SIL OFL 1.1) — a monospace
face chosen to read as "instrument readout" rather than "app UI," which fits a device whose whole
job is displaying raw RF telemetry (see `lib/EpdFont/builtinFonts/all.h`). Boxed elements (the
footer's button tabs, the battery badge, Ruby's box, Settings' selected row) use a shared
rounded-outline style (`Chrome::kCardRadius`) instead of solid filled highlights — outlines read as
"buttons" without being the heaviest, most ghost-prone thing on the screen after a `FAST_REFRESH`.

All of Ruby's art (splash, mood faces, sleep art — see [Meet Ruby](#meet-ruby)) is source BMP
under [`bmp/`](bmp/), baked into the firmware image at build time so a freshly flashed device
shows it immediately with no SD card setup required. It can still be overridden per-file at
`/bmp/<name>.bmp` on the SD card without recompiling — see [`bmp/README.md`](bmp/README.md) for
the full format/size table and how to swap in your own art.

## Where this comes from

Ruby is built on [CrossPlant](https://github.com/0xKnowles/CrossPlant)'s hardware
foundation — the same e-ink display driver, input handling, power management, SD storage, font
rendering, and activity/screen framework that make CrossPlant (and its own ancestor, CrossInk)
boot and draw on real Xteink hardware. Everything above that foundation — the RF capture layer,
the encrypted log, the creature, every screen — is new.

| Layer | What it is |
| --- | --- |
| `freeink-sdk/`, `lib/hal`, `lib/GfxRenderer`, `lib/EpdFont` | Hardware bring-up: display driver, buttons, power, SD card, fonts, graphics primitives. Carried over from CrossPlant/CrossInk largely unmodified — this is what makes the firmware boot on real hardware. |
| `lib/RfCapture` | **New.** 802.11 monitor-mode WiFi sniffing, passive BLE advertisement scanning, the opt-in raw-frame path behind `.pcap` export, and the opt-in `DeauthEngine`/`TargetList` active-deauth capability. |
| `lib/SignalCatalog` | **New.** Deduplicates observations into "have I seen this MAC before" and lifetime unique-device counters, plus a small RAM-only ring of recent sightings for the Recent Devices screen. |
| `lib/RubyLog` | **New.** AES-256-GCM encrypted append-only capture log. |
| `src/ruby` | **New.** The creature: procedurally-rendered (no bitmap art pipeline for the logic — see `bmp/` for the actual source art), fed by `SignalCatalog`. |
| `src/activities/*` | **New.** Dashboard, Settings, device list, log viewer, network picker (`targets/`), and Export/Maintenance screens replace CrossPlant's reader/browser/pet activities entirely. |

Reading-specific subsystems (EPUB/TXT/XTC rendering, the file browser, OPDS, KOReader sync,
WiFi-connected file transfer, the Lexend Deca/Bitter/Charein reading fonts, i18n) were removed,
not disabled — there is no code path for them left in this repo.

## Building from source

```sh
pio run -e default          # compile
pio run -e default -t upload  # flash over USB
```

There is a single PlatformIO environment (`default`). Unlike upstream CrossPlant's `tiny`/
`xlarge` split — which existed to omit different reading-font sizes depending on flash budget —
Ruby ships one fixed, small UI font set, so there's no equivalent tradeoff to build variants
around. The same binary supports both X3 and X4: hardware is detected at runtime
(`HalGPIO::deviceIsX3()`), not at build time.

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) builds this env and runs `pio check`
static analysis on every push/PR.

## On-device log decryption

The **Log Viewer** screen (Dashboard → `Down`) decrypts and browses the log right on the device,
with no key ever typed in. This works because the AES-256 key that encrypted a given file never
actually left the device that wrote it: `EncryptedLog` derives it once at boot (hardware TRNG seed
+ eFuse MAC, see [What "encrypted" means here](#what-encrypted-means-here)) and keeps it resident
in RAM for as long as the firmware is running. Reading a record back is just
`mbedtls_gcm_auth_decrypt` with that same in-memory key — the same primitive `writeEnvelope` uses
in reverse, plus GCM's built-in authentication tag check, which doubles as corruption/tamper
detection for free.

Random access to record N is O(1) rather than a scan from the start of the file: every record is
encrypted from a fixed-size 39-byte `LogRecordPlaintext`, so every on-disk envelope is exactly the
same 70 bytes (`kLogRecordEnvelopeSize`), and record N always starts at byte `N * 70`. That's what
lets the Log Viewer jump straight to "the last 8 records" or "the 8 before that" via
`EncryptedLog::decryptRecordRange()` without touching anything else in the file.

The PC-side path still exists and is still the only way to get the *plaintext* off the device
entirely (Log Viewer only ever displays it on-screen):

```sh
pip install -r scripts/requirements.txt
python3 scripts/decrypt_log.py --key <64 hex chars> 20260717.pclog
```

## Exporting captures

There is no USB/WiFi transfer protocol — power the device off, pull the SD card, and copy files
to a computer:

- **Encrypted log** — `/.ruby/log/*.pclog`. Decrypt with the command in
  [On-device log decryption](#on-device-log-decryption) above and the key from
  **Settings → Reveal log key**.
- **Raw handshake captures** (only present if you turned the feature on) —
  `/.ruby/pcap/*.pcap`, already plaintext. Load these directly into hashcat or `hcxpcapngtool`;
  see [Raw handshake capture](#raw-handshake-capture-crackable-pcap-export) above.
- **Screenshots** (only present if you used them) — `/.ruby/screenshots/*.bmp`, plain
  uncompressed 1-bit bitmaps. Set **Settings → Power button (tap) → Screenshot** to save one with
  a short tap of the Power button.

## What "encrypted" means here

The log is AES-256-GCM encrypted at rest so that a lost or stolen SD card doesn't hand over a
plaintext capture history. It is **not** anonymized — record contents are the same MAC
addresses, SSIDs, and RSSI values a passive analyzer would see over the air, deliberately, so the
log is actually useful for its purpose. Treat it and the device itself the same way you'd treat
any other RF capture tool: know the rules that apply where you use it, and don't point it at
networks or people without a reason you'd stand behind.

This does **not** apply to raw handshake capture (see above): those `.pcap` files are plaintext
by necessity and are off by default specifically because they don't get this protection.

## What was removed, and why

Book-reading, remote sync, and WiFi-client features made no sense for a device whose entire
value proposition is that it never associates with a network:

- **Epub/Txt/Xtc readers, file browser, OPDS, KOReader sync** — no reading surface exists.
- **WiFi station mode / "join network" / web server / WebDAV / OTA-over-HTTP** — dropped
  entirely. The device's WiFi radio only ever runs in monitor mode; it never has an IP address.
  Firmware updates are USB-only ([flash a release](#installing-ruby) or `pio run -t upload`), and
  capture export is SD-card-only.
- **Multi-language reading fonts (Lexend Deca, Bitter, Charein) and i18n** — the UI is English-
  only dashboard chrome, not paragraphs of book text, so ~57 MB of glyph tables and the
  translation pipeline were both unnecessary.
- **Auto-sleep on inactivity** — a reader auto-sleeps when you stop pressing buttons because
  reading is a foreground activity. A passive analyzer's entire job is to sit still and listen
  with no buttons pressed; auto-sleeping on that basis would defeat the point. Sleep here is
  always an explicit power-button press.

## License

MIT, same as upstream CrossPlant/CrossInk. `freeink-sdk/` carries its own MIT license and
third-party attribution — see `freeink-sdk/NOTICE`.
