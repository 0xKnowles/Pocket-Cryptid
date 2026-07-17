#pragma once

#include "activities/Activity.h"

// "What is it hearing right now" screen, in full: a live feed of all 16 RecentSightings entries
// (newest first), independent of the encrypted log and of SignalCatalog's dedup counters — see
// RecentSightings.h. The dashboard itself now shows a 4-entry preview of the same feed inline
// (DashboardActivity's "RECENT DEVICES" card); this is the expanded view, reached via Dashboard's
// Up button. Read-only, no paging: the ring buffer is small enough (16 entries) to fit on one
// screen. Redraws on a fixed cadence while open so it stays live.
class DeviceListActivity final : public Activity {
 public:
  explicit DeviceListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeviceList", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  unsigned long lastRenderMs = 0;
};
