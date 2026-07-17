# Changelog

All notable changes to this project are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- **SD-card bitmap art for Ruby** (`RubySpriteRenderer::draw()`): if `/bmp/<expression>.bmp`
  exists on the SD card (see `bmp/README.md` for the full list and how to deploy it), it's drawn
  scaled-to-fit and centered instead of the procedural silhouette; missing files fall back to the
  procedural rendering per-expression, so partial art works fine.
- **Dashboard "RECENT DEVICES" card**: a live 4-entry preview of `RecentSightings` (type, MAC,
  time-ago) embedded directly on the main screen next to Ruby, so seeing what's actually been
  found doesn't require leaving the dashboard. The full 16-entry feed is still available via
  `Up` (now titled "DEVICE LOG" to distinguish it from the new inline preview).
- Log Viewer records now show the WiFi channel / BLE address-kind (`extra`) and the EAPOL message
  number for handshake records, in addition to what was already shown.

### Changed

- Log Viewer rewritten from two packed, abbreviated lines per record to a bordered three-line
  card per record (type+MAC, time/RSSI/channel, label), and the page size dropped from 8 to 5
  records so each has room to breathe — both directly in response to "hard to read."
- Dashboard's "CAPTURE STATUS" card dropped raw WiFi-frame/BLE-advertisement counters and the
  session/boot counter to make room for the new "RECENT DEVICES" card; actually seeing what's
  been found is more useful at a glance than a raw packet tally, and the boot counter remains in
  `/.ruby/app_state.json` even though it's no longer shown on-screen.
- **Renamed the project from Pocket Cryptid to Ruby** — README/CHANGELOG/AGENTS.md, the
  on-screen title, the boot splash, the panic-report string, every `Cryptid*` class and macro
  (`CryptidManager` → `RubyManager`, `CRYPTID` → `RUBY`, `CryptidSettings` →
  `RubySettings`, etc.), `src/cryptid/` → `src/ruby/`, `lib/CryptidLog/` →
  `lib/RubyLog/`, and the `POCKET_CRYPTID_VERSION` build macro → `RUBY_VERSION`.
  **This changes on-disk paths and NVS namespaces** (`/.pocketcryptid/` → `/.ruby/`, the
  `EncryptedLog` NVS namespace and AES key domain tag both changed) — existing settings, stats,
  and previously-captured `.pclog` files from before this update will not carry forward; the
  device effectively re-initializes as if freshly flashed, and old logs become permanently
  undecryptable (same category of change as the existing "wipe log" action).
- **Reworked the creature's core mechanic**: instead of eating XP to evolve through five permanent
  stages, it's now a single fixed shape whose *expression* reacts to recent findings — closer to
  Pwnagotchi's mood faces. New `RubyExpression` states: `EXCITED` (handshake captured in the
  last ~30s, the biggest find), `CURIOUS` (any new unique device in the last ~90s), then the
  familiar drought-based `CONTENT` → `BORED` → `LONELY` → `SLEEPING`. Each expression now also
  drives a distinct eye + mouth combination (`RubySpriteRenderer::drawFace()`), not just eye
  state. `CryptidStage`/XP/the evolution banner are gone entirely; the old "IT HAS CHANGED: X"
  banner is now a "HANDSHAKE CAPTURED" flash instead. Lore entries still unlock progressively, now
  paced by lifetime capture count rather than XP (`RubyConfig::kCapturesPerLoreUnlock`).

### Added

- `bmp/` — empty directory for manually-added bitmap image assets (not yet wired into the
  firmware; `RubySpriteRenderer` still draws Ruby procedurally, see `src/ruby`).
- GitHub Actions CI (`.github/workflows/ci.yml`): builds the `default` PlatformIO environment
  and runs `pio check` static analysis on every push/PR.
- Verified on real X4 hardware for the first time.
- **Recent Devices** screen (Dashboard → `Up`): a live, RAM-only feed of the last 16 WiFi/BLE
  observations — type, MAC, RSSI, SSID/name, time since seen — so the device shows what it's
  actually hearing, not just aggregate counts (`lib/SignalCatalog/RecentSightings`,
  `src/activities/devices`).
- **Log Viewer** screen (Dashboard → `Down`): browses and decrypts the encrypted capture log
  on-device, no PC or key entry required — the AES key is already resident in RAM from boot.
  `Left`/`Right` switch between daily log files, `Up`/`Down` page through records 8 at a time.
  Backed by two new `EncryptedLog` methods, `decryptRecordRange()` and
  `recordCountForFileSize()`, which exploit the log's fixed 70-byte-per-record envelope size for
  O(1) random-access seeking instead of scanning from the start of the file
  (`src/activities/logviewer`).

### Changed

- UI font switched from Inter to [Space Mono](https://github.com/googlefonts/spacemono) (SIL OFL
  1.1) — a monospace "instrument readout" look in place of a general-purpose UI sans, generated at
  8/10/12pt via the same `fontconvert.py` pipeline upstream CrossPlant uses
  (`lib/EpdFont/builtinFonts`).
- Reworked chrome across the dashboard and Settings around a shared rounded-outline "card/button"
  style (`Chrome::kCardRadius`) instead of plain dividers and solid inverted-fill highlights:
  the footer is now four separated pill-shaped tabs instead of a flat row of labels, the header's
  battery readout is a pill badge, the specimen creature box has a rounded frame
  (`RubySpriteRenderer::draw()`), the two dashboard stat groups are boxed into "SIGNALS" /
  "CAPTURE STATUS" cards, and the Settings selected-row highlight is an outlined box instead of a
  solid black fill.

### Fixed

- Dashboard now runs in portrait, not landscape. Landscape put the on-screen footer button hints
  out of alignment with the physical buttons, which are wired for portrait; fixing it properly
  would mean carrying the orientation-aware button remapping layer upstream drops for reader
  page-turning, which this port intentionally doesn't have. Reworked the dashboard layout for a
  tall screen in the process: the creature is now a centered "specimen card" at the top with its
  name/stage/mood/XP bar beneath it, RF stats fill the rest of the screen full-width below that.
- Two remaining `cppcheck` `constParameterReference` findings in `Chrome::drawHeader` and
  `Chrome::drawFooterHints` (both now take `const GfxRenderer&`), the last thing blocking a fully
  green CI run.
- Footer button-hint text could clip at the bottom of the screen: the old 22px-tall footer band
  left too little room below the text baseline for a full line's descenders. The new pill-tab
  footer is 40px tall with the label vertically centered inside each pill.
- **Power button behavior was inverted**: a quick tap put the device to sleep and only a long
  hold forced a screen refresh — backwards from the "hold power to turn off" convention users
  expect, and the direct cause of "long-holding power doesn't sleep." Swapped in `main.cpp`: hold
  now sleeps, a tap forces a refresh.
- **Reverted the Left/Right button swap** from the previous entry below. It was based on one X4
  test where the two middle front buttons seemed to trigger each other's screen, but it
  contradicts the ADC-ladder calibration table in `freeink-sdk/InputManager.cpp` (real recorded
  voltages from pressing BACK/CONF/LEFT/RIGHT on physical Xteink hardware — the actual ground
  truth this device's button reading is built on), and a second, broader "buttons don't do what
  they say" report on an X3 unit suggests that swap fixed the wrong thing.
  `MappedInputManager::hardwareIndex()` is back to a direct passthrough; still investigating the
  real cause with more precise button-by-button reports.

## [0.1.0] - 2026-07-17

Initial build of Pocket Cryptid, built on the CrossPlant/CrossInk hardware foundation
(`freeink-sdk/`, `lib/hal`, `lib/GfxRenderer`, `lib/EpdFont`) with an entirely new application
layer.

### Added

- Passive 802.11 monitor-mode WiFi capture (`lib/RfCapture/WifiSniffer`): beacons, probe
  requests/responses, and best-effort WPA 4-way handshake message detection.
- Passive BLE advertisement scanning (`lib/RfCapture/BleScanner`), never transmits a SCAN_REQ.
- Unique-device deduplication and lifetime stat counters (`lib/SignalCatalog`).
- AES-256-GCM encrypted, device-bound, append-only capture log (`lib/CryptidLog/EncryptedLog`)
  with a companion decrypt tool (`scripts/decrypt_log.py`).
- The cryptid: a procedurally-rendered (no bitmap assets) shadowy creature that evolves through
  five stages driven by capture XP, with a mood tied to how recently it was last fed a new unique
  device (`src/cryptid`).
- Dashboard, Settings, Lore, and Maintenance/Export screens (`src/activities`).
- "Strict partial refresh" pet corner: the creature redraws on its own ~1.2s cadence without
  touching any pixel outside its fixed box, independent of the slower full-dashboard redraw.

### Removed

- Everything reading-related: EPUB/TXT/XTC rendering, file browser, OPDS, KOReader sync, the
  virtual plant pet, WiFi station mode / web server / WebDAV / HTTP OTA, multi-language reading
  fonts and the i18n pipeline, auto-sleep-on-inactivity.

[0.1.0]: https://github.com/0xKnowles/Pocket-Cryptid/releases/tag/v0.1.0
