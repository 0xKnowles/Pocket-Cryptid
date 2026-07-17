#pragma once

#include <cstddef>
#include <cstdint>

#include "activities/Activity.h"

// Browses ApHistory — every WiFi AP this device has ever seen, first/last seen and sighting
// count, persisted across reboots (see ApHistory.h). Reached from Settings' "AP History" row via
// pushActivity, so Back pops straight back to Settings rather than the Dashboard — same reasoning
// as TargetPickerActivity's own header comment. Sorted strongest-recency-first (most recently
// seen at the top); Up/Down page through kPageSize at a time, newest page first.
class ApHistoryActivity final : public Activity {
 public:
  explicit ApHistoryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ApHistory", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr size_t kPageSize = 6;

  size_t pageStart = 0;  // index (into the lastSeen-sorted order) of the first entry on this page
  unsigned long lastRenderMs = 0;
};
