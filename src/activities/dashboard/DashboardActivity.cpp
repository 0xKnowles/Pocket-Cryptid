#include "DashboardActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>

#include <cstddef>
#include <cstdio>

#include "BleScanner.h"
#include "EncryptedLog.h"
#include "RubyAppState.h"
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

constexpr int kCardOutsetX = 6;   // border stroke sits this far outside Chrome's text margin
constexpr int kCardTitleGap = 6;  // space between the title rule and the first row
constexpr int kCardTopPad = 6;
constexpr int kCardBottomPad = 8;

// Draws a bold section caption + rule at `y`. Returns the y the first Chrome::drawStatRow() call
// should land at. Pairs with endStatCard(), which closes the rounded outline once the caller
// knows where the last row ended — the two are separate calls (rather than one that takes a row
// count) because every card here has a different row count driven by runtime state.
int beginStatCard(const GfxRenderer& renderer, int y, const char* title) {
  renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y + kCardTopPad, title, true, EpdFontFamily::BOLD);
  const int ruleY = y + kCardTopPad + renderer.getLineHeight(FONT_SMALL_ID) + 2;
  renderer.drawLine(Chrome::contentLeft(), ruleY, Chrome::contentRight(renderer), ruleY, true);
  return ruleY + kCardTitleGap;
}

void endStatCard(const GfxRenderer& renderer, int cardTop, int rowsEndY) {
  const int x = Chrome::contentLeft() - kCardOutsetX;
  const int width = Chrome::contentRight(renderer) - Chrome::contentLeft() + kCardOutsetX * 2;
  const int height = (rowsEndY + kCardBottomPad) - cardTop;
  renderer.drawRoundedRect(x, cardTop, width, height, 1, Chrome::kCardRadius, true);
}
}  // namespace

void DashboardActivity::onEnter() {
  Activity::onEnter();

  // Portrait layout: the creature gets a prominent "specimen card" box centered at the top of
  // the screen, with its name/expression line directly beneath it, then the RF stats fill the
  // rest of the tall screen full-width below that. See DashboardActivity.h for why this is
  // portrait and not landscape — the physical buttons are laid out for this orientation.
  rubyBoxSize = 200;
  rubyBoxX = (renderer.getScreenWidth() - rubyBoxSize) / 2;
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

  const int battery = powerManager.getBatteryPercentage();
  Chrome::drawHeader(renderer, "RUBY", battery);

  // Specimen card: box, then name/expression directly beneath it.
  const RubyState& state = RUBY.getState();
  const RubyExpression expression = RUBY.currentExpression(false);
  int y = rubyBoxY + rubyBoxSize + 14;
  renderer.drawCenteredText(FONT_UI_12_ID, y, state.designation, true, EpdFontFamily::BOLD);
  y += 22;
  renderer.drawCenteredText(FONT_SMALL_ID, y, RubyBehavior::expressionLabel(expression));
  y += 24;

  const size_t loreTotal = RubyManager::loreEntryCount();
  char loreBuf[32];
  snprintf(loreBuf, sizeof(loreBuf), "%u/%zu lore entries unlocked", state.unlockedLoreCount, loreTotal);
  renderer.drawCenteredText(FONT_SMALL_ID, y, loreBuf);
  y += 24;

  int cardTop = y;
  y = beginStatCard(renderer, y, "SIGNALS");
  const auto& stats = SIGNAL_CATALOG.getStats();
  char buf[32];
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueWifiAPs));
  y = Chrome::drawStatRow(renderer, y, "Unique access points", buf);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueWifiClients));
  y = Chrome::drawStatRow(renderer, y, "Unique WiFi clients", buf);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.uniqueBleDevices));
  y = Chrome::drawStatRow(renderer, y, "Unique BLE devices", buf);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.handshakesCaptured));
  y = Chrome::drawStatRow(renderer, y, "Handshakes captured", buf);
  endStatCard(renderer, cardTop, y);
  y += 18;

  cardTop = y;
  y = beginStatCard(renderer, y, "CAPTURE STATUS");
  if (wifiSniffer.isRunning()) {
    char chbuf[16];
    snprintf(chbuf, sizeof(chbuf), "ch %u", wifiSniffer.currentChannel());
    y = Chrome::drawStatRow(renderer, y, "WiFi monitor", chbuf);
  } else {
    y = Chrome::drawStatRow(renderer, y, "WiFi monitor", "off");
  }
  y = Chrome::drawStatRow(renderer, y, "BLE scan", bleScanner.isRunning() ? "passive" : "off");

  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.wifiFramesObserved));
  y = Chrome::drawStatRow(renderer, y, "WiFi frames seen", buf);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.bleAdvertisementsObserved));
  y = Chrome::drawStatRow(renderer, y, "BLE adverts seen", buf);

  char sizeBuf[24];
  formatBytes(encryptedLog.currentFileSizeBytes(), sizeBuf, sizeof(sizeBuf));
  y = Chrome::drawStatRow(renderer, y, "Encrypted log (today)", sizeBuf);

  char uptimeBuf[24];
  formatUptime(millis(), uptimeBuf, sizeof(uptimeBuf));
  y = Chrome::drawStatRow(renderer, y, "Uptime this session", uptimeBuf);

  char sessionBuf[24];
  snprintf(sessionBuf, sizeof(sessionBuf), "#%lu", static_cast<unsigned long>(APP_STATE.bootCount));
  y = Chrome::drawStatRow(renderer, y, "Session", sessionBuf);
  endStatCard(renderer, cardTop, y);
  y += 18;

  renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, "Up: recent devices    Down: decrypt log");

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
