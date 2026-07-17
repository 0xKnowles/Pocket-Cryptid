#include "RubySettings.h"

void RubySettings::toJson(JsonDocument& doc) const {
  doc["wifiSniffEnabled"] = wifiSniffEnabled;
  doc["bleSniffEnabled"] = bleSniffEnabled;
  doc["rawHandshakeCaptureEnabled"] = rawHandshakeCaptureEnabled;
  doc["activeDeauthEnabled"] = activeDeauthEnabled;
  doc["wifiChannelDwellMs"] = wifiChannelDwellMs;
  doc["clockUtcOffsetQ"] = clockUtcOffsetQ;
  doc["fullRefreshIntervalMin"] = fullRefreshIntervalMin;
}

bool RubySettings::fromJson(JsonVariantConst doc) {
  wifiSniffEnabled = doc["wifiSniffEnabled"] | true;
  bleSniffEnabled = doc["bleSniffEnabled"] | true;
  rawHandshakeCaptureEnabled = doc["rawHandshakeCaptureEnabled"] | false;
  activeDeauthEnabled = doc["activeDeauthEnabled"] | false;
  wifiChannelDwellMs = doc["wifiChannelDwellMs"] | 300;
  clockUtcOffsetQ = doc["clockUtcOffsetQ"] | 48;
  fullRefreshIntervalMin = doc["fullRefreshIntervalMin"] | 20;
  return true;
}
