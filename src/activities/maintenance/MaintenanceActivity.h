#pragma once

#include "activities/Activity.h"

// Export/maintenance screen. The encrypted log lives on the removable SD card as plain files
// (/.ruby/log/*.pclog); pulling the card and copying files on a PC remains the primary export
// path, and this screen shows what's there plus how to read it back (scripts/decrypt_log.py + the
// key from Settings).
//
// Down also opens UsbTransferActivity, a USB-CDC pull path for the same files without removing
// the card. This is a deliberate trade against this project's earlier stealth-first stance (no
// exposed service, ever) — the previous version of this comment documented that stance
// explicitly. The channel it opens only runs while a user has physically navigated into that
// screen (no background listener during normal capture — see UsbTransferActivity for how logging
// is muted so its wire isn't shared with anything else), which keeps the "requires physical
// possession" bar intact even though "requires pulling the SD card" no longer holds.
class MaintenanceActivity final : public Activity {
 public:
  explicit MaintenanceActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Maintenance", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
