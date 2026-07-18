#pragma once

#include "activities/Activity.h"

// Simple vertical list: Up/Down selects a row, Left/Right adjusts it, Confirm activates it,
// Back returns to the dashboard. Two rows (Whitelist/Blacklist) are the one exception to "no
// sub-screens" — Confirm on those pushes TargetPickerActivity (see its own header comment for
// why that's push/pop navigation instead of the flat replace every other row here uses).
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
    RowWifiChannelScope,
    RowGhostClearInterval,
    RowPowerShortPress,
    RowRawCapture,
    RowActiveDeauth,
    RowWhitelist,
    RowBlacklist,
    RowRevealKey,
    RowWipeLog,
    RowResetStats,
    RowCount,
  };

  int selected = 0;
  bool wipeArmed = false;
  unsigned long wipeArmedUntilMs = 0;
  // Separate arm state from wipeArmed above — these are two independent destructive actions, and
  // sharing one flag would make both rows show "confirm?" simultaneously whenever either was
  // armed.
  bool statsResetArmed = false;
  unsigned long statsResetArmedUntilMs = 0;
  bool showingKey = false;
  char revealedKeyHex[65] = {};

  void adjustSelected(int direction);
  void activateSelected();
};
