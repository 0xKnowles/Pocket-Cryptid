# Ruby

**Ruby** is open-source custom firmware for the **Xteink X3 and X4** e-ink readers that turns the
hardware into something else entirely: a stealthy, long-battery-life passive RF signal analyzer
with a small digital companion living in the corner of the screen.

It is not a book reader. It does not read EPUBs, sync with KOReader, or talk to OPDS catalogs.
It listens — passively, receive-only — to the WiFi and Bluetooth Low Energy traffic already in
the air around it, catalogs what it hears into an encrypted on-device log, and Ruby reacts with a
changing expression as a visual front-end for that catalog — closer to Pwnagotchi's mood faces
than to a pet that eats XP to level up.

## Where this comes from

Ruby is built on [CrossPlant](https://github.com/0xKnowles/CrossPlant)'s hardware
foundation — the same e-ink display driver, input handling, power management, SD storage, font
rendering, and activity/screen framework that make CrossPlant (and its own ancestor, CrossInk)
boot and draw on real Xteink hardware. Everything above that foundation — the RF capture layer,
the encrypted log, the creature, every screen — is new.

| Layer | What it is |
| --- | --- |
| `freeink-sdk/`, `lib/hal`, `lib/GfxRenderer`, `lib/EpdFont` | Hardware bring-up: display driver, buttons, power, SD card, fonts, graphics primitives. Carried over from CrossPlant/CrossInk largely unmodified — this is what makes the firmware boot on real hardware. |
| `lib/RfCapture` | **New.** Passive 802.11 monitor-mode WiFi sniffing and passive BLE advertisement scanning. |
| `lib/SignalCatalog` | **New.** Deduplicates observations into "have I seen this MAC before" and lifetime unique-device counters, plus a small RAM-only ring of recent sightings for the Recent Devices screen. |
| `lib/RubyLog` | **New.** AES-256-GCM encrypted append-only capture log. |
| `src/ruby` | **New.** The creature: procedurally-rendered (no bitmap art pipeline), fed by `SignalCatalog`. |
| `src/activities/*` | **New.** Dashboard, Settings, Lore, and Maintenance screens replace CrossPlant's reader/browser/pet activities entirely. |

Reading-specific subsystems (EPUB/TXT/XTC rendering, the file browser, OPDS, KOReader sync,
WiFi-connected file transfer, the Lexend Deca/Bitter/Charein reading fonts, i18n) were removed,
not disabled — there is no code path for them left in this repo.

## How it works

1. **Capture.** `WifiSniffer` puts the ESP32-C3's radio into 802.11 monitor mode and hops across
   channels, extracting beacon/probe-request SSIDs, MAC addresses, and — when it hears one — the
   presence of a WPA 4-way handshake and which message number it is. `BleScanner` runs a
   **passive** BLE scan (it never sends a SCAN_REQ) and records advertised MACs, names, and
   manufacturer-data length. Neither path ever transmits, associates, pairs, or connects to
   anything — this device only listens.
2. **Catalog.** Every observation is deduplicated by `SignalCatalog` against a bounded in-RAM
   hash set. The first time a given MAC/role is seen, that's a "unique" event: it increments a
   lifetime counter, nudges the creature's expression, and — every few captures — unlocks another
   line of lore.
3. **Log.** Independently of deduplication, *every* observation is encrypted (AES-256-GCM via
   mbedtls, one record per packet) and appended to a daily log file on the SD card. The AES key
   is derived from a random seed generated on first boot plus this chip's eFuse MAC address and
   lives only in internal NVS flash — pulling the SD card gets you ciphertext with no key beside
   it. Note this means every sighting is logged, not just first-ever ones — a busy RF
   environment (a handful of phones re-advertising over BLE every few hundred milliseconds, a
   dozen APs beaconing every ~100ms) can write a meaningful volume of records per hour. That's
   intentional for a capture tool, but budget SD card space and export/rotate accordingly. See
   [Exporting the log](#exporting-the-log).
4. **React.** The creature (`RubyManager`) doesn't level up or grow — it's one fixed shape,
   already fully itself, and what changes is its **expression**: `EXCITED` for a few seconds right
   after a handshake capture (rare, the biggest find), `CURIOUS` for a shorter flash after any new
   unique device, settling into `CONTENT` → `BORED` → `LONELY` the longer the RF environment stays
   quiet. There's no XP, no stages, nothing to "feed" permanently — just a reaction to what it's
   heard most recently, the same idea as Pwnagotchi's mood faces.
5. **Display.** The dashboard is mostly static: a header, a specimen card, boxed RF stat panels, a
   footer. The creature lives in a fixed corner box that redraws on its own ~1.2s cadence using
   `HalDisplay::FAST_REFRESH` without ever touching (or `clearScreen()`-ing) any pixel outside
   that box — see `RubySpriteRenderer.h` and `DashboardActivity.h` for the full explanation
   of why that reads as a genuine partial refresh on hardware that has no windowed-update API.

## Screens

- **Dashboard** (home) — the creature, its current expression, lore-unlock progress, unique
  AP/client/BLE/handshake counts, capture status, log size, and session uptime. `Confirm` →
  Settings, `Left` → Lore, `Right` → Export/Maintenance, `Up` → Recent Devices, `Down` → Log
  Viewer, `Back` → force a full ghost-clearing refresh.
- **Settings** — toggle WiFi/BLE capture, adjust WiFi channel dwell time, set the ghost-clear
  refresh interval, reveal the log's AES key, wipe the log.
- **Lore** — flavor text unlocked progressively with lifetime captures, mixed with a few real
  running stats.
- **Export/Maintenance** — how to pull the log off the SD card and decrypt it, plus the current
  session's record count.
- **Recent Devices** — a live, RAM-only feed of the last 16 WiFi/BLE observations (type, MAC,
  RSSI, SSID/name, time since seen), newest first. This is separate from both `SignalCatalog`
  (which deliberately never retains which specific MACs it has seen — only dedup counts) and the
  encrypted log (which retains everything, but only ever encrypted at rest). Nothing shown here
  is persisted; it's lost on reboot along with the rest of RAM.
- **Log Viewer** — browses the encrypted capture log *on the device itself*, no PC required. See
  [On-device log decryption](#on-device-log-decryption) below for how that's possible without any
  key-entry UI. `Left`/`Right` switch between daily log files, `Up`/`Down` page through records
  within a file (8 at a time, newest first).

## Hardware

Same target as upstream CrossPlant/CrossInk: **Xteink X3 or X4**, ESP32-C3 (single-core RISC-V,
~380 KB usable RAM, no PSRAM), 800×480 e-ink panel. The dashboard runs in **portrait**
(480×800 logical) — that's the orientation the physical Back/Confirm/Left/Right/Up/Down buttons
are laid out for. Upstream CrossPlant handles rotated orientations with an orientation-aware
button remapping layer in `MappedInputManager`; this port deliberately doesn't carry that
complexity, so it stays in the one orientation where on-screen button hints are correct without
it.

`MappedInputManager` does carry one small physical correction: on real X3/X4 hardware, the
board's `HalGPIO::BTN_LEFT`/`BTN_RIGHT` constants are swapped relative to which physical button is
actually left vs. right on the case (confirmed by flashing a device — Back/Confirm, the outer two
of the four front buttons, landed correctly, but the two middle buttons opened the screen labeled
for the other one). `MappedInputManager::hardwareIndex()` swaps them back so every screen's
Left/Right bindings (Dashboard's Lore/Export, Settings' -/+, Lore's Prev/Next, Log Viewer's file
switching) match their on-screen labels.

## Look and feel

The UI font is [Space Mono](https://github.com/googlefonts/spacemono) (SIL OFL 1.1) — a monospace
face chosen to read as "instrument readout" rather than "app UI," which fits a device whose whole
job is displaying raw RF telemetry (see `lib/EpdFont/builtinFonts/all.h`). Boxed elements (the
footer's button tabs, the battery badge, the specimen card, Settings' selected row) use a shared
rounded-outline style (`Chrome::kCardRadius`) instead of solid filled highlights — outlines read as
"buttons" without being the heaviest, most ghost-prone thing on the screen after a `FAST_REFRESH`.

The ESP32-C3 has integrated WiFi 802.11 b/g/n and Bluetooth 5 (LE only) — both capture paths run
on the same radio hardware the original firmware used for book downloads and OTA updates.

## Building

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

## Exporting the log

There is no USB/WiFi transfer protocol — power the device off, pull the SD card, and copy the
files under `/.ruby/log/*.pclog` to a computer, then decrypt with the command above and
the key from **Settings → Reveal log key**.

## What "encrypted" means here

The log is AES-256-GCM encrypted at rest so that a lost or stolen SD card doesn't hand over a
plaintext capture history. It is **not** anonymized — record contents are the same MAC
addresses, SSIDs, and RSSI values a passive analyzer would see over the air, deliberately, so the
log is actually useful for its purpose. Treat it and the device itself the same way you'd treat
any other RF capture tool: know the rules that apply where you use it, and don't point it at
networks or people without a reason you'd stand behind.

## What was removed, and why

Book-reading, remote sync, and WiFi-client features made no sense for a device whose entire
value proposition is that it never associates with a network:

- **Epub/Txt/Xtc readers, file browser, OPDS, KOReader sync** — no reading surface exists.
- **WiFi station mode / "join network" / web server / WebDAV / OTA-over-HTTP** — dropped
  entirely. The device's WiFi radio only ever runs in monitor mode; it never has an IP address.
  Firmware updates are USB-only (`pio run -t upload`), and log export is SD-card-only.
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
