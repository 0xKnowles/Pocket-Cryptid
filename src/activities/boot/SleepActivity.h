#pragma once
#include "activities/Activity.h"

// Rendered once, synchronously, right before the device commits to deep sleep — capture stops
// here (deep sleep powers the radios down), so this screen also displays a final "session
// summary" of what was caught before going dark.
class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  bool fromTimeout;
};
