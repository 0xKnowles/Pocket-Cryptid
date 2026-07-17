#pragma once
#include <PersistableStore.h>

#include <cstdint>

// Small cross-boot counters that don't belong in CryptidSettings (not user-editable) or
// CryptidManager (not creature state) — mostly cosmetic, shown on the dashboard/lore screens.
class CryptidAppState : public PersistableStore<CryptidAppState> {
  friend class PersistableStore<CryptidAppState>;

 public:
  uint32_t bootCount = 0;
  uint32_t totalCaptureSeconds = 0;  // accumulated at each sleep, approximate

  static const char* getFilePath() { return "/.pocketcryptid/app_state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  CryptidAppState() = default;
};

#define APP_STATE CryptidAppState::getInstance()
