#include "CryptidSettings.h"

void CryptidSettings::toJson(JsonDocument& doc) const {
  doc["wifiSniffEnabled"] = wifiSniffEnabled;
  doc["bleSniffEnabled"] = bleSniffEnabled;
  doc["wifiChannelDwellMs"] = wifiChannelDwellMs;
  doc["clockUtcOffsetQ"] = clockUtcOffsetQ;
  doc["fullRefreshIntervalMin"] = fullRefreshIntervalMin;
}

bool CryptidSettings::fromJson(JsonVariantConst doc) {
  wifiSniffEnabled = doc["wifiSniffEnabled"] | true;
  bleSniffEnabled = doc["bleSniffEnabled"] | true;
  wifiChannelDwellMs = doc["wifiChannelDwellMs"] | 300;
  clockUtcOffsetQ = doc["clockUtcOffsetQ"] | 48;
  fullRefreshIntervalMin = doc["fullRefreshIntervalMin"] | 20;
  return true;
}
