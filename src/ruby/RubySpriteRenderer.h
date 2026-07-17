#pragma once

#include <GfxRenderer.h>

#include "RubyState.h"

// Draws Ruby: bitmap art per expression from the SD card (/bmp/<expression>.bmp — see the
// repo's bmp/ directory) when present, otherwise a jittered silhouette polygon with procedural
// eyes/mouth/noise as a fallback so the firmware never depends on the SD card having art on it.
// Unlike a pet that grows through stages, there's one fixed identity: what changes from frame to
// frame is the *expression*, driven by RubyExpression, the same way Pwnagotchi's face reacts to
// what it just found rather than the pet "leveling up."
//
// This is also what gets redrawn on every "strict partial refresh" tick: callers own clearing and
// redrawing only the box below, then calling GfxRenderer::displayBuffer(FAST_REFRESH) — see
// DashboardActivity — so the surrounding static dashboard content is never touched.
class RubySpriteRenderer {
 public:
  // Draws into a boxSize x boxSize square with top-left at (x, y). animFrame cycles the jitter
  // and eye state (2 frames is enough for a believable "breathing"/blink loop at a slow tick).
  static void draw(GfxRenderer& renderer, int x, int y, int boxSize, RubyExpression expression,
                   uint8_t animFrame);

  // Small portrait used on the sleep screen and boot splash — same silhouette generator, fixed
  // frame 0, SLEEPING expression (closed eyes, no noise), kept perfectly static across frames.
  static void drawPortrait(GfxRenderer& renderer, int x, int y, int boxSize);
};
