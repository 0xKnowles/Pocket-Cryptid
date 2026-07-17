# Ruby — Agent Guide

Canonical repo instruction file for AI coding agents working in this repo.

Project: passive WiFi/BLE RF signal analyzer firmware for the Xteink X3/X4 (ESP32-C3), with a
procedurally-rendered creature whose expression reacts to captured devices — Pwnagotchi-style
mood faces, not a pet that levels up. Built on the CrossPlant/CrossInk hardware foundation — see
README.md for the full layer breakdown.

## Core Rules

- Role: Senior Embedded Systems Engineer for ESP-IDF / Arduino-ESP32 work.
- The ESP32-C3 has no PSRAM and about 380 KB usable RAM. Stability beats features.
- Do not assume ESP-IDF or SDK API availability. Verify in `freeink-sdk/` or the live code.
- This device is receive-only by design: `WifiSniffer` and `BleScanner` must never call anything
  that transmits, associates, pairs, or advertises (no `esp_wifi_connect`, no active BLE scan, no
  AP mode, no GATT server). If a change would make the radio transmit, stop and flag it — that's
  a change to the product's core promise, not an implementation detail.
- Do not claim performance or memory wins without explaining the mechanism.
- Justify new heap allocations or explain why stack/static storage is not suitable.
- After proposing or making a fix, say how to verify it — this repo has no simulator, so
  "verify" usually means `pio run -e default` compiling, plus a description of what to check on
  real hardware.

## Hardware Constraints

- MCU: ESP32-C3, single-core RISC-V at 160 MHz, integrated WiFi + BLE 5 (LE only).
- Display: 800×480 e-ink, single framebuffer (`800 * 480 / 8 = 48000` bytes). Default orientation
  is `LandscapeCounterClockwise` (native panel orientation) — this is a dashboard, not a book
  page, so don't assume the portrait layout conventions from upstream CrossPlant.
- Storage is SD via SdFat. Only one reader can hold a file open at a time.

## RF Capture Rules

- `WifiSniffer` runs in ESP-IDF's promiscuous/monitor mode (`esp_wifi_set_promiscuous`), not
  Arduino `WiFi.h` station mode. The two are not meant to run concurrently on this device.
- The promiscuous RX callback (`WifiSniffer::promiscuousRxCallback`) runs in the WiFi driver's
  own task context — keep it allocation-free and non-blocking, and hand off via the internal
  queue (`rxQueue`) instead of doing real work there. Same rule for any BLE scan callback.
- `BleScanner` must stay in passive mode (`setActiveScan(false)`). Don't "optimize" this to
  active scanning without discussing it — it's a stealth/behavior decision, not a knob.
- 802.11/EAPOL frame parsing in `WifiSniffer.cpp` reads only header fields and IE tags that are
  broadcast in the clear (SSID, addresses, EAPOL key-info flags). Do not add code that touches
  encrypted payloads or attempts key derivation/cracking — that is out of scope for this project.

## Encrypted Log Rules

- `EncryptedLog` uses AES-256-GCM (mbedtls). **Never reuse a nonce under the same key** — the
  nonce-block reservation scheme in `EncryptedLog::reserveNonceBlock()` exists specifically to
  guarantee this across reboots; don't bypass it or shrink `kNonceBlockSize` without re-checking
  the uniqueness argument in the comment above it.
- The AES key is derived from a random seed (NVS, internal flash, not the SD card) + the chip's
  eFuse MAC. There is no user passphrase/keyboard UI by design — don't add one without checking
  with the user first, since it changes the recovery story (`revealDecryptionKeyHex()`).
- Keep `lib/RubyLog/LogRecord.h` and `scripts/decrypt_log.py` in sync. If the on-disk record
  layout changes, bump `kLogFormatVersion` first and update both sides together.

## Resource Rules

1. Keep local stack usage small. Anything meaningfully larger than 256 bytes should be justified.
2. Avoid repeated heap churn in loops. Allocate once in `onEnter()`, reuse, free in `onExit()`.
3. Large constant tables should be `static const`/`constexpr` so they live in flash, not DRAM.
4. Avoid `std::string` and Arduino `String` in hot paths (the RF capture callbacks especially).
   Prefer `string_view`, `char[]`, and `snprintf`.
5. Prefer `constexpr` for compile-time constants.
6. `new` is not nothrow on ESP32 with exceptions disabled — use `new (std::nothrow)` or
   `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h` for fallible allocations.

## HAL And Platform Rules

- Use HAL classes, not SDK classes, in app code (`HalStorage`/`Storage`, `HalGPIO`/`gpio`,
  `HalDisplay`/`display`, etc.) — same convention as upstream CrossPlant.
- File I/O uses `HalFile`, not Arduino `File`. Always close files explicitly.
- Use `MappedInputManager::Button::*` for button logic — it's a thin fixed mapping here (no
  remapping/reader-mode complexity like upstream), but still the right layer to go through.

## C++ / Embedded Gotchas

- No exceptions. No `abort()`. Log before returning failure.
- ISR/driver-task-context handlers (WiFi promiscuous callback) need `IRAM_ATTR` if they become
  hot enough to matter, and must not block or allocate.
- Do not cast unaligned `uint8_t*` data to wider pointer types when parsing 802.11 frames — use
  `memcpy` or the packed struct pattern already in `WifiSniffer.cpp`.

## Build And Verification

- PlatformIO is the source of truth. Personal overrides belong in `platformio.local.ini`.
- `pio run -e default` — firmware compile validation. There is currently only one env.
- `pio check -e default --fail-on-defect low --fail-on-defect medium --fail-on-defect high` for
  static analysis.
- `find src lib -name "*.cpp" -o -name "*.h" | xargs clang-format -i` for formatting touched
  C++ files.
- This port has not been hardware- or even compiler-verified in the environment it was written
  in (no PlatformIO toolchain available there). Treat `lib/RfCapture` and the NimBLE-Arduino
  integration as the highest-risk areas to check first against the actual resolved library
  version.

## Git Workflow

- Check `git status --short` before edits and before reporting results.
- Do not commit unless the user explicitly asks.
- Before staging, ensure `.pio/`, `compile_commands.json`, and `platformio.local.ini` are not
  included.
- Branch/commit message conventions: `<type>: <short summary>` (`feat`, `fix`, `docs`, `refactor`,
  `test`, `chore`, `perf`).

## Changelog

Add an entry to `CHANGELOG.md` for user-facing changes, grouped by Added/Changed/Deprecated/
Removed/Fixed/Security, newest version first.
