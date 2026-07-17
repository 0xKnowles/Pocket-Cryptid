# Changelog

All notable changes to this project are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- **On-device network picker for the whitelist/blacklist** (`TargetPickerActivity`) — no more
  hand-editing `/.ruby/targets.txt` on a PC to manage active-deauth targets. `Settings →
  Whitelist` / `Settings → Blacklist` each open a live scan (`ApScanCache`, a new dedup-by-BSSID
  cache fed from `WifiSniffer`'s observation stream — deliberately separate from
  `RecentSightings`, which duplicates an entry every time a beacon repeats and would make a poor
  "pick a network" list), sorted strongest-signal-first like picking a network to connect to.
  Confirm on an entry toggles it in/out of the list a `[+]` marker shows current membership.
  Reached via `pushActivity` (not the flat `replaceActivity` every other screen uses) so Back
  pops straight to Settings instead of the Dashboard.
- **`TargetList` redesigned as two genuinely independent lists**, not one list with a mode
  switch: whitelist non-empty means whitelist-only; whitelist empty and blacklist non-empty
  means attack-all-except-blacklist; both empty (the default) means attack nothing. The old
  single-list-plus-mode design meant switching modes silently reinterpreted whatever was already
  in the list — confusing once there were two dedicated on-device entry points into it.
  `/.ruby/targets.txt`'s format changed to match: entries now live under `[whitelist]`/
  `[blacklist]` section headers instead of a `mode=` line.
- **"Pause" replaces Dashboard's Left button** (`CaptureControl`, session-only, not persisted).
  Same button pauses and resumes — no screen change. Pausing stops `WifiSniffer` and `BleScanner`
  outright, which also halts `DeauthEngine` (driven entirely by the observation stream it stops
  producing) and all SD/`EncryptedLog` write activity for as long as it's paused. Resuming
  respects `RubySettings` rather than force-enabling both radios, so it won't turn on a radio the
  owner had off in Settings before pausing. A "-- PAUSED --" line shows under Ruby's mood while
  active, and the footer hint switches between "Pause"/"Resume".

### Removed

- **The Lore feature, completely** — screen, unlock progression, and all supporting state. It
  was consistently the piece of this build that read as "a pet-sim feature bolted onto a
  recon tool" rather than something that earned its screen real estate, and the owner asked for
  the Left button back for something actually useful (see Pause, above). Deleted
  `LoreActivity.h/.cpp` and its `ActivityManager::goToLore()` routing; stripped
  `RubyManager`'s ~40-entry flavor-text table, `loreEntry()`/`loreEntryCount()`/
  `dynamicLoreEntry()`, and the "N/M lore unlocked" caption from the dashboard; removed
  `RubyState::totalCaptures`/`unlockedLoreCount` and `RubyConfig::kCapturesPerLoreUnlock` (both
  existed only to pace lore unlocks — nothing else read them). Older `ruby_state.json` files with
  those fields still load fine; ArduinoJson just ignores keys the struct no longer has.

### Fixed

- **Low raw-capture yield: real-world testing showed 14 EAPOL M1 messages captured against only
  1 M2 and 1 M3 over ~2 hours, with `hcxpcapngtool` extracting only 1 crackable pair.** Root
  cause: `WifiSniffer::tick()` hopped channels on a fixed `dwellMs` (300ms default) cadence with
  no exception for an in-progress handshake, but a full 4-way exchange completes in tens of
  milliseconds — an AP's M1 arriving just before the hop timer fired meant the client's M2 reply
  landed on a channel Ruby had already left, so the AP just kept retransmitting M1 into a channel
  nobody was listening on, exactly matching the lopsided message counts observed. Fixed by
  briefly locking the current channel whenever an EAPOL frame is seen (`kHandshakeChannelLockMs`,
  3s, re-armed on every EAPOL frame so a slow multi-retry handshake keeps the lock alive) before
  resuming the normal hop cycle.

- **Export/Maintenance screen: content ran off the bottom of the screen, and the header read
  badly.** Confirmed by walking the actual render sequence: with raw-capture and active-deauth
  stats both present (a real, shipped combination), the single-column stack of stat rows plus two
  full paragraph blurbs added up to more vertical space than the landscape screen's height,
  running past `Chrome::contentBottom()` and off the visible area entirely — matching "some
  details are off screen." Rebuilt as a 2-column card layout (mirroring `DashboardActivity`'s
  SIGNALS/CAPTURE STATUS pattern): encrypted-log stats + a trimmed export blurb on the left,
  raw-capture and active-deauth cards stacked on the right (each only shown when there's
  something to report), a reserved line at the bottom for the firmware-update note that no card's
  content can grow into, and hard per-line bounds checks in the wrapped-text loops so overflow is
  now structurally impossible rather than just estimated to fit. Header title shortened from
  "EXPORT / MAINTENANCE" to "EXPORT".

- **Real field crash: `WIFI_MODE_STA` (added for active deauth's raw TX) crash-loops X3 hardware
  on every single boot — reverted to `WIFI_MODE_NULL`.** Confirmed via a hardware serial log
  within hours of shipping the active-deauth capability below: the device fades to black and
  restarts within ~700ms of every boot, looping forever. The log shows the actual failure —
  `E intr_alloc: No free interrupt inputs for AES interrupt (flags 0xE)` /
  `E esp-aes: Failed to allocate AES interrupt 261`, immediately followed by `abort()` — and, on
  other boots, the heap-health circuit breaker (see below) firing before that abort even happens,
  with the DMA-capable pool already down to ~2500-4100 bytes free within milliseconds of boot,
  long before any real capture or logging activity. Root cause: bringing the WiFi interface up in
  `WIFI_MODE_STA` (done unconditionally in `WifiSniffer::begin()`, needed so
  `esp_wifi_80211_tx(WIFI_IF_STA, ...)` had an interface to transmit on) starves the ESP32-C3's
  interrupt matrix badly enough — on the X3's already IMU/RTC-heavier interrupt budget — that
  `esp-aes` (the hardware crypto engine `EncryptedLog`'s every write depends on) can't allocate
  its own interrupt at all, and independently pre-fragments/exhausts the same small DMA pool.
  This broke *every* boot, not just when active deauth was actually armed, since the mode change
  ran regardless of whether `DeauthEngine` was enabled. Reverted `WifiSniffer::begin()` to
  `WIFI_MODE_NULL` (the working baseline before active deauth). `esp_wifi_80211_tx()` now fails
  harmlessly (returns an error, doesn't crash) with no STA interface up, so **active deauth is
  currently non-functional** — the toggle, `TargetList`, and targeting logic are all still in
  place and safe, but no deauth frames actually transmit until a way to get TX capability on this
  hardware without breaking crypto is found (most likely a transient, tightly-scoped mode switch
  only for the duration of a burst — untested, needs real hardware iteration).

- **Follow-up field report after the crash-loop fix above: device stable, but noticeably laggy
  with active deauth on, and the same BSSID re-attacked every 20-150ms instead of respecting the
  30-second cooldown.** Two separate bugs, both in `DeauthEngine`, confirmed via the same serial
  log (an `E wifi:invalid interface 0` per failed `esp_wifi_80211_tx()` call, six per burst,
  bursts logged tens of milliseconds apart for the same BSSID):
  - `sendDeauthBurst()` still ran its full transmit attempt — `esp_wifi_set_channel()` plus 6x
    `esp_wifi_80211_tx()` with a blocking `delay(2)` between each — even though every one of those
    calls was guaranteed to fail with the radio back in `WIFI_MODE_NULL` (see above). That's 12ms+
    of pure blocking wasted per burst, run synchronously inside
    `WifiSniffer::tick()`'s main-loop queue drain, for zero benefit — with enough networks in
    range this alone was enough to overflow the raw observation queue and visibly lag the device.
    Gated the actual transmit code behind a `kTxCapable = false` constant (left in place for when
    TX capability is restored) — `sendDeauthBurst()` now just counts the attempt and returns.
  - `trackerFor()`'s eviction policy, once its 16-slot table filled up, always evicted whatever
    was in *slot 0* rather than the actual least-recently-used entry — in a real dense RF
    environment (confirmed: 10+ distinct BSSIDs cycling through in a few seconds, plausibly many
    more), that constantly wiped out cooldown memory for whichever BSSID happened to be sitting
    there, letting it burst again almost immediately instead of waiting out `kCooldownMs`. Fixed
    to evict the entry with the oldest `lastBurstMs` (so untouched/never-fired entries, which
    carry no cooldown state worth protecting, are evicted before anything with a live cooldown),
    and bumped the table from 16 to 64 entries for real headroom.

### Added

- **Active deauth capability** (`DeauthEngine`, `TargetList`) — off by default. A field capture
  session produced a `.pcap` with zero crackable hashes because passive channel-hopping (13
  channels, 300ms dwell) essentially never stays parked on a network long enough to catch a full
  4-way handshake happening on its own. `DeauthEngine` transmits real 802.11 deauthentication
  frames (via `esp_wifi_80211_tx`, the same raw-TX primitive used by community ESP32 deauther
  projects — deauth isn't among the frame types ESP-IDF's own doxygen comment calls "supported"
  for that function, so this is a widely-used technique rather than an officially documented one;
  verify on real hardware before relying on it) at BSSIDs `TargetList` allows, forcing a
  reconnect so the resulting handshake lands in the raw-capture path that already existed.
  - `TargetList` is a whitelist/blacklist gating every target, backed by a human-editable SD file
    (`/.ruby/targets.txt`) — editable directly on a PC with the card out, or from the new Confirm
    action in `DeviceListActivity` (marks the selected AP `[T]`), or the mode toggle in Settings.
    Defaults to **Whitelist with an empty list**, specifically so turning the feature on can never
    itself attack every network in range — the owner has to explicitly add a BSSID first.
  - `RubySettings::activeDeauthEnabled` (off by default) additionally requires
    `rawHandshakeCaptureEnabled` to be on — main.cpp and `SettingsActivity` both refuse to arm
    active mode without it, since forcing a handshake nobody's capturing verbatim would just be
    disruption for nothing.
  - `WifiSniffer::begin()` now brings the radio up in `WIFI_MODE_STA` instead of `WIFI_MODE_NULL`
    (still never calls `esp_wifi_connect()` — an unconnected STA interface, not a joined one) so
    `esp_wifi_80211_tx(WIFI_IF_STA, ...)` has an interface to transmit on. Passive capture behavior
    is unchanged; this only makes TX possible, DeauthEngine's own opt-in still gates whether it
    ever happens.
  - **This is real RF interference against whatever it targets.** Only use it against networks
    you own or are explicitly authorized to test — see the in-app warning in `SettingsActivity`
    and the class comments on `DeauthEngine`/`TargetList`.
  - `MaintenanceActivity` shows burst/frame counters once any have been sent.

### Fixed

- **`WifiSniffer::classifyEapolMessage()` read the Key Data Length field 4 bytes off**, from the
  same overnight capture-session investigation above. The function takes `eapol + 4` (based at
  the Descriptor Type byte) but indexed the Key Data Length field at `[97]`/`[98]` — the offset
  for the *unshifted* EAPOL header, not the already-`+4`-based pointer it was actually given. The
  real field sits at relative offset `93`/`94` from that base. Message 4 (the handshake's final
  ACK) is the only message classified using that field (`keyDataLen == 0`), so this meant M4
  frames were almost never recognized and got silently dropped before ever reaching the raw
  capture queue — a real (if partial) contributor to that night's empty `.pcap`, on top of the
  passive-capture timing problem the active-deauth capability above addresses directly.

### Removed

- Temporary checkpoint/diagnostic logging left over from tonight's crash investigations
  (`enterDeepSleep`, `HalPowerManager::startDeepSleep`, the post-capture-start heap dump) — all
  marked "remove once confirmed/diagnosed" in their own comments, and now superseded by the
  heap-health circuit breaker below, which is a permanent, useful replacement rather than a
  one-off log line.

### Fixed

- **Real overnight crash with raw handshake capture on: heap/DMA-pool fragmentation abort after
  ~1.5 hours, then a crash loop on every subsequent boot.** Confirmed via a field crash report:
  `GCM encrypt failed: -1; heap free=1396 dmaFree=36 dmaLargest=4` — the same failure signature as
  the sleep-crash chased down earlier, but this time caused by raw handshake capture's own SD
  churn stacking on top of the encrypted log's. Three mitigations, none mutually exclusive:
  - **Heap-health circuit breaker** (`src/main.cpp::loop()`): every loop, check
    `heap_caps_get_largest_free_block(MALLOC_CAP_DMA)`; if it drops below a safe floor
    (`HEAP_DMA_LARGEST_BLOCK_MIN`, 4KB — well above the ~4 bytes seen at actual failure),
    proactively trigger the same silent, seamless restart the 12-hour defrag timer already uses,
    *before* the next GCM call gets a chance to abort. Converts an uncontrolled crash into an
    invisible reboot.
  - **Shorter defrag-reboot interval while raw capture is on**: the existing 12-hour periodic
    silent reboot (safety net against long-uptime fragmentation) drops to 1 hour
    (`HEAP_DEFRAG_INTERVAL_MS_RAW_CAPTURE`) specifically when `rawHandshakeCaptureEnabled` is set,
    since raw capture demonstrably compresses the fragmentation timeline well below the original
    12-hour assumption.
  - **Batched pcap writes**: a real handshake bursts 4 EAPOL captures within milliseconds, each
    previously triggering its own `Storage.open()`/write/close cycle — and every `Storage.open()`
    heap-allocates a file-handle object, so that's 4x the churn for the highest-density moment raw
    capture produces. `WifiSniffer::tick()` now drains everything queued into one batch and hands
    it to `PcapWriter::writeFrames()` (replacing the old single-frame `writeFrame()`) as a single
    open/write-many/close cycle.
  - Of the two items deferred when this was first mitigated, one is done (see below); the other
    (software AES) was attempted and reverted — also below, so it isn't silently retried later.

- **Forcing software AES-256-GCM instead of this chip's hardware accelerator — two genuine
  pioarduino toolchain bugs found and confirmed via CI job logs, both dead ends, reverted for
  good.** This would have been the direct fix for the DMA-pool dependency above rather than a
  workaround for it: `mbedtls_gcm_*` defaults to hardware acceleration, which needs a contiguous
  chunk of the small DMA-capable pool per call, while software AES/GCM only needs ordinary heap.
  - **Bug 1**: setting `custom_sdkconfig` at all (even just `CONFIG_MBEDTLS_HARDWARE_AES=n` /
    `CONFIG_MBEDTLS_HARDWARE_GCM=n`) forces this pinned pioarduino release into a full from-source
    rebuild of the entire ESP-IDF managed-component tree, which failed outright on a missing
    generated file (`https_server.crt.S`) from a component this project never uses. Traced that
    file to arduino-esp32's own `idf_component.yml`: it pulls in the full RainMaker cloud-agent
    suite (`esp_rainmaker`, `rmaker_common`, `esp_insights`, `esp_diag_data_store`,
    `esp_diagnostics`, `network_provisioning`) plus Zigbee, Modbus, esp-dsp and esp_modem — none
    referenced anywhere in this codebase (grep-confirmed). Fixed by finding pioarduino's sibling
    `custom_component_remove` mechanism (read directly from `builder/frameworks/component_manager.py`:
    exact-matches full `owner/name` keys against the resolved dependency manifest and deletes them
    *before* the rebuild compiles anything) and stripping those unused components.
  - **Bug 2, underneath the first**: with bug 1 fixed, CI got past `https_server.crt.S` into a
    clean full rebuild — and that rebuild proved the software-AES config compiles and links fine
    on its own. But pioarduino runs `framework = arduino` as *two separate build passes* once
    `custom_sdkconfig` is set: a raw ESP-IDF pass (logged as "Copied compiled esp32c3 IDF libraries
    to Arduino framework") that built and linked cleanly, followed by a second "Arduino compile"
    pass that recompiles the project and relinks against the framework's libraries. That second
    pass's linker pulled `libwpa_supplicant.a` straight from the untouched, prebuilt
    `framework-arduinoespressif32-libs` package, and even freshly-compiled `EncryptedLog.cpp.o`
    couldn't find `mbedtls_gcm_*` — undefined references to `mbedtls_aes_*`/`mbedtls_gcm_*`
    throughout, confirmed via the CI job log. The "copy compiled IDF libraries into the Arduino
    framework" step pioarduino logs before that second pass does not actually wire the
    freshly-built `libmbedcrypto.a` into that pass's link command. That's a structural gap in
    pioarduino's own two-pass build wiring for `framework=arduino`, not a config value — no
    `CONFIG_MBEDTLS_*` flag fixes a library that never gets linked into the final binary.
  - **Verdict**: not fixable from this project without patching pioarduino's builder scripts
    directly, which is out of scope. Reverted both `custom_sdkconfig` and `custom_component_remove`
    from `platformio.ini`, left as a documented dead end with the full diagnosis so it isn't
    silently retried later. The heap-health circuit breaker above remains the real safety net for
    the DMA-pool exhaustion this was meant to prevent at the source.

- **`EncryptedLog` holds a persistent write handle again, with a proper fix this time.** The
  open/write/close-per-record pattern (reverted to earlier tonight after a persistent handle broke
  Log Viewer) was itself a real contributor to the DMA-pool fragmentation above: every
  `Storage.open()` heap-allocates a file-handle object, and that churn adds up over a long capture
  session. The original bug was that a held-open write handle's buffered data wasn't visible yet to
  `LogViewerActivity`'s separate read handle. Fix: call `sync()` after every write instead of
  `close()` — `HalFile::sync()` flushes both the written bytes and the updated file size to the
  card without closing, so a freshly opened reader sees exactly what's been written so far, the
  same guarantee `close()` gave, without paying to reopen on every single record.

### Changed

- **Battery badge moved to the left of the header, title now right-aligned** (the reverse of the
  original layout). Also fixes a real collision this uncovered: the battery badge used to
  right-align to the raw screen edge, which in landscape is exactly where the button-hint
  sidebar's top pill lives — title now right-aligns to the shared content-area edge instead of
  the raw screen edge for the same reason.
- **RECENT DEVICES shows far more than 3-4 entries now.** Same root cause as the sidebar text fix
  below: it was spacing rows with `getLineHeight()` (~25px, paragraph spacing) instead of a size
  meant for compact rows. Replaced with a tight, purpose-sized line height, and — since the box is
  wide enough in landscape to have a lot of unused horizontal space at 1 column — laid entries out
  in a 2-column grid instead of a single-file list. Device type labels in this view are more
  abbreviated (AP/STA/EAP/BLE) to fit a full MAC address in each column without truncating it.

### Fixed

- **Sidebar letter spacing, second hardware-feedback round:** the upright-character stack from
  the previous fix used `GfxRenderer::getLineHeight()` (~25px for FONT_SMALL_ID) as the vertical
  step between characters — that's the font's *paragraph* line-spacing metric, way more generous
  than actual glyph height (~12px for this font's uppercase letters), so letters read as
  distractingly gapped. Worse, at 25px/character an 8-letter label like "Settings" or "Continue"
  needed ~200px against an ~121px-tall slot, overflowing into the next pill down. Replaced with a
  fixed 16px step sized off the font's real glyph metrics (tight but not touching, even across a
  descender-into-cap worst case), which only shrinks further for the specific labels that would
  still overflow at 16px — so short labels keep consistent, non-gappy spacing instead of being
  stretched to fill the slot, and long ones are guaranteed to stay inside their own pill.
- **Landscape follow-up, from real hardware feedback:**
  - Sidebar button-hint labels were rotated as whole glyphs (`drawTextRotated90CW`), which on
    real hardware reads as sideways-tilted text rather than a vertical label. Replaced with a
    column of ordinary upright characters, one per line, centered in the pill.
  - `Chrome::drawStatRow` always drew its label at the fixed global left margin regardless of
    which column it was called for — harmless with one full-width card, but with
    `DashboardActivity`'s new side-by-side SIGNALS/CAPTURE STATUS columns, CAPTURE STATUS's
    labels were drawing on top of SIGNALS' box. Added an explicit `leftX` parameter (defaults to
    the old global-margin behavior, so every other caller is unaffected).
  - SIGNALS and CAPTURE STATUS now both extend down to the bottom of the content area instead of
    shrink-wrapping around their rows — there was a lot of unused space below them at the old
    tightly-fit height. CAPTURE STATUS's row block is vertically centered within that taller box;
    SIGNALS stays top-anchored.

### Changed

- **Rotated the UI to landscape (792x528, the panel's native orientation).** Button *behavior*
  is unchanged — `MappedInputManager` maps straight to hardware regardless of screen orientation,
  so Back/Confirm/Left/Right/Up/Down all still do exactly what they did before. What changes is
  purely visual:
  - `Chrome::drawFooterHints`'s button-hint pills move from a horizontal bar along the bottom to a
    vertical strip along the screen's right edge, using `GfxRenderer::drawTextRotated90CW` (an
    existing but previously-unused renderer primitive) so each label reads top-to-bottom instead
    of left-to-right. Which edge, and in what stacking order, is derived from the same coordinate
    geometry Portrait's own rotation already used — not a new guess — see the comment in
    `Chrome.cpp`. `Chrome::contentRight()`/`contentBottom()` now reserve space for that sidebar
    instead of a bottom bar, which every other screen picks up automatically since they only ever
    ask Chrome for the content-area edges.
  - `DashboardActivity`'s SIGNALS and CAPTURE STATUS cards now sit side by side instead of
    stacked full-width — landscape has a lot more spare width below the top row than the old
    portrait canvas did, but a lot less spare height, so this reflow was needed. Everything else
    on that screen (box size/position, RECENT DEVICES, mood text) is unchanged.
  - `SleepActivity`'s portrait art and summary text now sit side by side instead of stacked, so
    the art can stay at its native 400x400 resolution (it doesn't scale up) instead of having to
    shrink to fit a canvas now shorter than the art is tall.
  - `BootActivity` needed no changes — its shrink-to-fit-and-anchor-to-height layout was already
    orientation-agnostic by construction.
  - This also caught and fixed a latent bug: `Chrome::drawStatRow`'s default right-align edge
    (used by several `MaintenanceActivity` rows) was computed from the raw screen width instead
    of the shared content-area edge, which would have drawn those values under the new sidebar.
  - Also fixed two stale doc claims noticed along the way: the README described a
    `BTN_LEFT`/`BTN_RIGHT` hardware swap in `MappedInputManager` that was actually reverted a
    while back (it's a direct passthrough now — see that file's own comment), and said Log Viewer
    pages 8 records at a time when it's actually 5 (`kPageSize`).

### Added

- **"RUBY" moved into the creature's own box.** It used to be the dashboard's header title;
  it's now a small chip pinned to the top-left corner of Ruby's box itself
  (`RubySpriteRenderer::draw`), so the header on that screen is just the divider rule and battery
  badge. Drawn inside the sprite renderer (not the caller) so it survives the box's own
  ~1.2s partial-refresh tick, the same reason the box's rounded border lives there too.
- **Much bigger Lore, and it now talks about real captures.** The static flavor-text pool doubled
  (16 → 32 entries), and a new second tier of 14 entries generates its text live from actual
  capture data — unique AP/client/BLE counts, handshakes, lifetime frame counts, uptime, boot
  count, log size, and the most recently heard device/signal strength — pulled from
  `SignalCatalog`, `RecentSightings`, `EncryptedLog`, and `RubyAppState`. These re-render fresh
  every time Lore is opened rather than freezing whatever was true at unlock time. Unlock pacing
  (one entry per 6 lifetime captures) is unchanged, so full unlock now takes longer (46 entries
  instead of 16) — intentional for a device meant to accumulate lore over weeks, not a day.
- **Raw handshake capture → crackable `.pcap` export.** A new opt-in, off-by-default setting
  (`Settings > Raw handshake capture`) that captures WPA 4-way-handshake frames verbatim —
  ANonce/SNonce/MIC and all — plus the SSID-bearing beacon for each network involved, and writes
  them as a standard libpcap file (`/.ruby/pcap/YYYYMMDD.pcap`, link-layer type 105/raw 802.11)
  that hashcat or hcxpcapngtool can attempt to crack offline. This is for auditing the password
  strength of networks the device's owner controls. Every other capture path in Ruby is
  deliberately metadata-only and AES-256-GCM-encrypted at rest (see `EncryptedLog`); this one
  can't be, because a cracking tool needs the exact bytes that were sent over the air — so these
  files are **plaintext** on the SD card, which is why the setting defaults off and the Settings
  screen shows a warning explaining the tradeoff before it's turned on. `MaintenanceActivity` now
  also shows raw-capture file count/size (only when files exist) alongside the encrypted log
  stats, with the same plaintext callout.
  - `WifiSniffer` gained a small (8-slot, ~3.3 KB, allocated only once the setting is actually
    turned on) side queue and an 8-entry BSSID tracking cache to know which beacon to grab —
    deliberately tiny given the RAM crisis fixed below; a user who never enables this pays zero
    extra RAM for it.
  - New `PcapWriter` module mirrors `EncryptedLog`'s day-rotation and open/write/close-per-record
    pattern (see the Log Viewer regression entry below for why that pattern was chosen over a
    persistent handle).

### Fixed

- **Sleep crash, take 4 — found it, confirmed fixed.** The added heap diagnostics gave a
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
