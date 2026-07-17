#pragma once

#include "activities/Activity.h"

// Single-entry viewer over CryptidManager's unlocked lore table. Up/Down move between unlocked
// entries; locked entries (beyond CRYPTID.getState().unlockedLoreCount) aren't shown at all —
// no teasing "???" placeholders, since the point is that new entries surface naturally as the
// creature grows, not that there's a checklist to grind.
class LoreActivity final : public Activity {
 public:
  explicit LoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Lore", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  size_t index = 0;
};
