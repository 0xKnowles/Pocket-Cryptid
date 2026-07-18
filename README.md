# Ruby

> [!WARNING]
> **Educational and authorized-testing use only.** Ruby passively captures WiFi and Bluetooth
> traffic from *everything in range*, not just your own devices — and can optionally transmit
> real 802.11 deauthentication frames (see [Active deauth](#active-deauth) — off by default,
> gated behind an explicit target list). **Only point it at networks and devices
> you own, or have explicit written authorization to test.** Capturing traffic from networks you
> don't control, and especially transmitting deauthentication frames at them, is illegal in most
> jurisdictions (wiretapping/interception and RF-interference statutes both apply) regardless of
> how small or "hobbyist" the device is. This project is published for security research and
> education. You are solely responsible for how you use it and for knowing the laws that apply
> where you use it.

<p align="center">
  <img src="bmp/boot.bmp" width="260" alt="Ruby — the boot splash art shown on first power-on">
</p>

<p align="center">
  <a href="https://github.com/0xKnowles/Ruby/actions/workflows/ci.yml">
    <img src="https://github.com/0xKnowles/Ruby/actions/workflows/ci.yml/badge.svg" alt="CI (build) status">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License">
  </a>
</p>

**Ruby** is open-source custom firmware for the **Xteink X3 and X4** e-ink readers that turns the
hardware into something else entirely: a stealthy, long-battery-life passive RF signal analyzer
with a small digital companion living in the corner of the screen.

It is not a book reader. It does not read EPUBs, sync with KOReader, or talk to OPDS catalogs.
It listens — passively, receive-only, by default — to the WiFi and Bluetooth Low Energy traffic
already in the air around it, catalogs what it hears into an encrypted on-device log, and Ruby
reacts with a changing expression as a visual front-end for that catalog — closer to Pwnagotchi's
mood faces than to a pet that eats XP to level up.

### At a glance

- **Hardware:** Xteink X3 or X4, ESP32-C3 (single-core RISC-V, no PSRAM), 792×528 e-ink panel,
  runs in landscape (the panel's native orientation). One firmware binary for both; hardware is
  detected at runtime, not chosen at build time.
- **Capture:** 802.11 monitor-mode WiFi sniffing across all channels (beacons, probe
  requests/responses, WPA 4-way-handshake message detection) plus passive BLE advertisement
  scanning — neither path associates, pairs, or connects to anything by default.
- **Log:** every observation is deduplicated for the on-screen stats and independently written to
  an AES-256-GCM encrypted daily log on the SD card, decryptable on-device (no PC required) or via
  a bundled Python script.
- **Opt-in, off by default:** raw plaintext `.pcap` handshake export for offline auditing with
  hashcat/`hcxpcapngtool` (with on-device PMKID-capable-capture visibility), and an active-deauth
  capability gated by a two-list whitelist/blacklist system — see
  [Active deauth](#active-deauth) for why it's currently confirmed non-functional on this hardware.
- **On-device management:** a live network-scan picker to build the whitelist/blacklist without a
  PC, a Power-button action you can set to Screenshot/Pause/Refresh, and a Settings screen for
  every capture toggle.
- **Vendor lookup, always on, no setting to flip:** Recent Devices falls back to an optional
  vendor-name lookup by MAC OUI for entries with no advertised SSID/name — see
  [Vendor OUI lookup](#vendor-oui-lookup).
- **License:** MIT. Single PlatformIO environment (`default`), built and static-analyzed on every
  push via GitHub Actions.

## Installing Ruby

The easiest way to get Ruby onto real hardware is to flash a prebuilt release straight from your
browser — no toolchain install required.

1. Grab the latest `firmware-default-v*.bin` from the
   **[Releases page](https://github.com/0xKnowles/Ruby/releases/latest)**.
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
<td align="center">A real burst of new<br>unique devices, not<br>just one.</td>
<td align="center">Steady recent<br>RF activity — the<br>everyday baseline.</td>
<td align="center">Quiet for a while.</td>
<td align="center">Quiet for a<br>long while.</td>
</tr>
</table>

There's no XP, no stages, nothing to "feed" permanently — just a mood, the same idea as
Pwnagotchi's faces, driven entirely by `SignalCatalog`'s counts rather than anything cosmetic.
CURIOUS specifically requires several new-unique sightings within a short window, not just one —
a single new device reads as ordinary CONTENT instead, so CURIOUS stays meaningful ("something's
actually picking up") even in a busy RF environment where new devices show up constantly. Pausing
capture (see [Screens](#screens)) shows BORED immediately, since nothing's being observed anymore,
rather than freezing whatever mood was showing right before Pause.

When the device is asleep, it shows a different, much larger piece of art instead:

<p align="center">
  <img src="bmp/sleep.bmp" width="280" alt="Ruby, asleep">
</p>

## How it works

1. **Capture.** `WifiSniffer` puts the ESP32-C3's radio into 802.11 monitor mode and hops across
   channels, extracting beacon/probe-request SSIDs, MAC addresses, and — when it hears one — the
   presence of a WPA 4-way handshake and which message number it is. The radio holds its current
   channel for a few seconds whenever it sees part of a handshake, instead of hopping away and
   missing the rest of it mid-exchange. `BleScanner` runs a **passive** BLE scan (it never sends a
   SCAN_REQ) and records advertised MACs, names, and manufacturer-data length. Neither path
   associates, pairs, or connects to anything, and BLE scanning never transmits. WiFi capture is
   passive by default too, unless [active deauth](#active-deauth) is deliberately turned on — an
   explicit, off-by-default opt-in covered in its own section below.
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

- **Dashboard** (home) — the creature, its current Mood (or "-- PAUSED --" while capture is
  paused), whether handshake capture/deauth are switched on, unique AP/client/BLE/handshake
  counts, capture status, log size, and session uptime. `Confirm` → Settings, `Left` → toggle
  Pause (same button pauses and resumes WiFi/BLE capture — no screen change, and it stops
  `DeauthEngine` too since that only ever fires from the observation stream capture produces),
  `Right` → Export/Maintenance, `Up` → Recent Devices, `Down` → Log Viewer, `Back` → force a full
  ghost-clearing refresh.
- **Settings** — toggle WiFi/BLE capture (BLE off by default — see
  [Hardware](#hardware) for why), adjust WiFi channel dwell time, lock WiFi monitor to a single
  channel instead of hopping all 13 (see
  [Raw handshake capture](#raw-handshake-capture-crackable-pcap-export) for why that matters), set
  the ghost-clear refresh interval, choose what a short Power-button tap does (Refresh /
  Screenshot / Pause — a long hold is always Sleep, not configurable), turn on
  [raw handshake capture](#raw-handshake-capture-crackable-pcap-export) or
  [active deauth](#active-deauth) (both off by default), manage the whitelist/blacklist (opens
  a live network scan to add/remove targets — see [Active deauth](#active-deauth)), reveal the
  log's AES key, wipe the log, or reset the Dashboard's SIGNALS counters back to zero.
- **Export/Maintenance** — how to pull captures off the SD card, plus the current session's
  record count and (when any exist) raw-capture file stats, a PMKID-capable-capture count, and
  deauth burst/frame counters.
- **Recent Devices** — a live, RAM-only feed of the last 16 WiFi/BLE observations (type, MAC,
  RSSI, SSID/name, time since seen), newest first, read-only, falling back to a vendor-name
  lookup (see [Vendor OUI lookup](#vendor-oui-lookup)) for entries with no SSID/name of their
  own. This is separate from both `SignalCatalog` (which deliberately never
  retains which specific MACs it has seen — only dedup counts) and the encrypted log (which
  retains everything, but only ever encrypted at rest). Nothing shown here is persisted; it's
  lost on reboot along with the rest of RAM. To manage the active-deauth target lists, use
  Settings' Whitelist/Blacklist rows instead (a dedicated live network scan — see
  [Active deauth](#active-deauth)).
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

**Realistic capture rates are low — 0-2 handshakes/hour of passive listening is normal, not
broken.** WiFi monitor mode hops all 13 channels by default, spending `wifiChannelDwellMs` (300ms)
on each, so any single channel is only actually being listened to roughly 1/13th of the time. A
real 4-way handshake completes in well under a second, and a stationary network's clients don't
renegotiate one often on their own — combine an inherently rare event with an ~8% chance of being
on the right channel when it happens, and a low hourly count is just the math working as expected.
Two real levers to improve it:
- **`Settings → WiFi channel scope`** locks monitor mode onto one known channel instead of hopping
  all 13 (see it per-network in the live scan behind `Whitelist`/`Blacklist`), raising that
  channel's duty cycle to 100% — at the cost of missing everything on the other 12.
- **[Active deauth](#active-deauth)**, below, is the bigger lever: it forces a handshake to happen
  *now*, on the channel already being listened to, instead of waiting on one to happen by chance.

By itself, this path only ever *listens* — nothing here transmits or provokes a handshake into
happening; it records what a passive monitor already sees handshakes doing on their own. A held
channel lock while a handshake is in progress (see [How it works](#how-it-works)) helps catch the
full exchange, but a real 4-way handshake completing on its own is still not guaranteed. **Active
deauth** (below) is the opt-in answer to that gap — see its own section for the current status of
its transmit capability.

## Active deauth

> **Status: confirmed NOT functional on this hardware, via real-hardware testing.** The
> transient-`WIFI_MODE_STA` approach did fix the earlier crash-loop (many bursts fired across a
> real session with no crash). But every burst's `esp_wifi_80211_tx()` call was rejected by the
> WiFi driver itself — it logs `wifi: unsupport frame type: 0c0` once per rejected frame — matching
> ESP-IDF's own documented behavior: its raw-TX allowlist (beacon/probe request/probe
> response/action/non-QoS data) hard-codes out deauth specifically, at the driver level. Bursts
> still get attempted, counted, and logged (see `Settings → Active deauth`/Export's ACTIVE DEAUTH
> card, which shows a **Rejected by driver** count), but nothing actually reaches the air. The only
> known way past this — patching ESP-IDF's precompiled `libnet80211.a` to weaken/override
> `ieee80211_raw_frame_sanity_check` — is a real, community-used technique, but every example found
> targets classic ESP32/S2/S3 (Xtensa); this device is an **ESP32-C3 (RISC-V)**, a different
> toolchain with no confirmed working precedent, and even on the platforms it's most tried on it
> isn't reliable. Not attempted here as a result — passive capture (below), optionally with
> `Settings → WiFi channel scope` locked to a known target's channel, is the only capture path
> confirmed to actually work on this firmware.

Passive capture alone often isn't enough to actually catch a handshake — a real 4-way handshake
completes in well under a second, and the radio has to already be parked on the right channel
when it happens. **Active deauth** is meant to close that gap by transmitting real 802.11
deauthentication frames at a target network, forcing a client to reconnect so the resulting
handshake lands in raw capture above instead of waiting — often in vain — for one to happen on its
own. On this hardware, per the status above, it doesn't currently manage to transmit anything.

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
  this isn't meant to be a sustained flood against any one network. (Currently moot on this
  hardware per the status above — no frame actually transmits regardless of cooldown/targeting.)
- Frame transmission uses `esp_wifi_80211_tx()`, the same raw-TX primitive most community ESP32
  deauther projects use, but ESP-IDF's own driver rejects the deauth frame type outright before it
  ever reaches the air — see the status note above.

**Treat this as real RF interference against whatever it targets, should the underlying driver
restriction ever get lifted (by this project or a future ESP-IDF release).** Transmitting
deauthentication frames at a network you don't own or don't have explicit authorization to test is
illegal in most jurisdictions, regardless of how small the transmitting device is. Only enable
this against networks you own or are explicitly authorized to audit — the both-lists-empty default
exists so that flipping the setting on can never itself put you outside that boundary; you have to
deliberately add a target first.

## Vendor OUI lookup

Recent Devices falls back to a vendor-name lookup, by MAC address prefix, for entries that don't
already have an SSID or advertised name. This needs a user-supplied file at `/.ruby/oui.txt` — not
shipped with the firmware — one `AABBCC<TAB>Vendor Name` entry per line, the same format IEEE's
public OUI registry export or Wireshark's `manuf` file already use. Without that file present,
unlabeled entries just show "(no name)" as before. No database ships in firmware, so this costs
nothing when the file isn't present — but a full ~50,000-entry registry gets scanned from scratch
on every visible row on every redraw, so a curated subset (common consumer/IoT vendors, say) will
feel a lot snappier than the full registry.

A passive deauth/disassoc detector, BLE tracker detector, and persistent AP history were all
prototyped alongside this on real hardware and then pulled back out: the AP history table alone
added a fixed ~14 KB of permanent RAM use (256 entries, unlike every sibling live-scan cache in
this codebase which stays well under 2 KB), and this chip has only ~380 KB total with no PSRAM to
absorb it. That was enough to reliably starve NimBLE's controller init of the memory it needs, so
BLE passive scan silently stopped working — a worse tradeoff than the awareness features were
worth. Reliable BLE capture won out.

## Hardware

Same target as upstream CrossPlant/CrossInk: **Xteink X3 or X4**, ESP32-C3 (single-core RISC-V,
~380 KB usable RAM, no PSRAM), 792×528 e-ink panel. The dashboard runs in **landscape**
(792×528 logical) — the panel's own native orientation, needing no rotation math to draw into.
`MappedInputManager` is a direct, orientation-unaware passthrough from logical buttons
(Back/Confirm/Left/Right/Up/Down/Power) to hardware — screen orientation is purely a rendering
concern, so it has no effect on what any button does. What *does* change with orientation is
`Chrome::drawFooterHints`: in landscape, the on-screen button-hint pills move from a horizontal
bar along the bottom to a vertical strip along the right edge, with rotated text — see
`ui/Chrome.cpp` for the coordinate-geometry reasoning behind that specific edge.

The ESP32-C3 has integrated WiFi 802.11 b/g/n and Bluetooth 5 (LE only) — both capture paths run
on the same radio hardware the original firmware used for book downloads and OTA updates.

**BLE passive scan is off by default**, unlike WiFi monitor capture. Real-hardware testing found
that starting it fragments the same DMA-capable memory pool `EncryptedLog`'s hardware AES needs
down to just a few KB, immediately at boot — tripping a proactive safety restart every single
time, identically, before the device ever became interactive. Restarting alone didn't fix anything
since the exact same fragmentation reproduced on the very next boot, so the device sat in an
endless ~1-second restart loop that looked and felt exactly like a hang. A crash-loop guard now
forces BLE (and raw handshake capture and active deauth, the other two "extra" capture paths) back
off automatically after 3 restarts in a row, so a device stuck in that loop recovers into a
working state on its own — but the underlying memory pressure that causes it in the first place
isn't fixed, just contained. Turn BLE on deliberately, in Settings, with that in mind.

**BLE passive scan and raw handshake capture can't both be on.** Further testing found BLE alone
settles at a steep but survivable ~63 KB of heap use once running — fine on its own — but turning
raw handshake capture on *while BLE is already running* (or the other way around) is what actually
tips the DMA-capable pool into fragmenting badly enough to crash-loop. `Settings` now refuses to
enable either one while the other is already active, so hitting this no longer means waiting out
a restart loop — just turn the one you don't need off first.

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
| `lib/RfCapture` | **New.** 802.11 monitor-mode WiFi sniffing, passive BLE advertisement scanning, the opt-in raw-frame path behind `.pcap` export (with on-device PMKID visibility), and the opt-in `DeauthEngine`/`TargetList` active-deauth capability. |
| `lib/SignalCatalog` | **New.** Deduplicates observations into "have I seen this MAC before" and lifetime unique-device counters, plus a small RAM-only ring of recent sightings for the Recent Devices screen and a live dedup-by-BSSID scan cache for the network-picker screens. |
| `lib/RubyLog` | **New.** AES-256-GCM encrypted append-only capture log. |
| `src/ruby` | **New.** The creature: procedurally-rendered (no bitmap art pipeline for the logic — see `bmp/` for the actual source art), fed by `SignalCatalog`. |
| `src/activities/*` | **New.** Dashboard, Settings, device list, log viewer, network picker (`targets/`), and Export/Maintenance screens replace CrossPlant's reader/browser/pet activities entirely. |
| `src/CaptureControl.h/.cpp`, `src/Screenshot.h/.cpp` | **New.** Session-only capture pause and framebuffer-to-BMP screenshot, both reachable from the Power button (see [Screens](#screens)). |

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

`/.ruby/oui.txt` (vendor OUI lookup, see [Vendor OUI lookup](#vendor-oui-lookup)) goes the other
direction — it's a file you supply yourself, not one Ruby writes, so there's nothing to export
for it.

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
  always an explicit long-press of the Power button — a short tap does whatever
  [Settings → Power button (tap)](#screens) is set to instead.
- **A "Lore" progression screen** — an earlier build unlocked short flavor-text entries as
  captures accumulated. It read as a pet-sim feature bolted onto a recon tool rather than
  something that earned its screen real estate, so it was removed outright in favor of
  [Pause](#screens) on the same Dashboard button.

## License

MIT, same as upstream CrossPlant/CrossInk. `freeink-sdk/` carries its own MIT license and
third-party attribution — see `freeink-sdk/NOTICE`.
