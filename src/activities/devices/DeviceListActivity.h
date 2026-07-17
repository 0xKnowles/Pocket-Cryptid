#pragma once

#include "activities/Activity.h"

// "What is it hearing right now" screen, in full: a live feed of all 16 RecentSightings entries
// (newest first), independent of the encrypted log and of SignalCatalog's dedup counters — see
// RecentSightings.h. The dashboard itself now shows a 4-entry preview of the same feed inline
// (DashboardActivity's "RECENT DEVICES" card); this is the expanded view, reached via Dashboard's
// Up button. No paging: the ring buffer is small enough (16 entries) to fit on one screen.
// Redraws on a fixed cadence while open so it stays live.
//
// Up/Down move a selection cursor; Confirm on a WifiAp entry toggles it in/out of TargetList
// (DeauthEngine's whitelist/blacklist) — the on-device counterpart to editing
// /.ruby/targets.txt directly. Confirm on a non-AP entry (client/BLE/handshake) does nothing,
// since deauth targets are BSSIDs.
class DeviceListActivity final : public Activity {
 public:
  explicit DeviceListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeviceList", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  unsigned long lastRenderMs = 0;
  size_t selected = 0;
};
