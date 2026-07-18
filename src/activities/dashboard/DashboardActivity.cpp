#include "DashboardActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <string>

#include "ApScanCache.h"
#include "BleScanner.h"
#include "CaptureControl.h"
#include "DeauthDetector.h"
#include "EncryptedLog.h"
#include "LogRecord.h"
#include "RecentSightings.h"
#include "RubyAppState.h"
#include "RubySettings.h"
#include "SignalCatalog.h"
#include "VendorOui.h"
#include "WifiSniffer.h"
#include "fontIds.h"
#include "ruby/RubyBehavior.h"
#include "ruby/RubyManager.h"
#include "ruby/RubySpriteRenderer.h"
#include "ruby/RubyThoughts.h"
#include "ui/Chrome.h"

namespace {
constexpr unsigned long kFullRedrawIntervalMs = 5000;
constexpr unsigned long kPetRedrawIntervalMs = 1200;
constexpr unsigned long kHandshakeBannerMs = 4000;
constexpr unsigned long kLevelUpBannerMs = 4000;
constexpr unsigned long kSpeechBubbleMs = 4000;

// Chrome::kHeaderHeight (40px) is sized for a real title's full ascender clearance, which this
// screen's blank title never needs — its header row is just the 16px-tall battery badge/EXP bar
// pair starting at y=5 (bottom edge y=21). A shorter, Dashboard-specific divider position (instead
// of the shared kHeaderHeight every other screen's real title relies on) removes the otherwise-idle
// ~12px of vertical padding that made this header band read as noticeably thicker than it needed
// to be, and the freed space is passed straight on to rubyBoxY below.
constexpr int kDashboardHeaderHeight = 28;

// Takes whole seconds rather than ms — the lifetime "Total Uptime" figure (APP_STATE.totalCaptureSeconds
// plus this session's own elapsed time) can run well past the ~49-day point where a uint32_t ms count
// wraps, but seconds alone comfortably covers a device's realistic service life.
void formatUptime(unsigned long totalSec, char* out, size_t outSize) {
  const unsigned long d = totalSec / 86400;
  const unsigned long h = (totalSec % 86400) / 3600;
  const unsigned long m = (totalSec % 3600) / 60;
  const unsigned long s = totalSec % 60;
  if (d > 0) {
    snprintf(out, outSize, "%lud %luh", d, h);
  } else if (h > 0) {
    snprintf(out, outSize, "%luh %lum", h, m);
  } else {
    snprintf(out, outSize, "%lum %lus", m, s);
  }
}

void formatBytes(uint64_t bytes, char* out, size_t outSize) {
  if (bytes >= 1024 * 1024) {
    snprintf(out, outSize, "%.1f MB", bytes / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(out, outSize, "%.1f KB", bytes / 1024.0);
  } else {
    snprintf(out, outSize, "%llu B", static_cast<unsigned long long>(bytes));
  }
}

void formatMac(const MacAddress& mac, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac.bytes[0], mac.bytes[1], mac.bytes[2], mac.bytes[3],
           mac.bytes[4], mac.bytes[5]);
}

// Resolves a BSSID to its cached SSID (ApScanCache — the same live network scan
// TargetPickerActivity's picker uses), falling back to the raw MAC if the network hasn't been
// seen recently enough to still be cached, or hasn't advertised a name — the DEAUTH/HANDSHAKE
// banners should always have *something* concrete identifying the network, not just a bare
// "something happened" message.
void formatNetworkLabel(const MacAddress& bssid, char* out, size_t outSize) {
  const ApScanCache::Entry* entry = apScanCache.findByBssid(bssid);
  if (entry && entry->ssidLen > 0) {
    snprintf(out, outSize, "%s", entry->ssid);
  } else {
    formatMac(bssid, out);
  }
}

void formatAgo(unsigned long seenAtMs, char* out, size_t outSize) {
  const unsigned long ageSec = (millis() - seenAtMs) / 1000;
  if (ageSec < 60) {
    snprintf(out, outSize, "%lus ago", ageSec);
  } else {
    snprintf(out, outSize, "%lum ago", ageSec / 60);
  }
}

// Paused means nothing is being observed right now, so the expression should read that way
// immediately rather than lag behind whatever real activity was happening right before Pause was
// pressed — BORED already has its own "half-lidded" face art (see RubySpriteRenderer::drawFace),
// so this reuses it rather than adding a new expression just for this.
RubyExpression effectiveExpression() {
  return captureIsPaused() ? RubyExpression::BORED : RUBY.currentExpression(false);
}

constexpr int kCardOutsetX = 6;   // border stroke sits this far outside the card's text column
constexpr int kCardTitleGap = 6;  // space between the title rule and the first row
constexpr int kCardTopPad = 6;
constexpr int kCardBottomPad = 8;
constexpr int kCardGap = 10;    // vertical gap between stacked cards
constexpr int kColumnGap = 14;  // horizontal gap between the top row's two columns

// FONT_SMALL_ID only ships a Regular face (see fontIds.h) — EpdFontFamily::getFont() silently
// falls back to Regular when a style's face isn't registered, so passing BOLD here never actually
// rendered bold on real hardware, despite asking for it. There's no bold 8pt face to register
// instead (only space_mono_8_regular exists), so this fakes it the way dot-matrix mono fonts
// commonly do: draw the same glyphs twice, offset one pixel to the right, thickening every stroke.
void drawBoldSmall(const GfxRenderer& renderer, int x, int y, const char* title) {
  renderer.drawText(FONT_SMALL_ID, x, y, title, true);
  renderer.drawText(FONT_SMALL_ID, x + 1, y, title, true);
}

// Draws a bold section caption + rule spanning [x, x + width) at `y`. Returns the y the first row
// should land at. Pairs with endStatCard(), which closes the rounded outline once the caller
// knows where the last row ended — the two are separate calls (rather than one that takes a row
// count) because every card here has a different row count driven by runtime state. Takes an
// explicit x/width (rather than always spanning the full content width) so the same card chrome
// works for both the full-width SIGNALS/CAPTURE STATUS cards and the narrower top-right device
// column.
int beginStatCard(const GfxRenderer& renderer, int x, int width, int y, const char* title) {
  drawBoldSmall(renderer, x, y + kCardTopPad, title);
  const int ruleY = y + kCardTopPad + renderer.getLineHeight(FONT_SMALL_ID) + 2;
  renderer.drawLine(x, ruleY, x + width, ruleY, true);
  return ruleY + kCardTitleGap;
}

void endStatCard(const GfxRenderer& renderer, int x, int width, int cardTop, int rowsEndY) {
  const int rectX = x - kCardOutsetX;
  const int rectWidth = width + kCardOutsetX * 2;
  const int height = (rowsEndY + kCardBottomPad) - cardTop;
  renderer.drawRoundedRect(rectX, cardTop, rectWidth, height, 1, Chrome::kCardRadius, true);
}

// Smaller-font, tighter-row variant of Chrome::drawStatRow (FONT_SMALL_ID + 16px rows instead of
// FONT_UI_10_ID + 20px), used only for SIGNALS/CAPTURE STATUS's own rows — CAPTURE STATUS's 5th
// row (Total Uptime) didn't fit the space this pair of cards has always had at the old row size,
// clipping visibly past the card's bottom edge. 5 rows at 16px is exactly 4 rows at 20px (80px
// either way), so this restores the pair's original total block height rather than growing it.
int drawCompactStatRow(const GfxRenderer& renderer, int y, const char* label, const char* value, int rightX,
                       int leftX) {
  constexpr int kRowHeight = 16;
  const int edge = rightX > 0 ? rightX : Chrome::contentRight(renderer);
  const int start = leftX > 0 ? leftX : Chrome::kMarginX;
  renderer.drawText(FONT_SMALL_ID, start, y, label);
  const int valueW = renderer.getTextWidth(FONT_SMALL_ID, value);
  renderer.drawText(FONT_SMALL_ID, edge - valueW, y, value);
  return y + kRowHeight;
}

// drawCenteredText() centers against the full screen width, which only works for the old
// full-width layout — the specimen name/mood text now sits under a left-aligned box, so it needs
// centering within just that column.
void drawCenteredTextIn(const GfxRenderer& renderer, int x, int width, int fontId, int y, const char* text,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int textW = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, x + (width - textW) / 2, y, text, true, style);
}

// Centered counterpart to drawBoldSmall() above, for the same reason: FONT_SMALL_ID has no bold
// face, so the PAUSED/ACTIVE tag below needs the double-draw trick too, not a real BOLD style.
void drawBoldSmallCenteredIn(const GfxRenderer& renderer, int x, int width, int y, const char* text) {
  const int textW = renderer.getTextWidth(FONT_SMALL_ID, text);
  const int textX = x + (width - textW) / 2;
  renderer.drawText(FONT_SMALL_ID, textX, y, text, true);
  renderer.drawText(FONT_SMALL_ID, textX + 1, y, text, true);
}

// Header-row EXP progress bar — deliberately long enough to read clearly at a glance but nowhere
// near the full header width, so it doesn't compete with the battery badge it sits beside for
// "most important thing in this row." A plain inset fillRect (not fillRoundedRect) for the filled
// portion avoids a "melted pill" look at low fill percentages, where a rounded fill narrower than
// its own corner radius would look wrong; the rounded outline around the whole track is enough to
// keep it reading as a pill overall.
void drawExpBar(const GfxRenderer& renderer, int x, int y, int width, int height, float progress) {
  renderer.drawRoundedRect(x, y, width, height, 1, height / 2, true);
  constexpr int kFillPad = 2;
  const int fillableWidth = width - kFillPad * 2;
  const int filledWidth = static_cast<int>(fillableWidth * std::clamp(progress, 0.0f, 1.0f));
  if (filledWidth > 0) {
    renderer.fillRect(x + kFillPad, y + kFillPad, filledWidth, height - kFillPad * 2, true);
  }
}

// Handshake-activity history — a plain "N ago" text list of the most recent captures, newest
// first, fed from RubyManager's persisted Unix-timestamp ring (see RubyState::handshakeTimestamps)
// rather than RecentSightings' shared 16-slot feed, which mixes in every AP/client/BLE sighting
// too and so pushes a handshake out of view again within moments in any normal RF environment —
// the opposite of "historical" for an event this rare. Persisted (not session-only) so captures
// from previous boots still show up here, not just this session's — real wall-clock time (unlike
// millis()) is the only timestamp that still means anything across a reboot. Text rather than a
// chart: handshakes are rare enough (and the list short enough) that raw ago-timestamps read more
// clearly at this scale than bars would. 0 is this ring's "slot never used" sentinel (see
// RubyState.h), so unused slots are simply skipped rather than counted as real captures; `next` is
// the ring's next-write index, needed to walk the buffer backwards from its most recently written
// slot regardless of whether it has wrapped yet.
void drawHandshakeHistoryList(const GfxRenderer& renderer, int x, int y, int width, int height,
                              const uint32_t* times, size_t capacity, uint8_t next) {
  constexpr int kLineHeight = 13;
  const int maxLines = std::max(1, height / kLineHeight);
  const time_t nowUnix = time(nullptr);

  int drawn = 0;
  for (size_t i = 0; i < capacity && drawn < maxLines; i++) {
    const size_t idx = (static_cast<size_t>(next) + capacity - 1 - i) % capacity;
    const uint32_t t = times[idx];
    if (t == 0) continue;  // unused slot -- ring isn't full of real captures yet
    const unsigned long agoSec =
        nowUnix > static_cast<time_t>(t) ? static_cast<unsigned long>(nowUnix - static_cast<time_t>(t)) : 0UL;
    char agoBuf[24];
    formatUptime(agoSec, agoBuf, sizeof(agoBuf));
    char line[40];
    snprintf(line, sizeof(line), "%s ago", agoBuf);
    renderer.drawText(FONT_SMALL_ID, x, y + drawn * kLineHeight,
                      renderer.truncatedText(FONT_SMALL_ID, line, width).c_str());
    drawn++;
  }
  if (drawn == 0) {
    renderer.drawText(FONT_SMALL_ID, x, y, "None yet.");
  }
}

// Live strip chart of RecentSightings' RSSI history — one bar per ring-buffer slot, oldest on the
// left, newest always anchored to the right edge (a slot with no sighting yet, when the ring isn't
// full, is just left blank rather than shifting everything right). Unlike Chrome::drawSignalBars'
// 4-level bucketing (built for a tiny fixed-size icon), this maps RSSI continuously across the
// chart's full height, since a real chart has the vertical room to show more than 4 steps of
// resolution. kRssiFloor/kRssiCeil are a typical WiFi/BLE dBm range, not a hard limit — a reading
// outside it just clamps to the shortest/tallest bar instead of drawing off-chart.
void drawSignalHistoryChart(const GfxRenderer& renderer, int x, int y, int width, int height) {
  constexpr int kRssiFloor = -90;
  constexpr int kRssiCeil = -30;
  constexpr int kBarGap = 2;
  const size_t capacity = RecentSightings::kCapacity;
  const size_t liveCount = recentSightings.count();
  const int barWidth =
      std::max(1, (width - kBarGap * static_cast<int>(capacity - 1)) / static_cast<int>(capacity));
  const int baseline = y + height;

  for (size_t slot = 0; slot < capacity; slot++) {
    const size_t indexFromNewest = capacity - 1 - slot;
    if (indexFromNewest >= liveCount) continue;  // ring not full yet -- leave this slot blank
    const auto& entry = recentSightings.at(indexFromNewest);
    const float norm =
        std::clamp(static_cast<float>(entry.rssi - kRssiFloor) / (kRssiCeil - kRssiFloor), 0.0f, 1.0f);
    const int barHeight = std::max(2, static_cast<int>(height * norm));
    const int barX = x + static_cast<int>(slot) * (barWidth + kBarGap);
    renderer.fillRect(barX, baseline - barHeight, barWidth, barHeight, true);
  }
}
}  // namespace

void DashboardActivity::onEnter() {
  Activity::onEnter();

  // Ruby's box is pinned top-left, a live "RECENT DEVICES" window sits beside it to the right at
  // the same height, name/mood text sits under the box, and SIGNALS + CAPTURE STATUS sit side by
  // side filling the rest of the width below that (see renderFull()) — landscape has much more
  // width to spend than the old portrait layout did, but a lot less height, so those two cards
  // are columns now instead of a full-width stack.
  rubyBoxSize = 230;
  rubyBoxX = Chrome::contentLeft();
  // Not Chrome::contentTop() — this screen's header is deliberately shorter than every other
  // screen's (see kDashboardHeaderHeight above), so its content starts higher up to match,
  // mirroring contentTop()'s own "+8" convention off of whatever the real divider position is.
  rubyBoxY = kDashboardHeaderHeight + 8;

  // Seed from the live counts rather than 0, so sightings that happened before this screen was
  // entered don't read as "new" and fire a speech-bubble reaction on the very first render.
  const auto& stats = SIGNAL_CATALOG.getStats();
  lastUniqueWifiAPs = stats.uniqueWifiAPs;
  lastUniqueWifiClients = stats.uniqueWifiClients;
  lastUniqueBleDevices = stats.uniqueBleDevices;

  pendingRenderKind = RenderKind::Full;
  lastFullRenderMs = 0;
  lastPeriodicFullRefreshMs = millis();
  requestUpdate();
}

void DashboardActivity::armSpeechBubble(const char* text) {
  speechBubbleActive = true;
  speechBubbleUntilMs = millis() + kSpeechBubbleMs;
  speechBubbleText = text;
}

void DashboardActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activityManager.goToSettings();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    toggleCapturePause();
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    activityManager.goToMaintenance();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    activityManager.goToDeviceList();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    activityManager.goToLogViewer();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    pendingRenderKind = RenderKind::Full;
    lastPeriodicFullRefreshMs = 0;  // force a FULL_REFRESH ghost-clear pass this cycle
    requestUpdate();
    return;
  }

  if (RUBY.consumeJustCapturedHandshake()) {
    handshakeBannerActive = true;
    handshakeBannerUntilMs = millis() + kHandshakeBannerMs;
    armSpeechBubble(RubyThoughts::speechForHandshake(static_cast<uint8_t>(millis())));
    // RubyManager::onSignalEvent() already records the persisted handshake-history timestamp
    // itself (see RubyManager.cpp) — not duplicated here, and not reliant on this screen being the
    // active one when a handshake actually lands.
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }
  if (handshakeBannerActive && millis() >= handshakeBannerUntilMs) {
    handshakeBannerActive = false;
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }

  uint8_t newLevel = 0;
  if (RUBY.consumeJustLeveledUp(newLevel)) {
    levelUpBannerActive = true;
    levelUpBannerUntilMs = millis() + kLevelUpBannerMs;
    levelUpBannerLevel = newLevel;
    armSpeechBubble(RubyThoughts::speechForLevelUp(static_cast<uint8_t>(millis())));
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }
  if (levelUpBannerActive && millis() >= levelUpBannerUntilMs) {
    levelUpBannerActive = false;
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }

  // DeauthDetector's own alertActive() is already time-windowed (see its class comment) — this
  // just notices the on/off transition so a redraw actually happens at both ends, the same way
  // the two consume-and-flag banners above do. The speech bubble only reacts to the rising edge
  // (alert starting) — there's nothing worth saying about it quietly ending.
  const bool deauthAlertNow = deauthDetector.alertActive();
  if (deauthAlertNow != lastDeauthAlertState) {
    if (deauthAlertNow) armSpeechBubble(RubyThoughts::speechForDeauthAlert(static_cast<uint8_t>(millis())));
    lastDeauthAlertState = deauthAlertNow;
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }

  if (speechBubbleActive && millis() >= speechBubbleUntilMs) {
    speechBubbleActive = false;
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }

  // A fresh unique sighting of any kind is also worth a quip — checked in a fixed AP/client/BLE
  // order so at most one fires per tick even if more than one count happens to jump in the same
  // loop() call; the others are still recorded as "last seen" below so they don't fire stale on a
  // later tick once this one's reaction has already cleared.
  const auto& freshStats = SIGNAL_CATALOG.getStats();
  LogRecordType newDeviceType = LogRecordType::WifiAp;
  bool sawNewDevice = false;
  if (freshStats.uniqueWifiAPs > lastUniqueWifiAPs) {
    newDeviceType = LogRecordType::WifiAp;
    sawNewDevice = true;
  } else if (freshStats.uniqueWifiClients > lastUniqueWifiClients) {
    newDeviceType = LogRecordType::WifiClient;
    sawNewDevice = true;
  } else if (freshStats.uniqueBleDevices > lastUniqueBleDevices) {
    newDeviceType = LogRecordType::BleDevice;
    sawNewDevice = true;
  }
  lastUniqueWifiAPs = freshStats.uniqueWifiAPs;
  lastUniqueWifiClients = freshStats.uniqueWifiClients;
  lastUniqueBleDevices = freshStats.uniqueBleDevices;
  if (sawNewDevice) {
    armSpeechBubble(RubyThoughts::speechForNewDevice(newDeviceType, static_cast<uint8_t>(millis())));
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }

  const unsigned long now = millis();
  if (now - lastFullRenderMs >= kFullRedrawIntervalMs) {
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
  } else if (now - lastPetRenderMs >= kPetRedrawIntervalMs && RUBY.animFrame() != lastAnimFrameRendered) {
    pendingRenderKind = RenderKind::RubyOnly;
    requestUpdate();
  }
}

void DashboardActivity::drawRubyPanel(bool withNoise) {
  const RubyExpression expression = effectiveExpression();
  const uint8_t frame = RUBY.animFrame();
  const char* reactionText = speechBubbleActive ? speechBubbleText : nullptr;
  RubySpriteRenderer::draw(renderer, rubyBoxX, rubyBoxY, rubyBoxSize,
                           withNoise ? expression : RubyExpression::SLEEPING, frame, RUBY.level(), reactionText);
  lastAnimFrameRendered = frame;
  lastPetRenderMs = millis();
}

void DashboardActivity::renderRubyBoxOnly() {
  drawRubyPanel(true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void DashboardActivity::renderFull() {
  renderer.clearScreen();

  // No title text here: "RUBY" now lives as a small chip pinned to the creature box's own
  // top-left corner (see RubySpriteRenderer::draw) instead of the shared header bar, so the
  // header on this one screen is just the divider rule, the battery badge, and (since the blank
  // title leaves the rest of the row free) an EXP progress bar right beside it.
  const int battery = powerManager.getBatteryPercentage();
  const int headerLeftContentRight = Chrome::drawHeader(renderer, "", battery, kDashboardHeaderHeight);
  constexpr int kExpBarGap = 10;
  constexpr int kExpBarWidth = 260;  // long enough to read at a glance, well short of the full row
  constexpr int kExpBarHeight = 16;  // matches the battery badge's own pill height
  const int expBarX = headerLeftContentRight + kExpBarGap;
  drawExpBar(renderer, expBarX, 5, kExpBarWidth, kExpBarHeight, RUBY.expProgress());

  // "<earned this level>/<total EXP this level spans> EXP" beside the bar — the second number is
  // the fixed size of the current level's bracket (RubyConfig::kLevelThresholds delta), not a
  // countdown, so it stays put while the first climbs toward it. Same vertical centering formula
  // the battery badge above uses for its own percentage text.
  char expLabelBuf[24];
  uint32_t expInto = 0, expNeededForLevel = 0;
  RUBY.expIntoLevel(expInto, expNeededForLevel);
  if (expNeededForLevel == 0) {
    snprintf(expLabelBuf, sizeof(expLabelBuf), "MAX");
  } else {
    snprintf(expLabelBuf, sizeof(expLabelBuf), "%lu/%lu EXP", static_cast<unsigned long>(expInto),
             static_cast<unsigned long>(expNeededForLevel));
  }
  const int expLabelY = 5 + (kExpBarHeight - renderer.getLineHeight(FONT_SMALL_ID)) / 2;
  renderer.drawText(FONT_SMALL_ID, expBarX + kExpBarWidth + kExpBarGap, expLabelY, expLabelBuf);

  // Top row: Ruby's box pinned top-left, then two side-by-side windows to the right at the same
  // height — a narrower "RECENT DEVICES" list (fewer entries, but a full 3 lines each) and a new
  // "SIGNAL HISTORY" strip chart, so signal strength has a real dynamic visual instead of only the
  // per-entry dBm number to its left.
  const int deviceColX = rubyBoxX + rubyBoxSize + kColumnGap;
  const int deviceColWidth = Chrome::contentRight(renderer) - deviceColX;
  const int topRowBottom = rubyBoxY + rubyBoxSize;
  const int devicesWidth = (deviceColWidth - kColumnGap) / 2;
  const int chartX = deviceColX + devicesWidth + kColumnGap;
  const int chartWidth = deviceColWidth - devicesWidth - kColumnGap;

  // Mood text sits under the box, centered within the box's own column (not the whole screen —
  // the box is left-aligned now, not centered). Computed here, before RECENT DEVICES/SIGNAL
  // HISTORY below, so those two cards know how far down they can extend: they used to stop at the
  // box's own bottom edge, matching a "CAPTURE SETTINGS" card that used to sit beside this Mood
  // block (removed entirely as redundant with the Settings screen) — now they extend down to match
  // the Mood column's own height instead, rather than leaving that space empty.
  //
  // Line spacing here used to be hardcoded guesses (20px/16px) rather than the fonts' actual
  // metrics — close enough most of the time, but FONT_UI_12_ID BOLD's real glyph height runs
  // taller than the guessed 20px, so "Mood" and its value ("Curious", etc.) visibly overlapped.
  // Using renderer.getLineHeight() per font fixes that at its source instead of just padding the
  // guess further.
  const RubyExpression expression = effectiveExpression();
  int moodY = topRowBottom + 6;
  drawCenteredTextIn(renderer, rubyBoxX, rubyBoxSize, FONT_UI_12_ID, moodY, "Mood", EpdFontFamily::BOLD);
  moodY += renderer.getLineHeight(FONT_UI_12_ID);
  drawCenteredTextIn(renderer, rubyBoxX, rubyBoxSize, FONT_SMALL_ID, moodY, RubyBehavior::expressionLabel(expression));
  moodY += renderer.getLineHeight(FONT_SMALL_ID);

  // Always draw one of these two, rather than only "-- PAUSED --" when paused, so the mood
  // column's height (and therefore where RECENT DEVICES/SIGNAL HISTORY and the SIGNALS/CAPTURE
  // STATUS row below land) stays identical between the two states — otherwise everything would
  // shift up by one line's height every time capture resumed.
  drawBoldSmallCenteredIn(renderer, rubyBoxX, rubyBoxSize, moodY, captureIsPaused() ? "-- PAUSED --" : "-- ACTIVE --");
  moodY += renderer.getLineHeight(FONT_SMALL_ID);

  int cardTop = rubyBoxY;
  const int deviceRowsBottom = moodY - kCardBottomPad;  // extend down to match the Mood column

  int y = beginStatCard(renderer, deviceColX, devicesWidth, cardTop, "RECENT DEVICES");
  const size_t liveCount = recentSightings.count();
  if (liveCount == 0) {
    renderer.drawText(FONT_SMALL_ID, deviceColX, y, "Nothing heard yet.");
  } else {
    // Tight, purpose-sized line spacing instead of GfxRenderer::getLineHeight() (~25px for
    // FONT_SMALL_ID — that's the font's paragraph line-spacing, not a per-row stride). A single
    // column now that the chart occupies the other half of this row — three lines each (type+MAC,
    // signal bars + RSSI + time-ago, device name) so fewer entries fit, but each one carries the
    // same detail Device Log's expanded view does, including a vendor-name fallback for entries
    // with no advertised label.
    constexpr int kLineHeight = 13;
    constexpr int kLineGap = 2;
    constexpr int kEntryGap = 6;
    constexpr int kEntryHeight = kLineHeight * 3 + kLineGap * 2 + kEntryGap;
    const int rowsPerColumn = std::max(1, (deviceRowsBottom - y) / kEntryHeight);
    const size_t maxVisible = std::min(liveCount, static_cast<size_t>(rowsPerColumn));

    for (size_t i = 0; i < maxVisible; i++) {
      const int entryY = y + static_cast<int>(i) * kEntryHeight;

      const auto& entry = recentSightings.at(i);
      char macBuf[18];
      formatMac(entry.mac, macBuf);
      char agoBuf[16];
      formatAgo(entry.seenAtMs, agoBuf, sizeof(agoBuf));
      char line1[32];
      snprintf(line1, sizeof(line1), "%s %s", logRecordTypeCompactName(entry.type), macBuf);
      renderer.drawText(FONT_SMALL_ID, deviceColX, entryY, line1);
      const int line2Y = entryY + kLineHeight + kLineGap;
      Chrome::drawSignalBars(renderer, deviceColX, line2Y, entry.rssi);
      char line2[32];
      snprintf(line2, sizeof(line2), "%d dBm  %s", entry.rssi, agoBuf);
      renderer.drawText(FONT_SMALL_ID, deviceColX + Chrome::kSignalBarsWidth + 4, line2Y, line2);

      const char* label = entry.label;
      char vendorBuf[32];
      if (!label[0] && lookupVendorOui(entry.mac, vendorBuf, sizeof(vendorBuf))) {
        label = vendorBuf;
      }
      const std::string line3 =
          renderer.truncatedText(FONT_SMALL_ID, label[0] ? label : "(no name)", devicesWidth);
      renderer.drawText(FONT_SMALL_ID, deviceColX, entryY + (kLineHeight + kLineGap) * 2, line3.c_str());
    }
  }
  endStatCard(renderer, deviceColX, devicesWidth, cardTop, deviceRowsBottom);

  const int chartTop = beginStatCard(renderer, chartX, chartWidth, cardTop, "SIGNAL HISTORY");
  // Two stacked sections rather than one chart alone in the card (RSSI by itself rarely needs the
  // whole card's height to read clearly, leaving a lot of otherwise-idle space). HANDSHAKES is a
  // plain "N ago" text list from a dedicated timestamp ring, not RecentSightings' shared,
  // AP/client/BLE-dominated 16-slot feed — handshakes are rare enough that ordinary traffic would
  // push one out of that shared feed within moments, the opposite of "historical" for an event
  // this infrequent — while SIGNAL stays a recent-observations RSSI bar chart below it.
  constexpr int kSubGap = 4;
  const int subLabelHeight = renderer.getLineHeight(FONT_SMALL_ID);
  const int trackHeight = (deviceRowsBottom - chartTop - kSubGap) / 2;

  drawBoldSmall(renderer, chartX, chartTop, "HANDSHAKES");
  const int handshakeGraphY = chartTop + subLabelHeight;
  drawHandshakeHistoryList(renderer, chartX, handshakeGraphY, chartWidth, trackHeight - subLabelHeight,
                           RUBY.handshakeTimestamps(), kHandshakeHistoryCapacity, RUBY.handshakeHistoryNext());

  const int signalLabelY = chartTop + trackHeight + kSubGap;
  drawBoldSmall(renderer, chartX, signalLabelY, "SIGNAL");
  const int signalGraphY = signalLabelY + subLabelHeight;
  drawSignalHistoryChart(renderer, chartX, signalGraphY, chartWidth, deviceRowsBottom - signalGraphY);
  endStatCard(renderer, chartX, chartWidth, cardTop, deviceRowsBottom);

  // Bottom half: SIGNALS + CAPTURE STATUS. Landscape's 792x528 canvas has much less spare height
  // below the top row than the old portrait canvas did, but a lot more spare width — so these
  // two cards sit side by side in their own columns instead of stacked full-width. Both cards'
  // borders extend all the way to the bottom of the content area, rather than shrink-wrapping
  // tightly around their rows the way a single full-width card used to.
  y = moodY + kCardGap;
  const int bottomY = y;
  const int colWidth = (Chrome::contentRight(renderer) - Chrome::contentLeft() - kColumnGap) / 2;
  const int leftColX = Chrome::contentLeft();
  const int rightColX = leftColX + colWidth + kColumnGap;
  const int columnBottom = Chrome::contentBottom(renderer) - kCardBottomPad;

  int leftY = beginStatCard(renderer, leftColX, colWidth, bottomY, "SIGNALS");
  const auto& stats = SIGNAL_CATALOG.getStats();
  char buf[32];
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueWifiAPs));
  leftY = drawCompactStatRow(renderer, leftY, "Unique access points", buf, leftColX + colWidth, leftColX);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueWifiClients));
  leftY = drawCompactStatRow(renderer, leftY, "Unique WiFi clients", buf, leftColX + colWidth, leftColX);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueBleDevices));
  leftY = drawCompactStatRow(renderer, leftY, "Unique BLE devices", buf, leftColX + colWidth, leftColX);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.handshakesCaptured));
  leftY = drawCompactStatRow(renderer, leftY, "Handshakes captured", buf, leftColX + colWidth, leftColX);

  // Slightly larger than the rows above — Chrome::drawStatRow's own FONT_UI_10_ID, rather than
  // the smaller drawCompactStatRow font every other SIGNALS row uses — a deliberate emphasis for
  // the one row here that names an actual device rather than just a count. Sits in what would
  // otherwise be idle space below the 4 counter rows, since this card's border already extends
  // all the way to columnBottom regardless of how much the rows above it actually fill.
  char lastHandshakeBuf[18];
  const MacAddress& lastHandshakeBssid = RUBY.lastHandshakeBssid();
  if (lastHandshakeBssid == MacAddress{}) {
    snprintf(lastHandshakeBuf, sizeof(lastHandshakeBuf), "none yet");
  } else {
    formatMac(lastHandshakeBssid, lastHandshakeBuf);
  }
  leftY = Chrome::drawStatRow(renderer, leftY + 4, "Last Hand Shook:", lastHandshakeBuf, false, leftColX + colWidth,
                              leftColX);
  endStatCard(renderer, leftColX, colWidth, bottomY, columnBottom);

  // Top-anchored, hugging the title exactly like SIGNALS does — this card used to center its rows
  // vertically instead, which left it visibly less tight to the top than SIGNALS beside it.
  int rightY = beginStatCard(renderer, rightColX, colWidth, bottomY, "CAPTURE STATUS");

  // One combined "Capture" row instead of separate WiFi monitor/BLE scan rows — names only
  // whichever capture path(s) are actually active ("none" if both are off) rather than two rows
  // that each spell out their own ON/OFF state.
  char captureBuf[32];
  if (wifiSniffer.isRunning() && bleScanner.isRunning()) {
    snprintf(captureBuf, sizeof(captureBuf), "WiFi ch%u, BLE", wifiSniffer.currentChannel());
  } else if (wifiSniffer.isRunning()) {
    snprintf(captureBuf, sizeof(captureBuf), "WiFi ch%u", wifiSniffer.currentChannel());
  } else if (bleScanner.isRunning()) {
    snprintf(captureBuf, sizeof(captureBuf), "BLE");
  } else {
    snprintf(captureBuf, sizeof(captureBuf), "none");
  }
  rightY = drawCompactStatRow(renderer, rightY, "Capture", captureBuf, rightColX + colWidth, rightColX);

  char sizeBuf[24];
  formatBytes(encryptedLog.currentFileSizeBytes(), sizeBuf, sizeof(sizeBuf));
  rightY = drawCompactStatRow(renderer, rightY, "Encrypted log", sizeBuf, rightColX + colWidth, rightColX);

  char uptimeBuf[24];
  formatUptime(millis() / 1000, uptimeBuf, sizeof(uptimeBuf));
  rightY = drawCompactStatRow(renderer, rightY, "Uptime this session", uptimeBuf, rightColX + colWidth, rightColX);

  // APP_STATE.totalCaptureSeconds only accumulates at each sleep entry (see enterDeepSleep() in
  // main.cpp), so it doesn't yet include this still-running session's own time — added on here so
  // the figure shown is always current, not stale as of the last time the device slept.
  char totalUptimeBuf[24];
  formatUptime(APP_STATE.totalCaptureSeconds + millis() / 1000, totalUptimeBuf, sizeof(totalUptimeBuf));
  rightY = drawCompactStatRow(renderer, rightY, "Total Uptime", totalUptimeBuf, rightColX + colWidth, rightColX);
  endStatCard(renderer, rightColX, colWidth, bottomY, columnBottom);

  drawRubyPanel(true);

  // At most one banner at a time — priority order is "most urgent to know about right now": a
  // nearby deauth attack in progress, then a level-up (bigger, rarer news than an ordinary
  // handshake, and often triggered by the very same handshake — kExpPerHandshake is a round
  // multiple of every level threshold's step, so the two frequently coincide), then Ruby's own
  // handshake capture. A de-prioritized banner's own timer still runs out quietly in the
  // background even while hidden behind a higher-priority one, rather than getting reset or
  // extended — simplest to reason about, and nothing here depends on exactly when it expires.
  char levelUpBannerBuf[24];
  char networkLabelBuf[40];
  char deauthBannerBuf[64];
  char handshakeBannerBuf[64];
  const char* bannerText = nullptr;
  if (deauthDetector.alertActive()) {
    // The network being targeted, not who's sending the deauth frames — a spoofed deauth's
    // source address is meaningless anyway, but the BSSID (addr3) says which network is actually
    // under attack, which is the useful thing to know at a glance.
    formatNetworkLabel(deauthDetector.lastTargetBssid(), networkLabelBuf, sizeof(networkLabelBuf));
    snprintf(deauthBannerBuf, sizeof(deauthBannerBuf), "DEAUTH NEARBY: %s", networkLabelBuf);
    bannerText = deauthBannerBuf;
  } else if (levelUpBannerActive) {
    snprintf(levelUpBannerBuf, sizeof(levelUpBannerBuf), "LEVEL UP -> Lv.%u", levelUpBannerLevel);
    bannerText = levelUpBannerBuf;
  } else if (handshakeBannerActive) {
    formatNetworkLabel(RUBY.lastHandshakeBssid(), networkLabelBuf, sizeof(networkLabelBuf));
    snprintf(handshakeBannerBuf, sizeof(handshakeBannerBuf), "HANDSHAKE CAPTURED: %s", networkLabelBuf);
    bannerText = handshakeBannerBuf;
  }
  if (bannerText) {
    constexpr int kBannerH = 28;
    const int bannerY = (renderer.getScreenHeight() - kBannerH) / 2;
    renderer.fillRect(0, bannerY, renderer.getScreenWidth(), kBannerH, true);
    const int textY = bannerY + (kBannerH - renderer.getLineHeight(FONT_UI_10_ID)) / 2;
    // Text drawn white-on-black by inverting: draw as non-ink over the filled band.
    renderer.drawCenteredText(FONT_UI_10_ID, textY, bannerText, false, EpdFontFamily::BOLD);
  }

  Chrome::drawFooterHints(renderer, "Refresh", "Settings", captureIsPaused() ? "Resume" : "Pause", "Export");

  const unsigned long now = millis();
  const bool dueForGhostClear =
      SETTINGS.fullRefreshIntervalMin > 0 &&
      (now - lastPeriodicFullRefreshMs) >= static_cast<unsigned long>(SETTINGS.fullRefreshIntervalMin) * 60000UL;
  renderer.displayBuffer(dueForGhostClear ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  if (dueForGhostClear) lastPeriodicFullRefreshMs = now;

  lastFullRenderMs = now;
}

void DashboardActivity::render(RenderLock&&) {
  if (pendingRenderKind == RenderKind::Full) {
    renderFull();
  } else {
    renderRubyBoxOnly();
  }
}
