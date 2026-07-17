#include "DashboardActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>

#include <cstddef>
#include <cstdio>

#include "BleScanner.h"
#include "CryptidAppState.h"
#include "CryptidSettings.h"
#include "EncryptedLog.h"
#include "SignalCatalog.h"
#include "WifiSniffer.h"
#include "cryptid/CryptidEvolution.h"
#include "cryptid/CryptidManager.h"
#include "cryptid/CryptidSpriteRenderer.h"
#include "fontIds.h"
#include "ui/Chrome.h"

namespace {
constexpr unsigned long kFullRedrawIntervalMs = 5000;
constexpr unsigned long kPetRedrawIntervalMs = 1200;
constexpr unsigned long kEvolutionBannerMs = 4000;

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
}  // namespace

void DashboardActivity::onEnter() {
  Activity::onEnter();

  // Portrait layout: the creature gets a prominent "specimen card" box centered at the top of
  // the screen, with its name/stage/mood/XP bar directly beneath it, then the RF stats fill the
  // rest of the tall screen full-width below that. See DashboardActivity.h for why this is
  // portrait and not landscape — the physical buttons are laid out for this orientation.
  cryptidBoxSize = 200;
  cryptidBoxX = (renderer.getScreenWidth() - cryptidBoxSize) / 2;
  cryptidBoxY = Chrome::contentTop();

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
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    pendingRenderKind = RenderKind::Full;
    lastPeriodicFullRefreshMs = 0;  // force a FULL_REFRESH ghost-clear pass this cycle
    requestUpdate();
    return;
  }

  if (CRYPTID.consumeJustEvolved()) {
    evolutionBannerActive = true;
    evolutionBannerUntilMs = millis() + kEvolutionBannerMs;
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }
  if (evolutionBannerActive && millis() >= evolutionBannerUntilMs) {
    evolutionBannerActive = false;
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
    return;
  }

  const unsigned long now = millis();
  if (now - lastFullRenderMs >= kFullRedrawIntervalMs) {
    pendingRenderKind = RenderKind::Full;
    requestUpdate();
  } else if (now - lastPetRenderMs >= kPetRedrawIntervalMs && CRYPTID.animFrame() != lastAnimFrameRendered) {
    pendingRenderKind = RenderKind::CryptidOnly;
    requestUpdate();
  }
}

void DashboardActivity::drawCryptidPanel(bool withNoise) {
  const CryptidMood mood = CRYPTID.currentMood(false);
  const uint8_t frame = CRYPTID.animFrame();
  CryptidSpriteRenderer::draw(renderer, cryptidBoxX, cryptidBoxY, cryptidBoxSize, CRYPTID.getState().stage,
                             withNoise ? mood : CryptidMood::ASLEEP, frame);
  lastAnimFrameRendered = frame;
  lastPetRenderMs = millis();
}

void DashboardActivity::renderCryptidBoxOnly() {
  drawCryptidPanel(true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void DashboardActivity::renderFull() {
  renderer.clearScreen();

  const int battery = powerManager.getBatteryPercentage();
  Chrome::drawHeader(renderer, "POCKET CRYPTID", battery);

  // Specimen card: box, then name/stage/mood/XP bar centered directly beneath it.
  const CryptidState& state = CRYPTID.getState();
  const CryptidMood mood = CRYPTID.currentMood(false);
  int y = cryptidBoxY + cryptidBoxSize + 14;
  renderer.drawCenteredText(FONT_UI_12_ID, y, state.designation, true, EpdFontFamily::BOLD);
  y += 22;
  // Plain ASCII hyphen, not an em dash: no guarantee the Inter subset baked into
  // builtinFonts covers U+2014, and this isn't worth risking a missing-glyph fallback over.
  char stageMood[48];
  snprintf(stageMood, sizeof(stageMood), "%s - %s", CryptidEvolution::stageName(state.stage),
           CryptidEvolution::moodLabel(mood));
  renderer.drawCenteredText(FONT_SMALL_ID, y, stageMood);
  y += 20;

  const uint32_t need = CryptidEvolution::xpNeededForNextStage(state.xp);
  constexpr int barW = 240;
  constexpr int barH = 8;
  const int barX = (renderer.getScreenWidth() - barW) / 2;
  renderer.drawRect(barX, y, barW, barH, true);
  if (need > 0) {
    const uint32_t into = CryptidEvolution::xpIntoCurrentStage(state.xp);
    const int fillW = static_cast<int>((static_cast<uint64_t>(into) * (barW - 2)) / need);
    if (fillW > 0) renderer.fillRect(barX + 1, y + 1, fillW, barH - 2, true);
  } else {
    renderer.fillRect(barX + 1, y + 1, barW - 2, barH - 2, true);  // APEX: full bar
  }
  y += barH + 22;

  renderer.drawLine(Chrome::contentLeft(), y, Chrome::contentRight(renderer), y, true);
  y += 18;

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

  y += 10;
  renderer.drawLine(Chrome::contentLeft(), y, Chrome::contentRight(renderer), y, true);
  y += 16;

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
  Chrome::drawStatRow(renderer, y, "Session", sessionBuf);

  drawCryptidPanel(true);

  if (evolutionBannerActive) {
    constexpr int kBannerH = 28;
    const int bannerY = (renderer.getScreenHeight() - kBannerH) / 2;
    renderer.fillRect(0, bannerY, renderer.getScreenWidth(), kBannerH, true);
    char evoBuf[48];
    snprintf(evoBuf, sizeof(evoBuf), "IT HAS CHANGED: %s", CryptidEvolution::stageName(state.stage));
    const int textY = bannerY + (kBannerH - renderer.getLineHeight(FONT_UI_10_ID)) / 2;
    // Text drawn white-on-black by inverting: draw as non-ink over the filled band.
    renderer.drawCenteredText(FONT_UI_10_ID, textY, evoBuf, false, EpdFontFamily::BOLD);
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
    renderCryptidBoxOnly();
  }
}
