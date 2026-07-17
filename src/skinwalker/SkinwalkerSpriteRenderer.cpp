#include "SkinwalkerSpriteRenderer.h"

#include <cmath>

namespace {

// Not relying on M_PI: it's a POSIX/GNU extension to <cmath>, not standard C++, and its
// availability under newlib's -std=gnu++2a varies by define. Cheap to just spell it out.
constexpr double kPi = 3.14159265358979323846;

// Cheap deterministic integer hash (Murmur3-style finalizer) — used instead of a stateful PRNG so
// every noise/jitter value is a pure function of (expression, frame, index). That keeps two calls
// with the same arguments producing pixel-identical output, which matters for the "strict partial
// refresh" contract: DashboardActivity may redraw the same frame more than once (e.g. after a
// settings round-trip) and it must not visibly shift when nothing actually changed.
uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// Returns a value in roughly [-amplitude, amplitude].
int jitter(uint32_t seed, int amplitude) {
  if (amplitude <= 0) return 0;
  const int32_t r = static_cast<int32_t>(hash32(seed) % static_cast<uint32_t>(2 * amplitude + 1));
  return r - amplitude;
}

// One fixed silhouette — unlike a pet that grows through stages, the skinwalker is already fully
// itself; what changes frame to frame is the *expression* (see drawFace), and what changes every
// redraw regardless of expression is a small amount of per-vertex jitter so the outline never
// looks perfectly static.
constexpr int kPointCount = 10;
constexpr int kRadiusPct = 55;    // percent of half the box size
constexpr int kJagPct = 65;       // percent-of-radius vertex deviation from a perfect circle
constexpr int kEyeSpreadPct = 45;  // half-distance between eyes, percent of radius

// Repeating jag pattern applied on top of kJagPct so the silhouette reads as ragged rather than a
// smooth many-sided polygon. Values are signed percent-of-radius offsets.
constexpr int8_t kJagPattern[6] = {0, -35, 18, -22, 30, -12};

void drawSilhouette(const GfxRenderer& renderer, int cx, int cy, int radiusPx, uint8_t animFrame, int jitterAmplitude,
                    uint32_t frameSeed) {
  int xs[kPointCount];
  int ys[kPointCount];
  for (int i = 0; i < kPointCount; i++) {
    const double angle = (2.0 * kPi * i) / kPointCount - kPi / 2.0;  // start pointing "up"
    const int jag = (kJagPattern[i % 6] * kJagPct) / 100;
    int radius = radiusPx + (radiusPx * jag) / 100;
    radius += jitter(frameSeed + i * 97 + animFrame * 131, jitterAmplitude);
    if (radius < 4) radius = 4;
    xs[i] = cx + static_cast<int>(radius * cos(angle));
    ys[i] = cy + static_cast<int>(radius * sin(angle));
  }
  renderer.fillPolygon(xs, ys, kPointCount, /*state=*/true);
}

// Eyes + a short mouth — together "the face," the only part of the drawing that actually carries
// SkinwalkerExpression. Both are cut into the silhouette as white (state=false) shapes.
void drawFace(const GfxRenderer& renderer, int cx, int cy, int radiusPx, SkinwalkerExpression expression,
             uint8_t animFrame) {
  const int spread = (radiusPx * kEyeSpreadPct) / 100;
  const int eyeY = cy - radiusPx / 6;
  const int mouthY = cy + radiusPx / 4;

  const bool wideOpen = expression == SkinwalkerExpression::EXCITED || expression == SkinwalkerExpression::CURIOUS;
  const bool activeBlink = wideOpen || expression == SkinwalkerExpression::CONTENT;
  const bool blinkClosed = activeBlink && (animFrame % 2 == 1);
  const bool halfLidded = expression == SkinwalkerExpression::BORED;
  const bool droopy = expression == SkinwalkerExpression::LONELY;
  const bool fullyClosed = expression == SkinwalkerExpression::SLEEPING;

  for (int side = -1; side <= 1; side += 2) {
    const int ex = cx + side * spread;
    if (fullyClosed || blinkClosed) {
      renderer.drawLine(ex - 3, eyeY, ex + 3, eyeY, /*state=*/false);
    } else if (droopy) {
      renderer.drawLine(ex - 2, eyeY + 1, ex + 2, eyeY + 1, /*state=*/false);
    } else if (halfLidded) {
      renderer.fillRect(ex - 2, eyeY, 4, 1, /*state=*/false);
    } else if (wideOpen) {
      renderer.fillRect(ex - 3, eyeY - 3, 5, 5, /*state=*/false);
    } else {
      renderer.fillRect(ex - 2, eyeY - 2, 4, 4, /*state=*/false);
    }
  }

  if (fullyClosed) return;  // asleep: no mouth, just closed eyes

  switch (expression) {
    case SkinwalkerExpression::EXCITED:
      renderer.fillRect(cx - 2, mouthY - 1, 4, 3, /*state=*/false);  // small open "o" — startled
      break;
    case SkinwalkerExpression::CURIOUS:
      renderer.drawLine(cx - 3, mouthY + 1, cx + 3, mouthY - 1, /*state=*/false);  // tilted, alert
      break;
    case SkinwalkerExpression::CONTENT:
      renderer.drawLine(cx - 3, mouthY, cx + 3, mouthY, /*state=*/false);
      break;
    case SkinwalkerExpression::BORED:
      renderer.drawLine(cx - 2, mouthY, cx + 2, mouthY, /*state=*/false);
      break;
    case SkinwalkerExpression::LONELY:
      renderer.drawLine(cx - 3, mouthY + 2, cx, mouthY, /*state=*/false);  // downturned corners
      renderer.drawLine(cx, mouthY, cx + 3, mouthY + 2, /*state=*/false);
      break;
    case SkinwalkerExpression::SLEEPING:
      break;  // unreachable (handled above); kept so this switch stays exhaustive
  }
}

void drawStaticNoise(const GfxRenderer& renderer, int x, int y, int boxSize, SkinwalkerExpression expression,
                     uint8_t animFrame) {
  int density = 0;
  switch (expression) {
    case SkinwalkerExpression::EXCITED:
      density = 14;
      break;
    case SkinwalkerExpression::CURIOUS:
      density = 9;
      break;
    case SkinwalkerExpression::CONTENT:
      density = 6;
      break;
    case SkinwalkerExpression::BORED:
      density = 3;
      break;
    case SkinwalkerExpression::LONELY:
      density = 1;
      break;
    case SkinwalkerExpression::SLEEPING:
      density = 0;
      break;
  }
  for (int i = 0; i < density; i++) {
    const uint32_t seed = hash32(static_cast<uint32_t>(i) * 733 + animFrame * 991 + boxSize);
    const int nx = x + static_cast<int>(seed % static_cast<uint32_t>(boxSize));
    const int ny = y + static_cast<int>((seed / 7) % static_cast<uint32_t>(boxSize));
    const bool ink = (seed & 1) == 0;
    renderer.drawPixel(nx, ny, ink);
  }
}

}  // namespace

void SkinwalkerSpriteRenderer::draw(GfxRenderer& renderer, int x, int y, int boxSize, SkinwalkerExpression expression,
                                    uint8_t animFrame) {
  renderer.fillRect(x, y, boxSize, boxSize, /*state=*/false);  // clear to white before redrawing

  const int halfBox = boxSize / 2;
  const int radiusPx = (kRadiusPct * halfBox) / 100;
  const int cx = x + halfBox;
  const int cy = y + halfBox;

  int jitterAmplitude = 1;
  if (expression == SkinwalkerExpression::EXCITED) jitterAmplitude = 4;
  if (expression == SkinwalkerExpression::CURIOUS) jitterAmplitude = 2;
  if (expression == SkinwalkerExpression::BORED) jitterAmplitude = 0;
  if (expression == SkinwalkerExpression::LONELY) jitterAmplitude = 0;
  if (expression == SkinwalkerExpression::SLEEPING) jitterAmplitude = 0;

  const uint32_t frameSeed = hash32(static_cast<uint32_t>(expression) * 4001 + animFrame * 17);
  drawSilhouette(renderer, cx, cy, radiusPx, animFrame, jitterAmplitude, frameSeed);
  drawFace(renderer, cx, cy, radiusPx, expression, animFrame);
  if (expression != SkinwalkerExpression::SLEEPING) {
    drawStaticNoise(renderer, x, y, boxSize, expression, animFrame);
  }

  // The specimen card's rounded frame is drawn last, on top of the silhouette/noise, and lives
  // here rather than in the caller because this is also what runs on the box-only "strict partial
  // refresh" tick (see the class comment) — a border drawn by the caller instead would get wiped
  // by this function's own fillRect() clear above and never redrawn. Drawing it last keeps it
  // crisp regardless of how far noise/silhouette pixels wander near the edge. The 1px stroke sits
  // fully inside [x, y, boxSize, boxSize].
  renderer.drawRoundedRect(x, y, boxSize, boxSize, 1, 10, /*state=*/true);
}

void SkinwalkerSpriteRenderer::drawPortrait(GfxRenderer& renderer, int x, int y, int boxSize) {
  draw(renderer, x, y, boxSize, SkinwalkerExpression::SLEEPING, 0);
}
