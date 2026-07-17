#pragma once

#include "activities/Activity.h"

// Export/maintenance screen. There is deliberately no USB/WiFi transfer protocol here — the
// encrypted log lives on the removable SD card as plain files (/.pocketcryptid/log/*.pclog), so
// "exporting" it is just pulling the card and copying files on a PC. This screen shows what's
// there and reminds the owner how to read it back (scripts/decrypt_log.py + the key from
// Settings). Keeping export card-based instead of a bespoke wire protocol is also the more
// stealth-appropriate choice: nothing about retrieving data requires the device to power on a
// radio or expose a service.
class MaintenanceActivity final : public Activity {
 public:
  explicit MaintenanceActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Maintenance", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
