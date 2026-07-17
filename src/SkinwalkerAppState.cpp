#include "SkinwalkerAppState.h"

void SkinwalkerAppState::toJson(JsonDocument& doc) const {
  doc["bootCount"] = bootCount;
  doc["totalCaptureSeconds"] = totalCaptureSeconds;
}

bool SkinwalkerAppState::fromJson(JsonVariantConst doc) {
  bootCount = doc["bootCount"] | 0;
  totalCaptureSeconds = doc["totalCaptureSeconds"] | 0;
  return true;
}
