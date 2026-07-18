#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>

#include "BleScanner.h"
#include "DeauthEngine.h"
#include "EncryptedLog.h"
#include "RubySettings.h"
#include "SignalCatalog.h"
#include "TargetList.h"
#include "WifiSniffer.h"
#include "activities/targets/TargetPickerActivity.h"
#include "fontIds.h"
#include "ruby/RubyManager.h"
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
  statsResetArmed = false;
  showingKey = false;
  firstRenderSinceEnter = true;
  requestUpdate();
}

void SettingsActivity::adjustSelected(int direction) {
  wipeArmed = false;
  statsResetArmed = false;
  showingKey = false;

  switch (selected) {
    case RowWifiEnabled:
      SETTINGS.wifiSniffEnabled = !SETTINGS.wifiSniffEnabled;
      if (SETTINGS.wifiSniffEnabled) {
        const uint8_t* wifiChannels;
        size_t wifiChannelCount;
        WifiSniffer::channelPlanFor(SETTINGS.wifiChannelScope, wifiChannels, wifiChannelCount);
        wifiSniffer.start(wifiChannels, wifiChannelCount, SETTINGS.wifiChannelDwellMs,
                           SETTINGS.rawHandshakeCaptureEnabled);
      } else {
        wifiSniffer.stop();
      }
      break;
    case RowBleEnabled:
      if (!SETTINGS.bleSniffEnabled && SETTINGS.rawHandshakeCaptureEnabled) {
        // Real-hardware testing found BLE running alongside raw handshake capture reliably
        // fragments the DMA-capable pool EncryptedLog's hardware AES needs badly enough to
        // crash-loop (see CHANGELOG/main.cpp's crash-loop guard) — refuse rather than trigger
        // that. Turn off raw capture first if BLE is what's wanted.
        break;
      }
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
    case RowWifiChannelScope: {
      // 0 = all 13 channels, 1-13 = a single locked channel — cycles 0..13..0. Takes effect the
      // next time WiFi monitor mode (re)starts (toggle it off/on, or reboot), same as
      // RowWifiDwell's dwell-time setting right above — not applied live to an already-running
      // WifiSniffer.
      const int next = std::clamp(static_cast<int>(SETTINGS.wifiChannelScope) + direction, 0, 13);
      SETTINGS.wifiChannelScope = static_cast<uint8_t>(next);
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
      if (!SETTINGS.rawHandshakeCaptureEnabled && bleScanner.isRunning()) {
        // Same DMA-pool reasoning as RowBleEnabled above, checked against BLE's actual live state
        // rather than the setting — if BLE is on but never actually started (see BleScanner's
        // isInitialized() check), it isn't consuming the memory that makes this combination risky.
        break;
      }
      SETTINGS.rawHandshakeCaptureEnabled = !SETTINGS.rawHandshakeCaptureEnabled;
      wifiSniffer.setRawCaptureEnabled(SETTINGS.rawHandshakeCaptureEnabled);
      // Turning raw capture off must also stop active deauth — see DeauthEngine's class comment
      // on why it refuses to run without something capturing the handshake it forces. Clearing
      // the setting too (not just disabling the live engine) matters: without this, the Active
      // deauth row kept showing ON — SETTINGS.activeDeauthEnabled never actually changed — even
      // though deauthEngine itself had gone silently inactive, and turning raw capture back on
      // later would silently resurrect it with no fresh confirmation from this row at all.
      if (!SETTINGS.rawHandshakeCaptureEnabled) SETTINGS.activeDeauthEnabled = false;
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
    case RowRevealKey:
      showingKey = encryptedLog.revealDecryptionKeyHex(revealedKeyHex, sizeof(revealedKeyHex));
      wipeArmed = false;
      statsResetArmed = false;
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
      statsResetArmed = false;
      showingKey = false;
      requestUpdate();
      return;
    case RowResetStats:
      if (statsResetArmed && millis() < statsResetArmedUntilMs) {
        SIGNAL_CATALOG.resetStats();
        // Reset alongside it, not independently — otherwise a MAC that already leveled Ruby up
        // could re-award EXP the moment its dedup ring entry is cleared and it's seen again,
        // silently overcounting the badge relative to what the dashboard's own counters now show.
        RUBY.resetExp();
        statsResetArmed = false;
      } else {
        statsResetArmed = true;
        statsResetArmedUntilMs = millis() + kWipeArmWindowMs;
      }
      wipeArmed = false;
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
  if (statsResetArmed && millis() >= statsResetArmedUntilMs) {
    statsResetArmed = false;
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
    statsResetArmed = false;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selected = (selected + 1) % RowCount;
    showingKey = false;
    wipeArmed = false;
    statsResetArmed = false;
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
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, label, true, EpdFontFamily::BOLD);
    const int valueW = renderer.getTextWidth(FONT_UI_10_ID, value);
    renderer.drawText(FONT_UI_10_ID, Chrome::contentRight(renderer) - valueW, y, value, true);
  };

  drawRow(RowWifiEnabled, "WiFi monitor", SETTINGS.wifiSniffEnabled ? "ON" : "OFF");
  y += kRowHeight;
  // Distinguish "the setting is on but the radio actually failed to start" from a plain "ON" —
  // NimBLE's controller init can fail silently (see BleScanner::begin()'s isInitialized() check),
  // most often from memory contention with WiFi monitor capture already running, and this row is
  // the one place an owner without a serial monitor attached would ever see that happened.
  drawRow(RowBleEnabled, "BLE passive scan",
          SETTINGS.bleSniffEnabled ? (bleScanner.isRunning() ? "ON" : "FAILED") : "OFF");
  y += kRowHeight;
  snprintf(valueBuf, sizeof(valueBuf), "%u ms/channel", SETTINGS.wifiChannelDwellMs);
  drawRow(RowWifiDwell, "WiFi channel dwell", valueBuf);
  y += kRowHeight;
  if (SETTINGS.wifiChannelScope == 0) {
    drawRow(RowWifiChannelScope, "WiFi channel scope", "All (1-13)");
  } else {
    snprintf(valueBuf, sizeof(valueBuf), "Ch %u only", SETTINGS.wifiChannelScope);
    drawRow(RowWifiChannelScope, "WiFi channel scope", valueBuf);
  }
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
  drawRow(RowRevealKey, "Reveal log key", "show >");
  y += kRowHeight;
  drawRow(RowWipeLog, "Wipe encrypted log", wipeArmed ? "confirm?" : "erase >");
  y += kRowHeight;
  drawRow(RowResetStats, "Reset signal stats", statsResetArmed ? "confirm?" : "reset >");
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
  } else if (statsResetArmed) {
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y,
                      "Press Confirm again to zero SIGNALS, forget seen devices, and reset Ruby's Level/EXP.", true,
                      EpdFontFamily::BOLD);
  } else if (selected == RowBleEnabled) {
    const auto lines = renderer.wrappedText(
        FONT_SMALL_ID,
        "Off by default: running alongside raw handshake capture fragments this device's memory "
        "badly enough to crash-loop, so turning this on refuses while raw capture is on (turn that "
        "off first). FAILED means NimBLE itself couldn't initialize; ON means it's actually "
        "scanning.",
        Chrome::contentRight(renderer) - Chrome::contentLeft(), 8);
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (const auto& line : lines) {
      if (y + lineHeight > Chrome::contentBottom(renderer)) break;  // hard stop — never draw past the screen's floor
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line.c_str());
      y += lineHeight;
    }
  } else if (selected == RowWifiChannelScope) {
    const auto lines = renderer.wrappedText(
        FONT_SMALL_ID,
        "All hops across 13 channels, so any one is only listened to ~1/13th of the time. Locking "
        "to a single known channel (see it in Whitelist/Blacklist's live scan) raises that to "
        "100% — the biggest lever for actually catching a handshake. Takes effect next time WiFi "
        "monitor restarts.",
        Chrome::contentRight(renderer) - Chrome::contentLeft(), 8);
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (const auto& line : lines) {
      if (y + lineHeight > Chrome::contentBottom(renderer)) break;  // hard stop — never draw past the screen's floor
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line.c_str());
      y += lineHeight;
    }
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
        "plaintext on the SD card. Refuses to turn on while BLE passive scan is running — turn "
        "that off first.",
        Chrome::contentRight(renderer) - Chrome::contentLeft(), 8);
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (const auto& line : lines) {
      if (y + lineHeight > Chrome::contentBottom(renderer)) break;  // hard stop — never draw past the screen's floor
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
      if (y + lineHeight > Chrome::contentBottom(renderer)) break;  // hard stop — never draw past the screen's floor
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
      if (y + lineHeight > Chrome::contentBottom(renderer)) break;  // hard stop — never draw past the screen's floor
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line.c_str());
      y += lineHeight;
    }
  }

  Chrome::drawFooterHints(renderer, "Home", "Select", "-", "+");
  renderer.displayBuffer(firstRenderSinceEnter ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  firstRenderSinceEnter = false;
}
