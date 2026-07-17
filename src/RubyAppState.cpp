#include "RubyAppState.h"

void RubyAppState::toJson(JsonDocument& doc) const {
  doc["bootCount"] = bootCount;
  doc["totalCaptureSeconds"] = totalCaptureSeconds;
}

bool RubyAppState::fromJson(JsonVariantConst doc) {
  bootCount = doc["bootCount"] | 0;
  totalCaptureSeconds = doc["totalCaptureSeconds"] | 0;
  return true;
}
