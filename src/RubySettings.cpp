#include "RubySettings.h"

void RubySettings::toJson(JsonDocument& doc) const {
  doc["wifiSniffEnabled"] = wifiSniffEnabled;
  doc["bleSniffEnabled"] = bleSniffEnabled;
  doc["rawHandshakeCaptureEnabled"] = rawHandshakeCaptureEnabled;
  doc["activeDeauthEnabled"] = activeDeauthEnabled;
  doc["wifiChannelDwellMs"] = wifiChannelDwellMs;
  doc["wifiChannelScope"] = wifiChannelScope;
  doc["clockUtcOffsetQ"] = clockUtcOffsetQ;
  doc["fullRefreshIntervalMin"] = fullRefreshIntervalMin;
  doc["powerShortPressAction"] = static_cast<uint8_t>(powerShortPressAction);
}

bool RubySettings::fromJson(JsonVariantConst doc) {
  wifiSniffEnabled = doc["wifiSniffEnabled"] | true;
  bleSniffEnabled = doc["bleSniffEnabled"] | true;
  rawHandshakeCaptureEnabled = doc["rawHandshakeCaptureEnabled"] | false;
  activeDeauthEnabled = doc["activeDeauthEnabled"] | false;
  wifiChannelDwellMs = doc["wifiChannelDwellMs"] | 300;
  // Anything outside 1-13 (including a corrupted/hand-edited file's stray byte) is treated as "all
  // channels" by WifiSniffer::channelPlanFor already, so no extra bounds-checking is needed here.
  wifiChannelScope = doc["wifiChannelScope"] | 0;
  clockUtcOffsetQ = doc["clockUtcOffsetQ"] | 48;
  fullRefreshIntervalMin = doc["fullRefreshIntervalMin"] | 20;
  const uint8_t storedAction =
      doc["powerShortPressAction"] | static_cast<uint8_t>(PowerShortPressAction::ScreenRefresh);
  // Bounds-check rather than trust the file blindly — a hand-edited or corrupted settings.json
  // could hold any byte value, and casting that straight into the enum would be undefined
  // behavior the first time something switches on it.
  powerShortPressAction = storedAction <= static_cast<uint8_t>(PowerShortPressAction::PauseRuby)
                              ? static_cast<PowerShortPressAction>(storedAction)
                              : PowerShortPressAction::ScreenRefresh;
  return true;
}
