#pragma once

#include <GfxRenderer.h>

#include "CryptidState.h"

// Renders the cryptid entirely procedurally — a jittered silhouette polygon plus analog-noise
// "static" scattered over it — instead of loading baked bitmap frames. There is no art pipeline
// here on purpose: the creature's silhouette gets more elaborate (more points, larger radius) as
// it evolves through CryptidStage, and its noise density/eye state reflect CryptidMood, all
// computed at draw time from small per-stage point tables in the .cpp.
//
// This is also what gets redrawn on every "strict partial refresh" tick: callers own clearing
// and redrawing only the box below, then calling GfxRenderer::displayBuffer(FAST_REFRESH) — see
// DashboardActivity — so the surrounding static dashboard content is never touched.
class CryptidSpriteRenderer {
 public:
  // Draws into a boxSize x boxSize square with top-left at (x, y). animFrame cycles the jitter
  // and eye state (2 frames is enough for a believable "breathing"/blink loop at a slow tick).
  static void draw(GfxRenderer& renderer, int x, int y, int boxSize, CryptidStage stage, CryptidMood mood,
                   uint8_t animFrame);

  // Small (32x32-ish) portrait used on the sleep screen and boot splash — same silhouette
  // generator, fixed frame 0, no noise overlay (kept perfectly static across sleep frames).
  static void drawPortrait(GfxRenderer& renderer, int x, int y, int boxSize, CryptidStage stage);
};
