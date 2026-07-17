# Pocket Cryptid

**Pocket Cryptid** is open-source custom firmware for the **Xteink X3 and X4** e-ink readers
that turns the hardware into something else entirely: a stealthy, long-battery-life passive RF
signal analyzer with a shadowy digital creature living in the corner of the screen.

It is not a book reader. It does not read EPUBs, sync with KOReader, or talk to OPDS catalogs.
It listens — passively, receive-only — to the WiFi and Bluetooth Low Energy traffic already in
the air around it, catalogs what it hears into an encrypted on-device log, and grows a small
analog-horror creature as a visual front-end for that catalog.

## Where this comes from

Pocket Cryptid is built on [CrossPlant](https://github.com/0xKnowles/CrossPlant)'s hardware
foundation — the same e-ink display driver, input handling, power management, SD storage, font
rendering, and activity/screen framework that make CrossPlant (and its own ancestor, CrossInk)
boot and draw on real Xteink hardware. Everything above that foundation — the RF capture layer,
the encrypted log, the creature, every screen — is new.

| Layer | What it is |
| --- | --- |
| `freeink-sdk/`, `lib/hal`, `lib/GfxRenderer`, `lib/EpdFont` | Hardware bring-up: display driver, buttons, power, SD card, fonts, graphics primitives. Carried over from CrossPlant/CrossInk largely unmodified — this is what makes the firmware boot on real hardware. |
| `lib/RfCapture` | **New.** Passive 802.11 monitor-mode WiFi sniffing and passive BLE advertisement scanning. |
| `lib/SignalCatalog` | **New.** Deduplicates observations into "have I seen this MAC before" and lifetime unique-device counters. |
| `lib/CryptidLog` | **New.** AES-256-GCM encrypted append-only capture log. |
| `src/cryptid` | **New.** The creature: procedurally-rendered (no bitmap art pipeline), fed by `SignalCatalog`. |
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
   lifetime counter and feeds the creature XP.
3. **Log.** Independently of deduplication, *every* observation is encrypted (AES-256-GCM via
   mbedtls, one record per packet) and appended to a daily log file on the SD card. The AES key
   is derived from a random seed generated on first boot plus this chip's eFuse MAC address and
   lives only in internal NVS flash — pulling the SD card gets you ciphertext with no key beside
   it. Note this means every sighting is logged, not just first-ever ones — a busy RF
   environment (a handful of phones re-advertising over BLE every few hundred milliseconds, a
   dozen APs beaconing every ~100ms) can write a meaningful volume of records per hour. That's
   intentional for a capture tool, but budget SD card space and export/rotate accordingly. See
   [Exporting the log](#exporting-the-log).
4. **Grow.** The creature (`CryptidManager`) advances through five stages
   (`DORMANT → LARVA → WRAITH → STALKER → APEX`) as lifetime XP accumulates, and its mood swings
   from `THRIVING` to `STARVING` based on how recently it was last fed a new unique device — not
   on any clock-based decay. It has no needs to fail at meeting; it just gets quieter when the RF
   environment goes quiet.
5. **Display.** The dashboard is mostly static: a header, a column of RF stats, a footer. The
   creature lives in a fixed corner box that redraws on its own ~1.2s cadence using
   `HalDisplay::FAST_REFRESH` without ever touching (or `clearScreen()`-ing) any pixel outside
   that box — see `CryptidSpriteRenderer.h` and `DashboardActivity.h` for the full explanation of
   why that reads as a genuine partial refresh on hardware that has no windowed-update API.

## Screens

- **Dashboard** (home) — unique AP/client/BLE/handshake counts, capture status, log size,
  session uptime, and the creature. `Confirm` → Settings, `Left` → Lore, `Right` → Export/
  Maintenance, `Back` → force a full ghost-clearing refresh.
- **Settings** — toggle WiFi/BLE capture, adjust WiFi channel dwell time, set the ghost-clear
  refresh interval, reveal the log's AES key, wipe the log.
- **Lore** — flavor text unlocked progressively with XP, mixed with a few real running stats.
- **Export/Maintenance** — how to pull the log off the SD card and decrypt it, plus the current
  session's record count.

## Hardware

Same target as upstream CrossPlant/CrossInk: **Xteink X3 or X4**, ESP32-C3 (single-core RISC-V,
~380 KB usable RAM, no PSRAM), 800×480 e-ink panel. The dashboard runs in **portrait**
(480×800 logical) — that's the orientation the physical Back/Confirm/Left/Right/Up/Down buttons
are laid out for. Upstream CrossPlant handles rotated orientations with an orientation-aware
button remapping layer in `MappedInputManager`; this port deliberately doesn't carry that
complexity, so it stays in the one orientation where on-screen button hints are correct without
it.

The ESP32-C3 has integrated WiFi 802.11 b/g/n and Bluetooth 5 (LE only) — both capture paths run
on the same radio hardware the original firmware used for book downloads and OTA updates.

## Building

```sh
pio run -e default          # compile
pio run -e default -t upload  # flash over USB
```

There is a single PlatformIO environment (`default`). Unlike upstream CrossPlant's `tiny`/
`xlarge` split — which existed to omit different reading-font sizes depending on flash budget —
Pocket Cryptid ships one fixed, small UI font set, so there's no equivalent tradeoff to build
variants around. The same binary supports both X3 and X4: hardware is detected at runtime
(`HalGPIO::deviceIsX3()`), not at build time.

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) builds this env and runs `pio check`
static analysis on every push/PR.

This firmware has **not been hardware-tested** as part of this port — it was written against the
same APIs and patterns the upstream HAL/graphics/persistence code already uses, but the RF
capture layer (`lib/RfCapture`) and the NimBLE-Arduino integration in particular should be
verified against real hardware and the exact `NimBLE-Arduino` version PlatformIO resolves before
relying on it. `pio run` (compile only, no device needed) is the first thing to check.

## Exporting the log

There is no USB/WiFi transfer protocol — power the device off, pull the SD card, and copy the
files under `/.pocketcryptid/log/*.pclog` to a computer. Then, with the key from
**Settings → Reveal log key**:

```sh
pip install -r scripts/requirements.txt
python3 scripts/decrypt_log.py --key <64 hex chars> 20260717.pclog
```

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
