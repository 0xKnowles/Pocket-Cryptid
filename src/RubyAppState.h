#pragma once
#include <PersistableStore.h>

#include <cstdint>

// Small cross-boot counters that don't belong in RubySettings (not user-editable) or
// RubyManager (not creature state) — mostly cosmetic, shown on the dashboard/lore screens.
class RubyAppState : public PersistableStore<RubyAppState> {
  friend class PersistableStore<RubyAppState>;

 public:
  uint32_t bootCount = 0;
  uint32_t totalCaptureSeconds = 0;  // accumulated at each sleep, approximate

  static const char* getFilePath() { return "/.ruby/app_state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  RubyAppState() = default;
};

#define APP_STATE RubyAppState::getInstance()
