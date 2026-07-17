#include "CryptidAppState.h"

void CryptidAppState::toJson(JsonDocument& doc) const {
  doc["bootCount"] = bootCount;
  doc["totalCaptureSeconds"] = totalCaptureSeconds;
}

bool CryptidAppState::fromJson(JsonVariantConst doc) {
  bootCount = doc["bootCount"] | 0;
  totalCaptureSeconds = doc["totalCaptureSeconds"] | 0;
  return true;
}
