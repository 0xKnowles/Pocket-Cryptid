# Changelog

All notable changes to this project are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- **Level 1-5 badge, tracking lifetime EXP** (`RubyState::totalExp`, `RubyConfig::levelForExp()`).
  Shown next to the existing "RUBY" name chip on the dashboard — no changes to the creature's
  art/expressions, just a small "Lv.N" badge beside it. EXP comes from two very differently-sized
  rewards: a captured handshake is worth `kExpPerHandshake` (2000 EXP) and a newly-seen unique
  device (AP/client/BLE) is worth `kExpPerUniqueDevice` (1 EXP) — chosen to roughly match the
  real-world ratio (about 1 handshake per ~2000 unique devices seen) so the two contribute
  comparably to leveling over a typical session, rather than common unique-device sightings
  drowning out rare handshakes or vice versa. Level thresholds: 0 / 10,000 / 40,000 / 100,000 /
  250,000 EXP for levels 1-5 (5x the original 2,000/8,000/20,000/50,000 — real overnight testing
  reached level 3 in a single 5-hour unattended run, leveling much faster than intended for a
  "lifetime progress" badge; now takes 5 captured handshakes alone to reach level 2, instead of
  just 1). Persisted alongside the rest of `RubyState` in `/.ruby/ruby_state.json`.
- **Dashboard header EXP progress bar**, right beside the battery badge — fills toward the next
  level as `RUBY.expProgress()` climbs, empty again immediately after a level-up. `Chrome::drawHeader()`
  now returns the x just past the battery badge specifically so this bar (and anything else a
  future blank-title screen wants to add there) can sit right next to it without recomputing the
  badge's width. Deliberately long enough to read at a glance (260px) but far short of the full
  header row, so it doesn't compete with the battery badge for attention. A "`N/M EXP`" label
  (`RubyManager::expIntoLevel()`) sits just to the right of the bar itself — EXP earned so far
  this level, and the fixed total EXP the current level spans (not a countdown, so the second
  number stays put while the first climbs toward it) — reading "MAX" once level 5 is hit.
- **"LEVEL UP → Lv.N" banner** (`RubyManager::consumeJustLeveledUp()`) — leveling up used to be
  silent, visible only by noticing the header badge/bar had moved. Reuses the existing
  HANDSHAKE CAPTURED/DEAUTH ACTIVITY NEARBY one-shot banner mechanism, and slots into the same
  priority chain: a nearby deauth alert still wins if both are active, then a level-up (bigger,
  rarer news, and often triggered by the very same handshake that also fires the handshake
  banner — every level threshold is a round multiple of `kExpPerHandshake`), then the ordinary
  handshake banner.
- **Vendor OUI lookup result cache** (`lookupVendorOui()`, `lib/SignalCatalog/VendorOui.cpp`) — a
  small 32-entry RAM-only cache keyed by OUI prefix, so the *same* vendor looked up repeatedly
  across redraws of the same handful of on-screen devices (the normal case — Recent Devices/Device
  Log redraw the same ~16 entries every tick, not a fresh batch each time) only ever touches the SD
  card once. Caches misses too, since an unrecognized OUI previously re-scanned all the way to EOF
  on every redraw — the worst case for a full ~50,000-entry registry. No behavior change for
  callers; purely a latency fix.
- **Dashboard always shows an "-- ACTIVE --"/"-- PAUSED --" tag under Ruby's mood**, not just
  "-- PAUSED --" while paused. The tag's row height feeds into the layout below it
  (`std::max(moodY, infoY)`), so showing it unconditionally means the SIGNALS/CAPTURE STATUS row
  no longer shifts up by one line every time capture resumes — the paused layout was intentionally
  liked, this just makes both states match it instead of only one.
- **`Settings → WiFi channel scope`** (`RubySettings::wifiChannelScope`,
  `WifiSniffer::channelPlanFor`). WiFi monitor mode hops all 13 channels by default at
  `wifiChannelDwellMs` each — a real 4-way handshake completes in well under a second, so any
  single channel is only actually being listened to roughly 1/13th of the time, which is most of
  why passive handshake capture can yield as little as 0-2/hour. This new row cycles between "All
  (1-13)" and locking onto one specific channel (visible per-network in the live scan behind
  `Whitelist`/`Blacklist`), raising that channel's duty cycle to 100% at the cost of not seeing
  anything on the other 12. Takes effect the next time WiFi monitor mode restarts, same as the
  existing dwell-time setting right above it — not applied live to an already-running capture
  session.
- **Crash-loop guard for repeated silent restarts** (`silentRebootCount`, `main.cpp`). Real-hardware
  testing found BLE passive scan fragmenting the DMA-capable memory pool `EncryptedLog`'s hardware
  AES needs badly enough, immediately at boot, to trip the existing DMA-pool circuit breaker within
  under a second — every single boot, identically, forever. Restarting alone never fixed it, so the
  device sat in an endless ~1-second restart loop that looked and felt exactly like a hang. Now
  counts consecutive silent restarts across a `RTC_NOINIT_ATTR` boot chain, and after 3 in a row,
  force-disables BLE passive scan, raw handshake capture, and active deauth, persists that, resets
  the count, and falls through to a normal (not silent-restart-skipped) boot screen — so the device
  recovers into a working, capturing state instead of looping forever, and it's visible that a
  recovery happened rather than looking like it's still just hanging.
- **`Settings → Reset signal stats`** (`SignalCatalog::resetStats()`) — zeroes the Dashboard's
  SIGNALS card (unique AP/client/BLE counts, handshakes captured) and clears the dedup rings behind
  them, so a MAC already seen before the reset can register as "new" again afterward rather than
  being silently ignored forever. Same arm-then-confirm-within-5-seconds pattern as `Wipe encrypted
  log`, as an independent action — arming one doesn't arm the other. Persists immediately rather
  than waiting for the usual debounced save. Doesn't touch the encrypted log or raw captures, only
  the dashboard counters.
- **Vendor OUI lookup** (`lookupVendorOui`, optional `/.ruby/oui.txt` on the SD card) — an entry
  formatted like IEEE's own public OUI registry export or Wireshark's `manuf` file (one
  `AABBCC<TAB>Vendor Name` per line). No database ships in firmware; costs nothing when the file
  isn't present. Recent Devices now shows the matched vendor name instead of "(no name)" for
  entries with no advertised label.
- **Active deauth: transmit capability re-enabled behind a transient WIFI_MODE_STA switch,
  pending real-hardware confirmation.** `DeauthEngine::sendDeauthBurst()` now switches the radio
  to `WIFI_MODE_STA` only for the duration of one burst (a handful of milliseconds), sends, then
  immediately reverts to `WIFI_MODE_NULL` — rather than the earlier attempt, which brought STA
  mode up for the whole session and crash-looped the X3 via `esp-aes` interrupt starvation. The
  working theory: that crash was about *ordering*, not concurrency — STA mode came up before
  `EncryptedLog` had ever written a record, so the interrupt-hungry STA driver and `esp-aes`'s
  very first interrupt request collided at boot. With `WIFI_MODE_NULL` as the resting state
  (unchanged), `esp-aes` claims its interrupt during ordinary logging long before any burst can
  fire, so this transient switch only asks the driver to reconfigure an already-running radio.
  Cross-checked against `github.com/yattsu/biscuit`'s WiFi deauther, which uses the same
  `WIFI_MODE_STA` + `esp_wifi_80211_tx()` technique on the same hardware — the key difference is
  Biscuit never runs anything like `EncryptedLog`'s always-on background AES logging concurrently
  with it, so it had no reason to hit (or avoid) the ordering issue above. **Needs a real device
  to confirm** — this environment can't compile-test interrupt behavior, and repeatedly toggling
  STA mode over a long session could plausibly fragment the DMA pool even if each individual
  switch is safe (the existing heap-health circuit breaker in `main.cpp` is the safety net if
  that happens: a silent restart, not a hard crash).
- **PMKID-capable capture counter** (`WifiSniffer::pmkidCapableFrames`, shown on the Export
  screen's RAW CAPTURE card). Scans each raw-captured EAPOL message-1 frame's Key Data field for
  the vendor-specific PMKID KDE (OUI `00:0F:AC`, type 4) — when present, hashcat's `-m 22000`
  PMKID mode can recover a PSK from that single frame, without ever needing a client to complete
  the rest of the 4-way handshake. Ruby was already capturing these bytes verbatim as part of
  every M1 frame; this just makes that fact visible on-device instead of only discoverable by
  running `hcxpcapngtool` on a PC afterward.
- **Configurable Power button short-press action** (`Settings → Power button (tap)`). A long hold
  is always Sleep, unconditionally — only the short tap is configurable now, cycling between
  Refresh (the previous, only behavior — a manual ghost-clearing full refresh), Screenshot (new —
  dumps the current framebuffer to `/.ruby/screenshots/<unixtime>.bmp`, a plain uncompressed 1bpp
  BMP, no encoder library needed since the framebuffer is already exactly that format in this
  orientation), and Pause (the same capture-pause toggle as Dashboard's own Pause button, usable
  from any screen instead of only the Dashboard).
- **Boot screen: "listening..." moved from the bottom of the screen to the top-center title
  block**, next to "Ruby"/the version row, and "passive RF analyzer" renamed to
  "Pocket RF Analyzer". The two used to be split across opposite edges of the screen (with
  "listening..." drawn in white specifically to stay readable over the full-art branch's dark
  e-waste pile background) — now they read as one title block, and both branches (with or
  without `boot.bmp` present) share the same text placement instead of duplicating it.
- **Dashboard: "Handshake Capture" and "DeAuth" ON/OFF readouts**, to the right of the Mood block
  in what was previously dead whitespace (the RECENT DEVICES card above it ends at the box's
  height; SIGNALS/CAPTURE STATUS don't start until below the mood text) — surfaces both opt-in
  capture settings without a trip into Settings.
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
- **Passive deauth/disassoc detector reintroduced** (`DeauthDetector`) — see "Removed" further down
  for why it left in the first place: it was never the RAM problem (`ApHistory`'s 256-entry table
  was), so it's back on its own, unaccompanied by `TrackerDetector`/`ApHistory`. Counts
  deauth/disassoc management frames from *any* source over the air (not just this device's own
  `DeauthEngine`, whose bursts are both physically invisible to the same antenna during TX and, as
  of the entry above, confirmed to never actually reach the air at all) and flags a 15-second alert
  window after a 5-frame spike within 5 seconds. Dashboard's old "DeAuth: ON/OFF" stat row — which
  only ever reflected `SETTINGS.activeDeauthEnabled`, a setting now known to be disconnected from
  real-world radio behavior — is replaced with **"Nearby DeAuth"**, showing `ALERT` during a spike,
  a running `N seen` count otherwise, or `none`. The dashboard banner that used to only ever announce
  a captured handshake now also announces "DEAUTH ACTIVITY NEARBY" (taking priority over the
  handshake banner if both are active at once).
- **`logRecordTypeCompactName()`** (`LogRecord.h`) — the 2-3 letter AP/STA/EAP/BLE codes Dashboard's
  RECENT DEVICES card already used, promoted from a copy local to `DashboardActivity.cpp` into a
  shared sibling of `logRecordTypeShortName()` now that Device Log and Log Viewer need the same
  compact codes for their own dense grids (see "Fixed", below).
- **RSSI signal-strength bars** (`Chrome::drawSignalBars()`) — a small 4-bar icon, like a phone's
  signal indicator, next to the RSSI reading on every entry in Dashboard's RECENT DEVICES card,
  Device Log, and Log Viewer. 4 bars at -50 dBm or stronger, down to 1 bar (never 0 — every entry
  here is a frame Ruby actually heard) below -70 dBm. Hand-drawn with plain filled rectangles
  rather than a font glyph, since Space Mono has no signal-bar character.
- **Dashboard's RECENT DEVICES card reworked: fewer entries, more per entry, plus a new SIGNAL
  HISTORY card beside it — both now extending down to fill the space beside the Mood column too**
  (there's no card down there anymore; see "Removed" below). RECENT DEVICES is narrower now (half
  its old width) and shows each entry across a full 3 lines — type/MAC, signal bars + RSSI +
  time-ago, and the device's name (falling back to a vendor-OUI lookup, same as Device Log) —
  rather than 2 lines with more entries crammed in. The other half of the row that opens up is
  SIGNAL HISTORY, with two stacked sections instead of one chart alone with a lot of otherwise idle
  vertical room: HANDSHAKES (top) is a plain "N ago" text list of recent captures, newest first,
  from a dedicated handshake-timestamp ring rather than `RecentSightings`' shared, AP/client/
  BLE-dominated 16-slot feed — handshakes are rare enough that ordinary traffic would push one out
  of that shared feed within moments, the opposite of "historical" for an event this infrequent.
  This ring is now persisted (`RubyState::handshakeTimestamps`, real Unix timestamps rather than
  `millis()`, which resets every reboot and can't tell "historical" apart from "since the last
  power-on") so captures from previous sessions still show up here too, not just the current one —
  while SIGNAL (bottom) stays a recent-observations RSSI bar chart, oldest on the left, newest
  anchored to the right.
- **Ruby thinks and talks now** (`RubyThoughts.h`, `Chrome::drawThoughtBubble()`/`drawSpeechBubble()`).
  A comic-style bubble runs along the bottom edge of her box, tailing up into her face — clear of
  both the name chip up top and her eyes in the middle — showing a quirky, hacker-flavored one-liner
  tied to her current mood — "ooh, what's this OUI?" while CURIOUS, "no bytes... send help" while
  BORED, and so on — updating on the same ~1.2s cadence as her expression itself. A pointed speech
  bubble briefly takes over instead for a few seconds whenever something actually happens: a new
  AP/client/BLE device spotted, a handshake captured, a level up, or a nearby deauth alert — each
  with 9 possible reactions to pick from (tripled from the original 3) so it isn't the exact same
  line every time.
- **Total Uptime**, a new row under "Uptime this session" in CAPTURE STATUS — lifetime awake time
  across every boot (`RubyAppState::totalCaptureSeconds`, already persisted at each sleep entry,
  just never previously surfaced anywhere in the UI), plus this still-running session's own
  elapsed time so the figure is always current. `formatUptime()` now takes whole seconds instead of
  milliseconds, since a lifetime total can run well past the ~49-day point where a `uint32_t`
  millisecond count wraps. SIGNALS and CAPTURE STATUS's rows now use a smaller font/row height
  (`drawCompactStatRow()`, Dashboard-local) so this 5th row fits without clipping past the card's
  bottom edge. `Settings → Reset signal stats` now zeroes it too (alongside EXP and the handshake
  history) — otherwise there was no way to reset it short of never running the device this long.

### Removed

- **Dashboard's "CAPTURE SETTINGS" card** (Handshake Capture/Active DeAuth/Nearby DeAuth), added
  earlier in this same batch of changes — all three are already on the Settings screen, and real
  usage found them redundant clutter on the dashboard itself. RECENT DEVICES and SIGNAL HISTORY now
  extend down to use that freed space instead (see "Added" above) rather than leaving it empty.

### Fixed

- **Sleep screen's "N unique devices catalogued" line ran off the right edge of the screen** once
  the count reached 2+ digits — drawn with a plain, unbounded `drawText` that never accounted for
  the panel's actual width. Both summary lines on that screen are now truncated to the real
  available width.
- **Dashboard's header bar (battery badge + EXP bar) read as noticeably thicker than it needed to
  be** — its divider always sat at the shared header height every other screen's real title relies
  on for ascender clearance, even though this screen's title is blank and its header content is
  only 16px tall. `Chrome::drawHeader()` now takes an optional divider-position override (every
  other screen's call site is unaffected, keeping the default), and Dashboard passes a shorter one
  sized to its actual content, freeing the ~12px difference for the content below instead of
  leaving it as dead padding.
- **`Settings → Reset signal stats` didn't reset Ruby's Level/EXP**, only the dashboard's dedup
  counters and hash rings. A MAC that had already contributed EXP could re-trigger a "new unique"
  event (and re-award EXP) the moment its dedup ring entry was cleared by the reset and it was
  seen again, silently inflating the Level badge relative to what the just-reset SIGNALS counters
  now showed. `RUBY.resetExp()` is now called alongside `SIGNAL_CATALOG.resetStats()` so the two
  can't drift apart; the confirmation prompt's wording now says so too.
- **Boot screen's "listening..." overlapped the character's head** on real hardware — it sat on a
  second line stacked directly under "Pocket RF Analyzer", which the previous layout assumed was
  clear down to `y=40`, an assumption that didn't hold for every `boot.bmp`. "Pocket RF Analyzer"
  and "listening..." now share a single line ("Pocket RF Analyzer — listening...") right under the
  Ruby/version row, keeping the whole title block clear of the art regardless of where the
  character's head actually starts.
- **Every screen's header title overlapped the divider rule drawn directly under it.**
  `Chrome::drawHeader()` draws the title with `FONT_UI_12_ID` BOLD at a fixed `y=6`, but that
  font's real ascender is 28px, putting the glyphs' baseline at `y=34` — well past the old
  `kHeaderHeight` of 28, which is where the divider was drawn. The divider rule was cutting
  through the lower quarter of every title's letters; most visible on screens with a real title
  (Dashboard's is blank, so it didn't show there). `kHeaderHeight` is now 40, leaving the baseline
  a clean 6px above the divider on every screen.
- **Device Log ran most of its 16 possible entries off the bottom of the screen.** It drew every
  `RecentSightings` entry in one wide-spaced column (~58px per entry, two `getLineHeight()`-spaced
  lines each) despite this file's own header comment already promising all 16 fit on one screen —
  at that spacing only about 8 did. Rewritten to use the same tight multi-column grid Dashboard's
  RECENT DEVICES card uses (13px lines, 224px columns), plus a third line for the label that
  Dashboard's narrower card omits — all 16 entries now always fit, with room to spare.
- **Log Viewer only fit 5 records per page** in a bordered, heavily-padded card format (~101px
  per record) — nowhere near as dense as the rest of the app's live-feed screens, and its own
  filename/hint row spacing (`y += 20`/`y += 18`, guessed rather than measured) ran past
  `FONT_UI_10_ID`'s real line height (31px), so the "Left/Right: switch file..." hint started
  drawing before the filename row above it had finished. Rewritten to the same borderless,
  multi-column grid as Device Log, with page size and Up/Down's paging step both computed from the
  real content area (`gridCapacity()`) instead of a fixed constant — around 27 records now fit per
  page on this panel, versus 5 before.
- **Active deauth confirmed non-functional on this hardware, via real-hardware testing** —
  `DeauthEngine`'s crash-loop concern is resolved (many bursts fired across a real test session
  with no crash or DMA-pool circuit-breaker trip), but every single burst's `esp_wifi_80211_tx()`
  call was rejected by the WiFi driver itself, logging `wifi: unsupport frame type: 0c0` once per
  frame — matching ESP-IDF's own documented/community-reported behavior: its raw-TX allowlist
  (beacon/probe request/probe response/action/non-QoS data) hard-codes out deauth specifically, at
  the driver level, independent of anything this codebase does. `burstsSent()`/`framesTransmitted()`
  kept counting up normally the whole time despite nothing reaching the air, silently overstating
  success — `DeauthEngine` now tracks `framesRejectedByDriver()` too, logs a clear warning per
  rejected burst, and the Export screen's ACTIVE DEAUTH card shows a **Rejected by driver** count
  alongside the existing attempted/sent ones. The only known way past the driver-level rejection —
  patching ESP-IDF's precompiled `libnet80211.a` to weaken/override
  `ieee80211_raw_frame_sanity_check` — is a real, community-used technique, but every example found
  targets classic ESP32/S2/S3 (Xtensa); this device is an ESP32-C3 (RISC-V, a different toolchain
  with no confirmed working precedent for this specific patch), and even on the platforms it's
  most tried on it isn't reliably successful. Not attempted here given that and this project's own
  prior multi-session dead end from a similarly-scoped precompiled-SDK patch attempt (see the
  software-AES/`custom_sdkconfig` history further down this file).
- **`Settings → Active deauth` could keep showing ON after it had actually gone silently
  inactive.** Turning off `Raw handshake capture` correctly disabled the live `DeauthEngine` (it
  refuses to run without something capturing the handshake it forces), but left
  `SETTINGS.activeDeauthEnabled` itself untouched — so the row still read ON, and turning raw
  capture back on later would silently re-arm active deauth with no fresh confirmation from that
  row at all. Toggling raw capture off now also clears the `activeDeauthEnabled` setting itself,
  not just the runtime engine, so the row and reality can't drift apart, and re-enabling deauth
  always requires an explicit, fresh toggle.
- **`Settings → WiFi monitor`, toggled off then back on, silently dropped raw handshake capture**
  even if it had been on — `wifiSniffer.start()` was called there without passing
  `SETTINGS.rawHandshakeCaptureEnabled` through, defaulting to `false`. Fixed alongside wiring in
  the new channel-scope setting above, since both needed the same call site touched anyway.
- **Ruby's mood was pinned on CURIOUS almost permanently in any reasonably active RF
  environment.** `RubyBehavior::expressionFor()` used to trigger CURIOUS off *any single*
  new-unique WiFi/BLE sighting within the last 90 seconds — in a target-rich environment (a busy
  street, an apartment building), a new device can realistically show up more often than every 90
  seconds indefinitely, so CONTENT's "steady baseline" state could almost never actually surface.
  `RubyManager` now tracks a small ring of recent new-unique-sighting timestamps
  (`kRecentEventCapacity`), and CURIOUS requires a real burst — `kCuriousBurstThreshold` (3)
  sightings within the window, not just one — so ordinary single sightings correctly read as
  CONTENT, and CURIOUS goes back to meaning "something just picked up" the way it was intended to.
- **Pausing capture left Ruby's expression frozen on whatever it was the instant before Pause was
  pressed**, instead of reflecting that nothing is being observed anymore — noticeable since the
  natural decay toward BORED/LONELY can take up to 20-60 minutes of real elapsed time.
  `DashboardActivity` now shows BORED immediately whenever `captureIsPaused()`, reusing its
  existing "half-lidded" face art rather than adding a new expression just for this.
- **BLE passive scan + raw handshake capture together still crash-looped even after the fix
  below**, confirmed via real-hardware serial log: BLE alone settles at a steep but survivable
  ~63 KB of heap use once running (free heap dropped from 71196 to 8144 bytes across an 8-second
  window after BLE started), but turning raw handshake capture on *while BLE was already running*
  pushed the DMA-capable pool's largest free block down to 1396 bytes, tripping the circuit
  breaker — and since both settings were now persisted `true`, the next 2 boots hit the identical
  combination immediately, until the crash-loop guard caught it at 3 restarts. `SettingsActivity`
  now refuses to turn either one on while the other is already active (BLE checked against
  `bleScanner.isRunning()`'s live state, raw capture checked against the setting), so hitting this
  combination live no longer requires a restart to recover from — turn one off first. `main.cpp`'s
  `startCaptureIfEnabled()` also disables BLE at boot if a persisted `settings.json` somehow still
  has both on (from before this fix, or hand-edited), so that specific bad state can't even start
  down the crash-loop path in the first place.
- **BLE passive scan enabling itself caused an endless ~1-second restart loop, presenting as a full
  system hang.** Root-caused via real-hardware serial log after the fix above wasn't enough on its
  own: BLE's controller/host buffers fragment the same DMA-capable pool `EncryptedLog`'s hardware
  AES needs down to ~3-4 KB immediately at boot (below `HEAP_DMA_LARGEST_BLOCK_MIN`), which
  correctly trips the existing DMA-pool circuit breaker (see `main.cpp`) — but since the exact same
  fragmentation reproduces identically on the very next boot, the device silently restarted forever
  instead of recovering. `bleSniffEnabled` now defaults to `false` (previously `true`, auto-starting
  on every boot) so this risky path never runs without deliberate opt-in, and the new crash-loop
  guard (see "Added", above) breaks the cycle even for a user who does opt in.
- **BLE passive scan silently failing to start when toggled on, with no indication anywhere why.**
  `NimBLEDevice::init()` returns `void` and can fail internally (confirmed via real-hardware serial
  log: `esp_bt_controller_init()` unable to claim its memory pool — free heap was down to ~19 KB,
  independent of WiFi monitor capture's own state) without throwing or logging anywhere visible
  without a serial monitor attached. `BleScanner::begin()` used to mark itself successful
  regardless, so a subsequent failed `start()` looked identical to the setting simply being off.
  Now checks `NimBLEDevice::isInitialized()` and fails loudly (`LOG_ERR`) instead. `Settings → BLE
  passive scan` also now shows **FAILED** rather than a plain **ON** when the setting is on but the
  radio didn't actually start, since that row (not just the Dashboard's live status) is the one
  place an owner without a serial monitor would ever see this happened. See "Removed", below, for
  the actual memory fix.
- **Sleep screen ghosting/burn-in for the entire time the device sat asleep.** `SleepActivity`
  drew its one-time "GONE QUIET" screen with a bare `displayBuffer()`, defaulting to
  `FAST_REFRESH` — the partial-update waveform, which doesn't fully clear whatever was on screen
  before (typically the Dashboard). Since this screen is meant to sit unchanged on the panel for
  hours, it's exactly the case that needs `FULL_REFRESH`'s complete waveform cycle instead, same
  as the existing manual Screen-Refresh action and the Dashboard's own ghost-clear interval both
  already use for the same reason.
- **Most screens besides Dashboard/SleepActivity looked lower-contrast/"washed out" than intended.**
  Same root cause as the Sleep screen fix above, just spread across nearly every other activity:
  `BootActivity`, `CrashActivity`, `MaintenanceActivity` (Export), and `FullScreenMessageActivity`'s
  default were all drawing their one-shot screens with a bare `displayBuffer()`/an explicit
  `FAST_REFRESH` default — the lower-fidelity partial-update waveform — instead of the crisp,
  higher-contrast `FULL_REFRESH` waveform a one-shot screen has no reason not to use. Changed all
  four to `FULL_REFRESH` unconditionally. The frequently-interactive/live screens
  (`LogViewerActivity`, `TargetPickerActivity`, `DeviceListActivity`, `SettingsActivity`) needed a
  different fix, since forcing `FULL_REFRESH` on every redraw would make paging/navigation/the live
  device feed flash repeatedly instead: each now tracks a `firstRenderSinceEnter` flag, set in
  `onEnter()`, so the first render after entering the screen gets a crisp `FULL_REFRESH` and every
  subsequent redraw (paging, cursor movement, the periodic live-feed tick) stays `FAST_REFRESH` as
  before.
- **Dashboard's card titles ("RECENT DEVICES", "SIGNALS", "CAPTURE STATUS"), the "RUBY" name chip,
  and the "-- PAUSED --"/"-- ACTIVE --" tag were never actually bold on real hardware**, despite
  the code requesting `EpdFontFamily::BOLD` for all of them. `FONT_SMALL_ID` (Space Mono 8) only
  has a Regular face registered — no bold 8pt asset exists — and `EpdFontFamily::getFont()`
  silently falls back to Regular when the requested style's face isn't registered, so the `BOLD`
  argument was a no-op the whole time. Faked properly now by drawing each of those labels twice,
  offset one pixel horizontally, the same trick dot-matrix mono fonts commonly use to fake a
  heavier weight without a dedicated bold face.

### Changed

- **Dashboard's "Encrypted log (today)" row renamed to just "Encrypted log"** — the "(today)"
  qualifier was accurate (it's `EncryptedLog::currentFileSizeBytes()`, today's file only) but
  cluttered a label that's already tight for space next to WiFi monitor/BLE scan/uptime.
- **Settings screen's row labels ("WiFi monitor", "BLE passive scan", etc.) now draw bold.**
  Unlike `FONT_SMALL_ID` above, `FONT_UI_10_ID` (Space Mono 10) does have a real bold face
  registered, so this one's a straightforward style change (`EpdFontFamily::REGULAR` →
  `EpdFontFamily::BOLD`), not a fallback-bug fake-out.

### Removed

- **Passive deauth/disassoc detector, BLE tracker detector, and persistent AP history — added,
  then pulled back out after real-hardware testing showed they broke BLE passive scan.** All three
  shipped together in a batch alongside the Vendor OUI lookup (which stays — see "Added" further
  down and the BLE fix above). `ApHistory`'s 256-entry table alone added a fixed ~14 KB of
  permanent RAM use (`sizeof(Entry)` ≈ 56 bytes × 256), dramatically more than every sibling table
  in `lib/SignalCatalog` (`ApScanCache` uses 32 entries, `RecentSightings`/`TrackerDetector`
  themselves used only 16) — on a chip with ~380 KB total RAM and no PSRAM, already tight enough
  that BLE's controller init was failing with only ~19 KB free heap in the field, confirmed via
  serial log. `DeauthDetector` and `TrackerDetector` cost only a few hundred bytes between them and
  weren't the actual problem, but they shipped in the same batch as the real culprit and depended
  on BLE (in the tracker detector's case) or added Dashboard/Export UI surface for a feature set
  that, on balance, wasn't worth trading reliable BLE capture for. Reverted `DeauthDetector.h/.cpp`,
  `TrackerDetector.h/.cpp`, `ApHistory.h/.cpp`, `ApHistoryActivity.h/.cpp`, the `Deauth`/`Disassoc`
  `WifiFrameKind` additions, `BleObservation::manufacturerPayload`, the Dashboard banner priority
  chain, the Export "NEARBY THREATS" card, and Settings' AP History row — all cleanly, back to
  their pre-existing state.
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

- **Dashboard: "Mood" and its value (e.g. "Curious") visibly overlapped.** The line spacing
  between them was a hardcoded guess (20px) rather than the actual font metric — `FONT_UI_12_ID`
  BOLD's real glyph height runs taller than that guess. Switched to
  `renderer.getLineHeight(fontId)` per line so the spacing always matches what's actually drawn.

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
