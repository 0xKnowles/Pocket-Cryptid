#pragma once

#include "activities/Activity.h"

// Simple vertical list: Up/Down selects a row, Left/Right adjusts it, Confirm activates it,
// Back returns to the dashboard. No sub-screens, no keyboard entry — every setting here is a
// toggle or a small bounded number, which keeps the whole settings surface a single screen.
class SettingsActivity final : public Activity {
 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Settings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Row : uint8_t {
    RowWifiEnabled = 0,
    RowBleEnabled,
    RowWifiDwell,
    RowGhostClearInterval,
    RowRevealKey,
    RowWipeLog,
    RowCount,
  };

  int selected = 0;
  bool wipeArmed = false;
  unsigned long wipeArmedUntilMs = 0;
  bool showingKey = false;
  char revealedKeyHex[65] = {};

  void adjustSelected(int direction);
  void activateSelected();
};
