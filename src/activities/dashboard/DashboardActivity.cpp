#include "DashboardActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>

#include <cstddef>
#include <cstdio>

#include "BleScanner.h"
#include "EncryptedLog.h"
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

constexpr int kCardOutsetX = 6;   // border stroke sits this far outside the card's text column
constexpr int kCardTitleGap = 6;  // space between the title rule and the first row
constexpr int kCardTopPad = 6;
constexpr int kCardBottomPad = 8;
constexpr int kCardGap = 10;    // vertical gap between stacked cards
constexpr int kColumnGap = 14;  // horizontal gap between the top row's two columns

// Draws a bold section caption + rule spanning [x, x + width) at `y`. Returns the y the first row
// should land at. Pairs with endStatCard(), which closes the rounded outline once the caller
// knows where the last row ended — the two are separate calls (rather than one that takes a row
// count) because every card here has a different row count driven by runtime state. Takes an
// explicit x/width (rather than always spanning the full content width) so the same card chrome
// works for both the full-width SIGNALS/CAPTURE STATUS cards and the narrower top-right device
// column.
int beginStatCard(const GfxRenderer& renderer, int x, int width, int y, const char* title) {
  renderer.drawText(FONT_SMALL_ID, x, y + kCardTopPad, title, true, EpdFontFamily::BOLD);
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
    activityManager.goToLore();
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
  const RubyExpression expression = RUBY.currentExpression(false);
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
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (size_t i = 0; i < liveCount; i++) {
      if (y + lineHeight * 2 + 6 > deviceRowsBottom) break;  // out of room in this box's height
      const auto& entry = recentSightings.at(i);
      char macBuf[18];
      formatMac(entry.mac, macBuf);
      char agoBuf[16];
      formatAgo(entry.seenAtMs, agoBuf, sizeof(agoBuf));
      char line1[32];
      snprintf(line1, sizeof(line1), "%-6s %s", logRecordTypeShortName(entry.type), macBuf);
      renderer.drawText(FONT_SMALL_ID, deviceColX, y, line1);
      y += lineHeight + 2;
      char line2[32];
      snprintf(line2, sizeof(line2), "  %d dBm  %s", entry.rssi, agoBuf);
      renderer.drawText(FONT_SMALL_ID, deviceColX, y, line2);
      y += lineHeight + 6;
    }
  }
  endStatCard(renderer, deviceColX, deviceColWidth, cardTop, deviceRowsBottom);

  // Mood/lore text sits under the box, centered within the box's own column (not the whole
  // screen — the box is left-aligned now, not centered). This used to show the auto-generated
  // per-device "SPECIMEN-XXXX" designation (RubyManager::begin(), still used in log messages) as
  // a bold headline, but on the dashboard itself that read as unexplained noise rather than
  // useful status — a plain "Mood" label over the actual mood value is clearer.
  const RubyState& state = RUBY.getState();
  const RubyExpression expression = RUBY.currentExpression(false);
  y = topRowBottom + 6;
  drawCenteredTextIn(renderer, rubyBoxX, rubyBoxSize, FONT_UI_12_ID, y, "Mood", EpdFontFamily::BOLD);
  y += 20;
  drawCenteredTextIn(renderer, rubyBoxX, rubyBoxSize, FONT_SMALL_ID, y, RubyBehavior::expressionLabel(expression));
  y += 16;

  const size_t loreTotal = RubyManager::loreEntryCount();
  char loreBuf[32];
  snprintf(loreBuf, sizeof(loreBuf), "%u/%zu lore unlocked", state.unlockedLoreCount, loreTotal);
  drawCenteredTextIn(renderer, rubyBoxX, rubyBoxSize, FONT_SMALL_ID, y, loreBuf);
  y += 16;

  // Bottom half: SIGNALS + CAPTURE STATUS. Landscape's 792x528 canvas has much less spare height
  // below the top row than the old portrait canvas did, but a lot more spare width — so these
  // two cards sit side by side in their own columns instead of stacked full-width. Every other
  // proportion on this screen (box size, top row, mood text) is unchanged.
  y += kCardGap;
  const int bottomY = y;
  const int colWidth = (Chrome::contentRight(renderer) - Chrome::contentLeft() - kColumnGap) / 2;
  const int leftColX = Chrome::contentLeft();
  const int rightColX = leftColX + colWidth + kColumnGap;

  int leftY = beginStatCard(renderer, leftColX, colWidth, bottomY, "SIGNALS");
  const auto& stats = SIGNAL_CATALOG.getStats();
  char buf[32];
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueWifiAPs));
  leftY = Chrome::drawStatRow(renderer, leftY, "Unique access points", buf, false, leftColX + colWidth);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueWifiClients));
  leftY = Chrome::drawStatRow(renderer, leftY, "Unique WiFi clients", buf, false, leftColX + colWidth);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueBleDevices));
  leftY = Chrome::drawStatRow(renderer, leftY, "Unique BLE devices", buf, false, leftColX + colWidth);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.handshakesCaptured));
  leftY = Chrome::drawStatRow(renderer, leftY, "Handshakes captured", buf, false, leftColX + colWidth);
  endStatCard(renderer, leftColX, colWidth, bottomY, leftY);

  int rightY = beginStatCard(renderer, rightColX, colWidth, bottomY, "CAPTURE STATUS");
  if (wifiSniffer.isRunning()) {
    char chbuf[16];
    snprintf(chbuf, sizeof(chbuf), "ch %u", wifiSniffer.currentChannel());
    rightY = Chrome::drawStatRow(renderer, rightY, "WiFi monitor", chbuf, false, rightColX + colWidth);
  } else {
    rightY = Chrome::drawStatRow(renderer, rightY, "WiFi monitor", "off", false, rightColX + colWidth);
  }
  rightY = Chrome::drawStatRow(renderer, rightY, "BLE scan", bleScanner.isRunning() ? "passive" : "off", false,
                               rightColX + colWidth);

  char sizeBuf[24];
  formatBytes(encryptedLog.currentFileSizeBytes(), sizeBuf, sizeof(sizeBuf));
  rightY = Chrome::drawStatRow(renderer, rightY, "Encrypted log (today)", sizeBuf, false, rightColX + colWidth);

  char uptimeBuf[24];
  formatUptime(millis(), uptimeBuf, sizeof(uptimeBuf));
  rightY = Chrome::drawStatRow(renderer, rightY, "Uptime this session", uptimeBuf, false, rightColX + colWidth);
  endStatCard(renderer, rightColX, colWidth, bottomY, rightY);

  drawRubyPanel(true);

  if (handshakeBannerActive) {
    constexpr int kBannerH = 28;
    const int bannerY = (renderer.getScreenHeight() - kBannerH) / 2;
    renderer.fillRect(0, bannerY, renderer.getScreenWidth(), kBannerH, true);
    const int textY = bannerY + (kBannerH - renderer.getLineHeight(FONT_UI_10_ID)) / 2;
    // Text drawn white-on-black by inverting: draw as non-ink over the filled band.
    renderer.drawCenteredText(FONT_UI_10_ID, textY, "HANDSHAKE CAPTURED", false, EpdFontFamily::BOLD);
  }

  Chrome::drawFooterHints(renderer, "Refresh", "Settings", "Lore", "Export");

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
