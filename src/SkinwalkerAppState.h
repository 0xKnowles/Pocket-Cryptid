#pragma once
#include <PersistableStore.h>

#include <cstdint>

// Small cross-boot counters that don't belong in SkinwalkerSettings (not user-editable) or
// SkinwalkerManager (not creature state) — mostly cosmetic, shown on the dashboard/lore screens.
class SkinwalkerAppState : public PersistableStore<SkinwalkerAppState> {
  friend class PersistableStore<SkinwalkerAppState>;

 public:
  uint32_t bootCount = 0;
  uint32_t totalCaptureSeconds = 0;  // accumulated at each sleep, approximate

  static const char* getFilePath() { return "/.skinwalker/app_state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  SkinwalkerAppState() = default;
};

#define APP_STATE SkinwalkerAppState::getInstance()
