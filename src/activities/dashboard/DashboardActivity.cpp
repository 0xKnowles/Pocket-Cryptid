#include "DashboardActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>

#include "BleScanner.h"
#include "CaptureControl.h"
#include "DeauthDetector.h"
#include "EncryptedLog.h"
#include "LogRecord.h"
#include "RecentSightings.h"
#include "RubySettings.h"
#include "SignalCatalog.h"
#include "WifiSniffer.h"
#include "fontIds.h"
#include "ruby/RubyBehavior.h"
#include "ruby/RubyManager.h"
#include "ruby/RubySpriteRenderer.h"
#include "ui/Chrome.h"

namespace {
constexpr unsigned long kFullRedrawIntervalMs = 5000;
constexpr unsigned long kPetRedrawIntervalMs = 1200;
constexpr unsigned long kHandshakeBannerMs = 4000;

void formatUptime(unsigned long ms, char* out, size_t outSize) {
  const unsigned long totalSec = ms / 1000;
  const unsigned long h = totalSec / 3600;
  const unsigned long m = (totalSec % 3600) / 60;
  const unsigned long s = totalSec % 60;
  if (h > 0) {
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
  rubyBoxY = Chrome::contentTop();

  pendingRenderKind = RenderKind::Full;
  lastFullRenderMs = 0;
  lastPeriodicFullRefreshMs = millis();
  requestUpdate();
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

  // DeauthDetector's own alertActive() is already time-windowed (see its class comment) — this
  // just notices the on/off transition so a redraw actually happens at both ends, the same way
  // the two consume-and-flag banners above do.
  const bool deauthAlertNow = deauthDetector.alertActive();
  if (deauthAlertNow != lastDeauthAlertState) {
    lastDeauthAlertState = deauthAlertNow;
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
  RubySpriteRenderer::draw(renderer, rubyBoxX, rubyBoxY, rubyBoxSize,
                           withNoise ? expression : RubyExpression::SLEEPING, frame);
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
  // header on this one screen is just the divider rule plus the battery badge.
  const int battery = powerManager.getBatteryPercentage();
  Chrome::drawHeader(renderer, "", battery);

  // Top row: Ruby's box pinned top-left, "RECENT DEVICES" window beside it to the right at the
  // same height.
  const int deviceColX = rubyBoxX + rubyBoxSize + kColumnGap;
  const int deviceColWidth = Chrome::contentRight(renderer) - deviceColX;
  const int topRowBottom = rubyBoxY + rubyBoxSize;

  int cardTop = rubyBoxY;
  int y = beginStatCard(renderer, deviceColX, deviceColWidth, cardTop, "RECENT DEVICES");
  const size_t liveCount = recentSightings.count();
  const int deviceRowsBottom = topRowBottom - kCardBottomPad;  // don't overrun the box's height
  if (liveCount == 0) {
    renderer.drawText(FONT_SMALL_ID, deviceColX, y, "Nothing heard yet.");
  } else {
    // Tight, purpose-sized line spacing instead of GfxRenderer::getLineHeight() (~25px for
    // FONT_SMALL_ID — that's the font's paragraph line-spacing, not a per-row stride, and was
    // only fitting 3-4 entries here). This box is also wide enough in landscape to lay entries
    // out in columns rather than one long single-file list — kEntryColWidth is sized for the
    // worst case ("STA 12:34:56:78:9A:BC" at this font's fixed pitch) so a 2-column grid fits
    // cleanly without truncating anything.
    constexpr int kLineHeight = 13;
    constexpr int kLineGap = 2;
    constexpr int kEntryGap = 6;
    constexpr int kEntryHeight = kLineHeight * 2 + kLineGap + kEntryGap;
    constexpr int kEntryColWidth = 224;
    const int rowsPerColumn = std::max(1, (deviceRowsBottom - y) / kEntryHeight);
    const int columnCount = std::max(1, deviceColWidth / (kEntryColWidth + kColumnGap));
    const size_t maxVisible = std::min(liveCount, static_cast<size_t>(rowsPerColumn * columnCount));

    for (size_t i = 0; i < maxVisible; i++) {
      const size_t col = i / static_cast<size_t>(rowsPerColumn);
      const size_t row = i % static_cast<size_t>(rowsPerColumn);
      const int entryX = deviceColX + static_cast<int>(col) * (kEntryColWidth + kColumnGap);
      const int entryY = y + static_cast<int>(row) * kEntryHeight;

      const auto& entry = recentSightings.at(i);
      char macBuf[18];
      formatMac(entry.mac, macBuf);
      char agoBuf[16];
      formatAgo(entry.seenAtMs, agoBuf, sizeof(agoBuf));
      char line1[32];
      snprintf(line1, sizeof(line1), "%s %s", logRecordTypeCompactName(entry.type), macBuf);
      renderer.drawText(FONT_SMALL_ID, entryX, entryY, line1);
      char line2[32];
      snprintf(line2, sizeof(line2), "  %d dBm  %s", entry.rssi, agoBuf);
      renderer.drawText(FONT_SMALL_ID, entryX, entryY + kLineHeight + kLineGap, line2);
    }
  }
  endStatCard(renderer, deviceColX, deviceColWidth, cardTop, deviceRowsBottom);

  // Mood text sits under the box, centered within the box's own column (not the whole screen —
  // the box is left-aligned now, not centered). This used to show the auto-generated per-device
  // "SPECIMEN-XXXX" designation (RubyManager::begin(), still used in log messages) as a bold
  // headline, but on the dashboard itself that read as unexplained noise rather than useful
  // status — a plain "Mood" label over the actual mood value is clearer.
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
  // column's height (and therefore where std::max(moodY, infoY) below lands) stays identical
  // between the two states — otherwise everything from the SIGNALS/CAPTURE STATUS row down would
  // shift up by one line's height every time capture resumed.
  drawBoldSmallCenteredIn(renderer, rubyBoxX, rubyBoxSize, moodY, captureIsPaused() ? "-- PAUSED --" : "-- ACTIVE --");
  moodY += renderer.getLineHeight(FONT_SMALL_ID);

  // Same row, to the right of the mood block: this was dead whitespace before (the RECENT DEVICES
  // card above it ends at topRowBottom, and SIGNALS/CAPTURE STATUS don't start until bottomY) —
  // put it to use surfacing the two opt-in capture settings that most change what this device is
  // actually doing, so the owner doesn't have to go into Settings to check.
  int infoY = topRowBottom + 6;
  infoY = Chrome::drawStatRow(renderer, infoY, "Handshake Capture",
                              SETTINGS.rawHandshakeCaptureEnabled ? "ON" : "OFF", false,
                              deviceColX + deviceColWidth, deviceColX);
  // Replaces the old "DeAuth: ON/OFF" setting readout — active deauth's own transmit path is
  // confirmed rejected by the WiFi driver on this hardware (see DeauthEngine's class comment), so
  // that ON/OFF told you nothing useful about what's actually happening in the air. This instead
  // surfaces DeauthDetector's passive count of deauth/disassoc frames from *anyone* nearby —
  // information that's still real regardless of whether this device's own attempts transmit.
  char deauthBuf[16];
  if (deauthDetector.alertActive()) {
    snprintf(deauthBuf, sizeof(deauthBuf), "ALERT");
  } else {
    const uint32_t deauthTotal = deauthDetector.totalDeauthFrames() + deauthDetector.totalDisassocFrames();
    if (deauthTotal == 0) {
      snprintf(deauthBuf, sizeof(deauthBuf), "none");
    } else {
      snprintf(deauthBuf, sizeof(deauthBuf), "%lu seen", static_cast<unsigned long>(deauthTotal));
    }
  }
  infoY = Chrome::drawStatRow(renderer, infoY, "Nearby DeAuth", deauthBuf, false, deviceColX + deviceColWidth,
                              deviceColX);

  // Bottom half: SIGNALS + CAPTURE STATUS. Landscape's 792x528 canvas has much less spare height
  // below the top row than the old portrait canvas did, but a lot more spare width — so these
  // two cards sit side by side in their own columns instead of stacked full-width. Every other
  // proportion on this screen (box size, top row, mood text) is unchanged. Both cards' borders
  // extend all the way to the bottom of the content area (there's room to spare below 4 rows of
  // stats now), rather than shrink-wrapping tightly around their rows the way a single full-width
  // card used to.
  y = std::max(moodY, infoY) + kCardGap;
  const int bottomY = y;
  const int colWidth = (Chrome::contentRight(renderer) - Chrome::contentLeft() - kColumnGap) / 2;
  const int leftColX = Chrome::contentLeft();
  const int rightColX = leftColX + colWidth + kColumnGap;
  const int columnBottom = Chrome::contentBottom(renderer) - kCardBottomPad;

  int leftY = beginStatCard(renderer, leftColX, colWidth, bottomY, "SIGNALS");
  const auto& stats = SIGNAL_CATALOG.getStats();
  char buf[32];
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueWifiAPs));
  leftY = Chrome::drawStatRow(renderer, leftY, "Unique access points", buf, false, leftColX + colWidth, leftColX);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueWifiClients));
  leftY = Chrome::drawStatRow(renderer, leftY, "Unique WiFi clients", buf, false, leftColX + colWidth, leftColX);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueBleDevices));
  leftY = Chrome::drawStatRow(renderer, leftY, "Unique BLE devices", buf, false, leftColX + colWidth, leftColX);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.handshakesCaptured));
  leftY = Chrome::drawStatRow(renderer, leftY, "Handshakes captured", buf, false, leftColX + colWidth, leftColX);
  endStatCard(renderer, leftColX, colWidth, bottomY, columnBottom);

  // CAPTURE STATUS's 4 rows are vertically centered within the card instead of hugging the title
  // the way SIGNALS' do — asked for explicitly, and it also reads better here since these rows
  // (WiFi monitor/BLE scan/log size/uptime) are a much more varied mix of value lengths than
  // SIGNALS' four numbers, so a centered block looks more deliberate than a top-anchored one.
  constexpr int kCaptureStatusRowCount = 4;
  constexpr int kStatRowHeight = 20;  // mirrors Chrome::drawStatRow's internal row height
  const int captureRowsTop = beginStatCard(renderer, rightColX, colWidth, bottomY, "CAPTURE STATUS");
  const int captureRowsBlockHeight = kCaptureStatusRowCount * kStatRowHeight;
  const int captureRowsAvailable = columnBottom - captureRowsTop;
  int rightY = captureRowsTop + std::max(0, (captureRowsAvailable - captureRowsBlockHeight) / 2);
  if (wifiSniffer.isRunning()) {
    char chbuf[16];
    snprintf(chbuf, sizeof(chbuf), "ch %u", wifiSniffer.currentChannel());
    rightY = Chrome::drawStatRow(renderer, rightY, "WiFi monitor", chbuf, false, rightColX + colWidth, rightColX);
  } else {
    rightY = Chrome::drawStatRow(renderer, rightY, "WiFi monitor", "off", false, rightColX + colWidth, rightColX);
  }
  rightY = Chrome::drawStatRow(renderer, rightY, "BLE scan", bleScanner.isRunning() ? "passive" : "off", false,
                               rightColX + colWidth, rightColX);

  char sizeBuf[24];
  formatBytes(encryptedLog.currentFileSizeBytes(), sizeBuf, sizeof(sizeBuf));
  rightY = Chrome::drawStatRow(renderer, rightY, "Encrypted log", sizeBuf, false, rightColX + colWidth,
                               rightColX);

  char uptimeBuf[24];
  formatUptime(millis(), uptimeBuf, sizeof(uptimeBuf));
  rightY = Chrome::drawStatRow(renderer, rightY, "Uptime this session", uptimeBuf, false, rightColX + colWidth,
                               rightColX);
  endStatCard(renderer, rightColX, colWidth, bottomY, columnBottom);

  drawRubyPanel(true);

  // At most one banner at a time — priority order is "most urgent to know about right now": a
  // nearby deauth attack in progress, then Ruby's own (positive, less time-sensitive) handshake
  // capture.
  const char* bannerText = nullptr;
  if (deauthDetector.alertActive()) {
    bannerText = "DEAUTH ACTIVITY NEARBY";
  } else if (handshakeBannerActive) {
    bannerText = "HANDSHAKE CAPTURED";
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
