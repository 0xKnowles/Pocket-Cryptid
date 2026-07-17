# Changelog

All notable changes to this project are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- GitHub Actions CI (`.github/workflows/ci.yml`): builds the `default` PlatformIO environment
  and runs `pio check` static analysis on every push/PR.

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
