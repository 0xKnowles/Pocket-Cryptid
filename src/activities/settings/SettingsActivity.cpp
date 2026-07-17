#include "SettingsActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>

#include "ApHistory.h"
#include "BleScanner.h"
#include "DeauthEngine.h"
#include "EncryptedLog.h"
#include "RubySettings.h"
#include "TargetList.h"
#include "WifiSniffer.h"
#include "activities/devices/ApHistoryActivity.h"
#include "activities/targets/TargetPickerActivity.h"
#include "fontIds.h"
#include "ui/Chrome.h"

namespace {
constexpr uint16_t kWifiDwellStepMs = 50;
constexpr uint16_t kWifiDwellMinMs = 100;
constexpr uint16_t kWifiDwellMaxMs = 2000;
constexpr uint8_t kGhostClearSteps[] = {0, 5, 10, 15, 20, 30, 60};
constexpr size_t kGhostClearStepCount = sizeof(kGhostClearSteps) / sizeof(kGhostClearSteps[0]);
constexpr unsigned long kWipeArmWindowMs = 5000;
constexpr int kPowerShortPressActionCount = static_cast<int>(PowerShortPressAction::PauseRuby) + 1;

const char* powerShortPressActionLabel(PowerShortPressAction action) {
  switch (action) {
    case PowerShortPressAction::ScreenRefresh:
      return "Refresh";
    case PowerShortPressAction::Screenshot:
      return "Screenshot";
    case PowerShortPressAction::PauseRuby:
      return "Pause";
  }
  return "?";
}
}  // namespace

void SettingsActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  wipeArmed = false;
  showingKey = false;
  requestUpdate();
}

void SettingsActivity::adjustSelected(int direction) {
  wipeArmed = false;
  showingKey = false;

  switch (selected) {
    case RowWifiEnabled:
      SETTINGS.wifiSniffEnabled = !SETTINGS.wifiSniffEnabled;
      if (SETTINGS.wifiSniffEnabled) {
        wifiSniffer.start(WifiSniffer::kAllChannels, WifiSniffer::kAllChannelsCount, SETTINGS.wifiChannelDwellMs);
      } else {
        wifiSniffer.stop();
      }
      break;
    case RowBleEnabled:
      SETTINGS.bleSniffEnabled = !SETTINGS.bleSniffEnabled;
      if (SETTINGS.bleSniffEnabled) {
        bleScanner.start();
      } else {
        bleScanner.stop();
      }
      break;
    case RowWifiDwell: {
      const int next = static_cast<int>(SETTINGS.wifiChannelDwellMs) + direction * kWifiDwellStepMs;
      SETTINGS.wifiChannelDwellMs =
          static_cast<uint16_t>(std::clamp(next, static_cast<int>(kWifiDwellMinMs), static_cast<int>(kWifiDwellMaxMs)));
      break;
    }
    case RowGhostClearInterval: {
      size_t idx = 0;
      for (size_t i = 0; i < kGhostClearStepCount; i++) {
        if (kGhostClearSteps[i] == SETTINGS.fullRefreshIntervalMin) idx = i;
      }
      const int next = std::clamp(static_cast<int>(idx) + direction, 0, static_cast<int>(kGhostClearStepCount) - 1);
      SETTINGS.fullRefreshIntervalMin = kGhostClearSteps[next];
      break;
    }
    case RowPowerShortPress: {
      const int next = std::clamp(static_cast<int>(SETTINGS.powerShortPressAction) + direction, 0,
                                  kPowerShortPressActionCount - 1);
      SETTINGS.powerShortPressAction = static_cast<PowerShortPressAction>(next);
      break;
    }
    case RowRawCapture:
      SETTINGS.rawHandshakeCaptureEnabled = !SETTINGS.rawHandshakeCaptureEnabled;
      wifiSniffer.setRawCaptureEnabled(SETTINGS.rawHandshakeCaptureEnabled);
      // Turning raw capture off must also stop active deauth — see DeauthEngine's class comment
      // on why it refuses to run without something capturing the handshake it forces.
      deauthEngine.setEnabled(SETTINGS.activeDeauthEnabled && SETTINGS.rawHandshakeCaptureEnabled);
      break;
    case RowActiveDeauth:
      if (!SETTINGS.activeDeauthEnabled && !SETTINGS.rawHandshakeCaptureEnabled) {
        // Refuse to arm active mode without raw capture on to catch what it forces — leave off.
        break;
      }
      SETTINGS.activeDeauthEnabled = !SETTINGS.activeDeauthEnabled;
      deauthEngine.setEnabled(SETTINGS.activeDeauthEnabled && SETTINGS.rawHandshakeCaptureEnabled);
      break;
    default:
      break;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void SettingsActivity::activateSelected() {
  switch (selected) {
    case RowWifiEnabled:
    case RowBleEnabled:
    case RowRawCapture:
    case RowActiveDeauth:
      adjustSelected(1);
      return;
    case RowWhitelist:
      activityManager.pushActivity(
          std::make_unique<TargetPickerActivity>(renderer, mappedInput, TargetListKind::Whitelist));
      return;
    case RowBlacklist:
      activityManager.pushActivity(
          std::make_unique<TargetPickerActivity>(renderer, mappedInput, TargetListKind::Blacklist));
      return;
    case RowApHistory:
      activityManager.pushActivity(std::make_unique<ApHistoryActivity>(renderer, mappedInput));
      return;
    case RowRevealKey:
      showingKey = encryptedLog.revealDecryptionKeyHex(revealedKeyHex, sizeof(revealedKeyHex));
      wipeArmed = false;
      requestUpdate();
      return;
    case RowWipeLog:
      if (wipeArmed && millis() < wipeArmedUntilMs) {
        encryptedLog.wipeAndResetKey();
        wipeArmed = false;
      } else {
        wipeArmed = true;
        wipeArmedUntilMs = millis() + kWipeArmWindowMs;
      }
      showingKey = false;
      requestUpdate();
      return;
    default:
      return;
  }
}

void SettingsActivity::loop() {
  if (wipeArmed && millis() >= wipeArmedUntilMs) {
    wipeArmed = false;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    selected = (selected - 1 + RowCount) % RowCount;
    showingKey = false;
    wipeArmed = false;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selected = (selected + 1) % RowCount;
    showingKey = false;
    wipeArmed = false;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    adjustSelected(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    adjustSelected(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }
}

void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, "SETTINGS");

  constexpr int kRowHeight = 32;
  int y = Chrome::contentTop();
  char valueBuf[24];

  auto drawRow = [&](int index, const char* label, const char* value) {
    const bool isSelected = index == selected;
    if (isSelected) {
      // An outlined "button" box rather than a solid inverted fill — reads as "this row is
      // selected/pressable" without the heaviest-thing-on-the-screen look a full-black bar has.
      Chrome::drawSelectionHighlight(renderer, Chrome::contentLeft() - 4, y - 3,
                                     Chrome::contentRight(renderer) - Chrome::contentLeft() + 8, kRowHeight - 6);
    }
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, label, true, EpdFontFamily::REGULAR);
    const int valueW = renderer.getTextWidth(FONT_UI_10_ID, value);
    renderer.drawText(FONT_UI_10_ID, Chrome::contentRight(renderer) - valueW, y, value, true);
  };

  drawRow(RowWifiEnabled, "WiFi monitor", SETTINGS.wifiSniffEnabled ? "ON" : "OFF");
  y += kRowHeight;
  drawRow(RowBleEnabled, "BLE passive scan", SETTINGS.bleSniffEnabled ? "ON" : "OFF");
  y += kRowHeight;
  snprintf(valueBuf, sizeof(valueBuf), "%u ms/channel", SETTINGS.wifiChannelDwellMs);
  drawRow(RowWifiDwell, "WiFi channel dwell", valueBuf);
  y += kRowHeight;
  if (SETTINGS.fullRefreshIntervalMin == 0) {
    drawRow(RowGhostClearInterval, "Ghost-clear refresh", "off");
  } else {
    snprintf(valueBuf, sizeof(valueBuf), "every %u min", SETTINGS.fullRefreshIntervalMin);
    drawRow(RowGhostClearInterval, "Ghost-clear refresh", valueBuf);
  }
  y += kRowHeight;
  drawRow(RowPowerShortPress, "Power button (tap)", powerShortPressActionLabel(SETTINGS.powerShortPressAction));
  y += kRowHeight;
  drawRow(RowRawCapture, "Raw handshake capture", SETTINGS.rawHandshakeCaptureEnabled ? "ON" : "OFF");
  y += kRowHeight;
  drawRow(RowActiveDeauth, "Active deauth", SETTINGS.activeDeauthEnabled ? "ON" : "OFF");
  y += kRowHeight;
  char countBuf[16];
  snprintf(countBuf, sizeof(countBuf), "%u saved >", static_cast<unsigned>(targetList.count(TargetListKind::Whitelist)));
  drawRow(RowWhitelist, "Whitelist", countBuf);
  y += kRowHeight;
  snprintf(countBuf, sizeof(countBuf), "%u saved >", static_cast<unsigned>(targetList.count(TargetListKind::Blacklist)));
  drawRow(RowBlacklist, "Blacklist", countBuf);
  y += kRowHeight;
  snprintf(countBuf, sizeof(countBuf), "%u known >", static_cast<unsigned>(apHistory.count()));
  drawRow(RowApHistory, "AP History", countBuf);
  y += kRowHeight;
  drawRow(RowRevealKey, "Reveal log key", "show >");
  y += kRowHeight;
  drawRow(RowWipeLog, "Wipe encrypted log", wipeArmed ? "confirm?" : "erase >");
  y += kRowHeight + 8;

  if (showingKey) {
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, "AES-256 key (hex) — write this down:", true,
                      EpdFontFamily::BOLD);
    y += 20;
    // Split into two lines: 64 hex chars is too wide for one row at this font size.
    char line1[33];
    char line2[33];
    strncpy(line1, revealedKeyHex, 32);
    line1[32] = '\0';
    strncpy(line2, revealedKeyHex + 32, 32);
    line2[32] = '\0';
    renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line1);
    y += 16;
    renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line2);
    y += 24;
    renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y,
                      "Use with scripts/decrypt_log.py to read the log on a PC.");
  } else if (wipeArmed) {
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y,
                      "Press Confirm again to permanently erase the log and its key.", true, EpdFontFamily::BOLD);
  } else if (selected == RowPowerShortPress) {
    const auto lines = renderer.wrappedText(
        FONT_SMALL_ID,
        "Left/Right cycles what a short Power tap does: Refresh, Screenshot (saves to "
        "/.ruby/screenshots), or Pause (same as Dashboard's Pause button, from any screen). "
        "Holding Power always sleeps the device.",
        Chrome::contentRight(renderer) - Chrome::contentLeft(), 8);
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (const auto& line : lines) {
      if (y + lineHeight > Chrome::contentBottom(renderer)) break;  // hard stop — never draw past the screen's floor
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line.c_str());
      y += lineHeight;
    }
  } else if (selected == RowRawCapture) {
    const auto lines = renderer.wrappedText(
        FONT_SMALL_ID,
        "Saves WPA handshake frames unencrypted to /.ruby/pcap for cracking-tool auditing "
        "(hashcat/hcxpcapngtool) of networks you own. Unlike the encrypted log, these files are "
        "plaintext on the SD card.",
        Chrome::contentRight(renderer) - Chrome::contentLeft(), 8);
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (const auto& line : lines) {
      if (y + lineHeight > Chrome::contentBottom(renderer)) break;
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line.c_str());
      y += lineHeight;
    }
  } else if (selected == RowActiveDeauth) {
    const auto lines = renderer.wrappedText(
        FONT_SMALL_ID,
        "Transmits real deauthentication frames at BSSIDs your target list allows, to force a "
        "handshake instead of waiting for one. Only use against networks you own or are "
        "explicitly authorized to test — doing this to networks you don't is illegal in most "
        "places. Requires raw handshake capture to be on; won't arm without it.",
        Chrome::contentRight(renderer) - Chrome::contentLeft(), 8);
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (const auto& line : lines) {
      if (y + lineHeight > Chrome::contentBottom(renderer)) break;
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line.c_str());
      y += lineHeight;
    }
  } else if (selected == RowWhitelist || selected == RowBlacklist) {
    const auto lines = renderer.wrappedText(
        FONT_SMALL_ID,
        "Confirm opens a live network scan — press Confirm on a network to add or remove it. "
        "Whitelist non-empty: active deauth only attacks those networks. Whitelist empty: "
        "attacks everything except the blacklist. Both empty (default): attacks nothing.",
        Chrome::contentRight(renderer) - Chrome::contentLeft(), 8);
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (const auto& line : lines) {
      if (y + lineHeight > Chrome::contentBottom(renderer)) break;
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line.c_str());
      y += lineHeight;
    }
  }

  Chrome::drawFooterHints(renderer, "Home", "Select", "-", "+");
  renderer.displayBuffer();
}
