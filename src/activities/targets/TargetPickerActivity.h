#pragma once

#include "TargetList.h"
#include "activities/Activity.h"

// "Pick a network like you're connecting to it" — reached from Settings' "Whitelist"/"Blacklist"
// rows via pushActivity (not the usual flat replaceActivity navigation every other screen uses),
// specifically so Back->finish() pops straight back to Settings instead of the Dashboard. Shows
// ApScanCache's live, deduplicated-by-BSSID scan results (strongest signal first); Up/Down move
// a cursor, Confirm toggles the highlighted network in/out of whichever list (`kind`) this
// instance was opened for — a `[+]`/`[-]` marker shows current membership. This is the on-device
// alternative to hand-editing /.ruby/targets.txt.
class TargetPickerActivity final : public Activity {
 public:
  explicit TargetPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, TargetListKind kind)
      : Activity("TargetPicker", renderer, mappedInput), kind(kind) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  TargetListKind kind;
  size_t selected = 0;
  unsigned long lastRenderMs = 0;
};
