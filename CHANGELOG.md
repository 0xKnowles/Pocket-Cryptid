# Changelog

All notable changes to this project are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Fixed

- **Sleep crash, take 4 — found it (pending confirmation)**: the added heap diagnostics gave a
  concrete number — `free=2692` bytes of *general* heap right after capture starts, dropping to
  ~1400-1500 with the DMA-capable pool almost fully fragmented (`dmaLargest=0`), moments before
  the `esp-aes` allocation failure and abort. That's not a small dedicated pool running out — the
  whole application was down to a few KB of free RAM. The user confirmed by bisecting to a known-
  good build (before the responsiveness pass) that this is a real regression, and with
  `EncryptedLog`'s persistent-handle change already reverted without fixing it, `SignalCatalog`'s
  open-addressing hash index is the remaining suspect from that same change: ~10 KB of *static*
  RAM added permanently to a device already apparently running with only single-digit KB of
  margin. Reverted `HashRing` back to the plain O(Capacity) linear scan it was before — same
  dedup behavior, no extra RAM. CPU cost was the acceptable trade here, not memory.
- **Sleep crash, take 3**: fresh serial logs from real hardware ruled out the previous theory —
  this run had no `GFX !! Outside range` flood at all, just `esp-aes: Failed to allocate memory`
  on a `GCM encrypt` call, seconds after a *fresh boot*, immediately followed by `abort()`. This
  points to the hardware AES engine's small DMA-capable memory pool (distinct from, and much
  smaller than, general heap) being exhausted almost immediately at startup — not something tied
  to sleep specifically, just where it happens to get triggered. Added temporary diagnostics
  (`ESP.getFreeHeap()` alongside `heap_caps_get_free_size`/`heap_caps_get_largest_free_block` for
  `MALLOC_CAP_DMA`) right after capture starts and at the exact GCM failure point in
  `EncryptedLog::writeEnvelope()`, since "plenty of free heap overall" and "DMA pool exhausted"
  look identical from `ESP.getFreeHeap()` alone. Needed before attempting a real fix (most likely
  disabling hardware AES acceleration in favor of software AES, or reducing what else competes for
  that pool) rather than guessing again.
- **Log Viewer showed "Could not decrypt this page."** Regression from keeping `EncryptedLog`'s
  file handle open across writes (see the responsiveness entry below): `LogViewerActivity` opens
  a *separate* read handle to the same file to decrypt it for viewing, and that concurrent-handle
  interaction was producing stale/incomplete reads while the write handle was still open. Reverted
  to open/write/close per record — correctness of reading back what was captured matters more than
  the filesystem-overhead savings, and a proper fix (routing LogViewer's reads through the same
  open handle) isn't worth the complexity right now. `currentFileSizeBytes()` also reverted to a
  fresh open() per call rather than reading the (now-gone) persistent handle's size.
- **Device crash-loops when entering sleep**: serial logs showed a flood of `GFX !! Outside range`
  errors (a draw call computing wildly out-of-bounds coordinates — traced to *not* be the sleep
  portrait, procedural silhouette, or any rounded-rect/corner drawing, all of which are
  mathematically bounded well within the screen; still unconfirmed which call it actually is)
  immediately followed by `abort()` and a reboot into the crash screen. Each `LOG_ERR` for a
  rejected pixel is a blocking call over ~115200 baud USB CDC serial; enough of them back-to-back
  can stall the task long enough to starve the idle task and trip the watchdog — turning a
  cosmetic bug into a hard crash. `GfxRenderer::drawPixel` now caps that log to the first 5
  occurrences instead of logging unboundedly. Also added temporary `LOG_INF` checkpoints through
  `SleepActivity::onEnter()` and the deep-sleep entry sequence
  (`enterDeepSleep()`/`HalPowerManager::startDeepSleep()`), and moved the HWCDC serial teardown in
  `startDeepSleep()` as late as possible (right before `esp_deep_sleep_start()` instead of at the
  top of the function) so a failure anywhere in the GPIO-isolation/wakeup-arming sequence still has
  a chance to log before serial dies — needed to actually pin down the exact call site if the log
  cap alone doesn't resolve it. All temporary — will be removed once confirmed fixed.
- CI build failure: `SignalCatalog::nextPowerOfTwo` was a `SignalCatalog` member used inside the
  nested `HashRing` template's own static member initializer — GCC rejects that ("called in a
  constant expression before its definition is complete") even though it's lexically defined
  earlier in the same class body, because the enclosing class isn't yet a "complete-class context"
  at that point. Moved it to a free function ahead of the class instead.

### Changed

- Sleep screen portrait bumped from 280px to 400px to match the new, larger `sleep.bmp` (400×400)
  at native resolution — RubySpriteRenderer only ever scales art down, never up, so 400px is the
  largest size that stays pixel-crisp instead of padding a smaller image.
- Dashboard's "Mood" label nudged 2px closer to the specimen box.
- **Responsiveness: encrypted log writes no longer open/write/close the SD file per record.**
  `EncryptedLog` previously did a full file open (a directory scan on FAT — the expensive part),
  five `write()` calls, and a close for *every single* WiFi frame or BLE advertisement, called
  synchronously from the sniffer/scanner callbacks that fire on the same cooperative loop that
  reads button input. It now keeps the file open across a whole day's writes (`logFile`,
  `EncryptedLog::openTodaysFile()`) and calls `sync()` after each record instead of a full
  close+reopen — same per-record durability, far less filesystem overhead in a busy RF
  environment. `currentFileSizeBytes()` (polled every dashboard redraw) now reads the already-open
  handle's size instead of opening a second handle just to check it.
- **Responsiveness: SignalCatalog's dedup rings are no longer a linear scan.** `contains()`/
  `insert()` on the fixed-capacity WiFi AP/client/BLE rings (up to 768 slots each) scanned every
  occupied slot on every single observed packet. Added an open-addressing hash index (linear
  probing with tombstones, ~10 KB total extra RAM across the three rings) alongside the existing
  ring buffer, turning both into an O(1) average lookup — the ring's insertion-order/oldest-slot
  eviction behavior is unchanged, this only speeds up "have I seen this hash before."
- Dashboard's headline under the specimen box was the auto-generated per-device "SPECIMEN-XXXX"
  designation (`RubyManager::begin()`, derived from the MAC address) — meant as flavor, but with
  no explanation on-screen it just read as unexplained noise. Replaced with a plain "Mood" label
  over the actual mood/expression value; the designation itself is unchanged and still used in log
  messages, just no longer given top billing on the dashboard.

### Added

- **Full-screen boot splash** (`boot.bmp`, baked in the same way as the expression art) replaces
  the old small-portrait boot screen, with a "RUBY vX.Y.Z" title band overlaid on top (white text
  on a black band, so it stays legible regardless of what's under it) — `RUBY_BASE_VERSION`, a new
  build macro alongside `RUBY_VERSION`, supplies the plain version number without the `-dev+branch`
  suffix. `sleep.bmp` is no longer reused for the boot splash; it's sleep-screen-only now.
- **Sleep screen refreshes itself every 15 minutes** while parked, to clear e-ink ghosting instead
  of leaving the same static frame up indefinitely (`HalPowerManager::startDeepSleep`,
  `SLEEP_SCREEN_REFRESH_INTERVAL_US`). **USB power only** — the board's battery-latch circuit cuts
  the MCU's power entirely between sleeps on battery (see the existing GPIO13 comment in that
  file), which also kills the RTC timer domain this relies on, so on battery only the physical
  power button can wake the device, same as before. When the timer does fire, `main.cpp` detects
  it via `esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER` and does a silent redraw-and-
  resleep — no capture restart, no dashboard, doesn't count as a boot.
- **Bitmap art for Ruby, baked into the firmware** (`RubySpriteRenderer::draw()`,
  `RubyEmbeddedArt.h`): each expression's art (`bmp/*.bmp`) is re-encoded at build-prep time
  (`scripts/generate_embedded_art.py`, needs `Pillow`) into an 8bpp-grayscale C header and
  compiled directly into the firmware image, so it's there from a fresh flash with no SD-card
  setup — no more "not found on SD card" if a user never provisioned one. An SD card at
  `/bmp/<expression>.bmp` (see `bmp/README.md`) is still checked first and overrides the built-in
  art when present, for anyone who wants to swap in their own without recompiling. Falls back
  further still to the procedural silhouette only if a bitmap somehow fails to parse.
- **Dashboard "RECENT DEVICES" window**: a live preview of `RecentSightings` (type, MAC, RSSI,
  time-ago) embedded directly on the main screen beside Ruby, so seeing what's actually been found
  doesn't require leaving the dashboard. The full 16-entry feed is still available via `Up` (now
  titled "DEVICE LOG" to distinguish it from the new inline preview).
- Log Viewer records now show the WiFi channel / BLE address-kind (`extra`) and the EAPOL message
  number for handshake records, in addition to what was already shown.

### Changed

- **Dashboard relayout**: the specimen box moved from centered-at-top to pinned top-left and grew
  from 170×170 to 230×230 (roughly a quarter of the screen), the "RECENT DEVICES" window sits
  beside it to the right at the same height instead of as a third full-width card below, and
  SIGNALS + CAPTURE STATUS now fill the bottom half full-width with the extra room that frees up.
  `Chrome`'s stat-card helpers (`beginStatCard`/`endStatCard`, local to `DashboardActivity.cpp`)
  now take an explicit x/width instead of always spanning the full content width, so the same card
  chrome works for both the full-width bottom cards and the narrower top-right column.
- **Sleep screen made much bigger**: portrait 96px → 280px, most text bumped from `FONT_SMALL_ID`
  to `FONT_UI_10_ID`/`FONT_UI_12_ID` — it sits untouched for potentially hours, so it should read
  from across a room, not just up close.
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

### Fixed

- **Footer button-hint labels didn't match the button that actually fired**, root-caused from a
  precise button-by-button report on real X3 hardware: the on-screen labels read
  "Refresh - Lore - Export - Settings" left to right, but pressing those same four physical
  positions actually opened "Refresh - Settings - Lore - Export" — a 3-way rotation, not the
  simple swap suspected earlier. `Chrome::drawFooterHints()` built its 4-slot label array as
  `{back, left, right, confirm}`, i.e. assuming the physical left-to-right button order is
  Back-Left-Right-Confirm; it's actually Back-**Confirm**-**Left**-**Right**. Every screen's
  button-to-label wiring (`Confirm`→Settings, `Left`→Lore, `Right`→Export, etc.) was already
  correct — only the visual label position was wrong, confirming the earlier
  `MappedInputManager::hardwareIndex()` revert was the right call and this was a separate bug.
- **Dashboard text overlapped the bottom footer buttons** after the last round of changes added
  the "RECENT DEVICES" card without trimming enough elsewhere. Tightened the vertical budget:
  specimen box 200→170px, card gaps 14→10px, spacing under the name/mood/lore lines trimmed, the
  "RECENT DEVICES" preview 4→3 entries, and the standalone "Up: full device list / Down: decrypt
  log" hint line folded into that card's title instead of getting its own line.
- **Uploaded `.bmp` art wasn't displaying**, root-caused via the serial log added below:
  `/bmp/curious.bmp not found on SD card` — the art had been added to the *repo's* `bmp/` folder,
  not copied onto the device's actual SD card, and the firmware had no other source for it. Rather
  than just documenting the SD-card step more clearly, the art is now baked into the firmware
  image directly (see "Added" above) so this class of mismatch — repo assets vs. what's physically
  on the card — can't happen for the built-in expressions at all; the SD-card path still exists,
  now purely as an optional override. Also added `LOG_ERR`/`LOG_INF` calls at every point
  `RubySpriteRenderer`'s SD/embedded loaders can bail (open failed, specific `BmpReaderError`, bad
  dimensions) so a real parse failure would be equally easy to diagnose from serial output, and
  turned on Atkinson dithering for bitmap art (was off by default), improving how
  photographic/gradient art looks on the 4-level grayscale panel.

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
