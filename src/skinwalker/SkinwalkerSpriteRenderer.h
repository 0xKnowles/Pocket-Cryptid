#pragma once

#include <GfxRenderer.h>

#include "SkinwalkerState.h"

// Renders the skinwalker entirely procedurally — a jittered silhouette polygon, a pair of eyes, a
// small mouth, and analog-noise "static" — instead of loading baked bitmap frames. Unlike a pet
// that grows through stages, the silhouette here is one fixed shape: what changes from frame to
// frame is the *expression* (eyes/mouth/noise/jitter), driven by SkinwalkerExpression, the same
// way Pwnagotchi's face reacts to what it just found rather than the pet "leveling up."
//
// This is also what gets redrawn on every "strict partial refresh" tick: callers own clearing and
// redrawing only the box below, then calling GfxRenderer::displayBuffer(FAST_REFRESH) — see
// DashboardActivity — so the surrounding static dashboard content is never touched.
class SkinwalkerSpriteRenderer {
 public:
  // Draws into a boxSize x boxSize square with top-left at (x, y). animFrame cycles the jitter
  // and eye state (2 frames is enough for a believable "breathing"/blink loop at a slow tick).
  static void draw(GfxRenderer& renderer, int x, int y, int boxSize, SkinwalkerExpression expression,
                   uint8_t animFrame);

  // Small portrait used on the sleep screen and boot splash — same silhouette generator, fixed
  // frame 0, SLEEPING expression (closed eyes, no noise), kept perfectly static across frames.
  static void drawPortrait(GfxRenderer& renderer, int x, int y, int boxSize);
};
