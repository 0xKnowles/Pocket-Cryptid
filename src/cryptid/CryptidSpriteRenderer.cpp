#include "CryptidSpriteRenderer.h"

#include <cmath>

namespace {

// Not relying on M_PI: it's a POSIX/GNU extension to <cmath>, not standard C++, and its
// availability under newlib's -std=gnu++2a varies by define. Cheap to just spell it out.
constexpr double kPi = 3.14159265358979323846;

// Cheap deterministic integer hash (Murmur3-style finalizer) — used instead of a stateful PRNG
// so every noise/jitter value is a pure function of (stage, frame, index). That keeps two calls
// with the same arguments producing pixel-identical output, which matters for the "strict
// partial refresh" contract: DashboardActivity may redraw the same frame more than once (e.g.
// after a settings round-trip) and it must not visibly shift when nothing actually changed.
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

struct StageShape {
  uint8_t pointCount;      // silhouette vertex count — more points = more elaborate creature
  uint16_t radiusPct;      // silhouette radius: percent of half the box size (input), rescaled to
                           // physical px in-place by draw() before use — see there
  uint8_t jagPct;          // how much each vertex deviates from a perfect circle, percent of radius
  uint8_t eyeCount;        // 0 for DORMANT (not materialized yet), 2 otherwise
  uint8_t eyeSpreadPct;    // half-distance between eyes, percent of radius
};

constexpr StageShape kShapes[] = {
    /* DORMANT */ {5, 22, 25, 0, 0},
    /* LARVA   */ {7, 34, 40, 2, 30},
    /* WRAITH  */ {9, 46, 60, 2, 42},
    /* STALKER */ {11, 58, 78, 2, 50},
    /* APEX    */ {14, 68, 95, 2, 56},
};

// Repeating jag pattern applied on top of the per-stage jagPct so silhouettes read as ragged
// rather than as a smooth many-sided polygon. Values are signed percent-of-radius offsets.
constexpr int8_t kJagPattern[6] = {0, -35, 18, -22, 30, -12};

void drawSilhouette(GfxRenderer& renderer, int cx, int cy, const StageShape& shape, uint8_t animFrame,
                    int jitterAmplitude, uint32_t frameSeed) {
  int xs[16];
  int ys[16];
  const int n = shape.pointCount;
  for (int i = 0; i < n; i++) {
    const double angle = (2.0 * kPi * i) / n - kPi / 2.0;  // start pointing "up"
    const int jag = (kJagPattern[i % 6] * shape.jagPct) / 100;
    int radius = shape.radiusPct + (shape.radiusPct * jag) / 100;
    radius += jitter(frameSeed + i * 97 + animFrame * 131, jitterAmplitude);
    if (radius < 4) radius = 4;
    // `radius` here is already in physical pixels — the caller (draw()) rescales
    // shape.radiusPct from a percent-of-box-size value to px before calling in.
    xs[i] = cx + static_cast<int>(radius * cos(angle));
    ys[i] = cy + static_cast<int>(radius * sin(angle));
  }
  renderer.fillPolygon(xs, ys, n, /*state=*/true);
}

void drawEyes(GfxRenderer& renderer, int cx, int cy, const StageShape& shape, int radiusPx, CryptidMood mood,
             uint8_t animFrame) {
  if (shape.eyeCount == 0) return;
  const int spread = (radiusPx * shape.eyeSpreadPct) / 100;
  const int eyeY = cy - radiusPx / 6;
  const bool blinkClosed = (animFrame % 2 == 1) && (mood == CryptidMood::THRIVING || mood == CryptidMood::CONTENT);
  const bool halfLidded = (mood == CryptidMood::STARVING);

  for (int side = -1; side <= 1; side += 2) {
    const int ex = cx + side * spread;
    if (blinkClosed) {
      renderer.drawLine(ex - 3, eyeY, ex + 3, eyeY, /*state=*/false);
    } else if (halfLidded) {
      renderer.fillRect(ex - 2, eyeY, 4, 1, /*state=*/false);
    } else {
      renderer.fillRect(ex - 2, eyeY - 2, 4, 4, /*state=*/false);
    }
  }
}

void drawStaticNoise(GfxRenderer& renderer, int x, int y, int boxSize, CryptidMood mood, uint8_t animFrame) {
  int density = 0;
  switch (mood) {
    case CryptidMood::THRIVING:
      density = 10;
      break;
    case CryptidMood::CONTENT:
      density = 6;
      break;
    case CryptidMood::RESTLESS:
      density = 14;
      break;
    case CryptidMood::STARVING:
      density = 4;
      break;
    case CryptidMood::ASLEEP:
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

void CryptidSpriteRenderer::draw(GfxRenderer& renderer, int x, int y, int boxSize, CryptidStage stage,
                                 CryptidMood mood, uint8_t animFrame) {
  renderer.fillRect(x, y, boxSize, boxSize, /*state=*/false);  // clear to white before redrawing

  const StageShape& templateShape = kShapes[static_cast<uint8_t>(stage)];
  StageShape shape = templateShape;
  const int halfBox = boxSize / 2;
  shape.radiusPct = static_cast<uint16_t>((templateShape.radiusPct * halfBox) / 100);  // now physical px

  const int cx = x + halfBox;
  const int cy = y + halfBox;
  int jitterAmplitude = 1;
  if (mood == CryptidMood::THRIVING) jitterAmplitude = 3;
  if (mood == CryptidMood::RESTLESS) jitterAmplitude = 4;
  if (mood == CryptidMood::STARVING) jitterAmplitude = 0;
  if (mood == CryptidMood::ASLEEP) jitterAmplitude = 0;

  const uint32_t frameSeed = hash32(static_cast<uint32_t>(stage) * 4001 + animFrame * 17);
  drawSilhouette(renderer, cx, cy, shape, animFrame, jitterAmplitude, frameSeed);
  drawEyes(renderer, cx, cy, shape, shape.radiusPct, mood, animFrame);
  if (mood != CryptidMood::ASLEEP) {
    drawStaticNoise(renderer, x, y, boxSize, mood, animFrame);
  }
}

void CryptidSpriteRenderer::drawPortrait(GfxRenderer& renderer, int x, int y, int boxSize, CryptidStage stage) {
  draw(renderer, x, y, boxSize, stage, CryptidMood::ASLEEP, 0);
}
