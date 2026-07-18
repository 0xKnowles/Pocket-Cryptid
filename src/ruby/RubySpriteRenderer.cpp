#include "RubySpriteRenderer.h"

#include <BitmapSource.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "RubyEmbeddedArt.h"
#include "fontIds.h"

namespace {

// Optional SD-card override, one per expression — same folder layout as the repo's bmp/
// directory, so copying that folder onto the SD card root is all that's needed to replace the
// art baked into the firmware (see RubyEmbeddedArt.h) without recompiling.
const char* bmpPathFor(RubyExpression expression) {
  switch (expression) {
    case RubyExpression::EXCITED:
      return "/bmp/excited.bmp";
    case RubyExpression::CURIOUS:
      return "/bmp/curious.bmp";
    case RubyExpression::CONTENT:
      return "/bmp/content.bmp";
    case RubyExpression::BORED:
      return "/bmp/bored.bmp";
    case RubyExpression::LONELY:
      return "/bmp/lonely.bmp";
    case RubyExpression::SLEEPING:
      return "/bmp/sleep.bmp";
  }
  return "";
}

// Scales bitmap down (never up) to fit inside boxSize x boxSize and centers it, then draws —
// shared by both the SD-card and embedded-asset paths below. `label` is just for logging.
void drawScaledAndCentered(const GfxRenderer& renderer, const Bitmap& bitmap, int x, int y, int boxSize,
                           const char* label) {
  const int bw = bitmap.getWidth();
  const int bh = bitmap.getHeight();
  LOG_INF("RUBYART", "Drawing %s (%dx%d, %ubpp)", label, bw, bh, bitmap.getBpp());

  // Mirrors GfxRenderer::drawBitmap's own fit-to-box scale (shrink-only) so the centering offset
  // computed here lines up with what it will actually draw.
  const float scale =
      std::min(1.0f, std::min(static_cast<float>(boxSize) / bw, static_cast<float>(boxSize) / bh));
  const int drawnW = static_cast<int>(bw * scale);
  const int drawnH = static_cast<int>(bh * scale);
  const int offsetX = x + (boxSize - drawnW) / 2;
  const int offsetY = y + (boxSize - drawnH) / 2;

  renderer.drawBitmap(bitmap, offsetX, offsetY, boxSize, boxSize);
}

// Tries the SD-card override first — returns false (drawing nothing) if the file doesn't exist
// or fails to parse, so the caller can fall back to the art baked into the firmware.
bool tryDrawFromSd(const GfxRenderer& renderer, int x, int y, int boxSize, RubyExpression expression) {
  const char* path = bmpPathFor(expression);
  if (!Storage.exists(path)) return false;

  HalFile file = Storage.open(path);
  if (!file) {
    LOG_ERR("RUBYART", "Failed to open SD override %s", path);
    return false;
  }

  HalFileSource source(file);
  Bitmap bitmap(source, /*dithering=*/true);
  const BmpReaderError err = bitmap.parseHeaders();
  if (err != BmpReaderError::Ok) {
    LOG_ERR("RUBYART", "Failed to parse SD override %s: %s", path, Bitmap::errorToString(err));
    file.close();
    return false;
  }
  if (bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    LOG_ERR("RUBYART", "SD override %s parsed with bad dimensions %dx%d", path, bitmap.getWidth(),
            bitmap.getHeight());
    file.close();
    return false;
  }

  drawScaledAndCentered(renderer, bitmap, x, y, boxSize, path);
  file.close();
  return true;
}

// Draws the art baked into the firmware (see RubyEmbeddedArt.h) — always present, so this should
// only ever fail if the generator script produced something the parser rejects.
bool tryDrawFromEmbedded(const GfxRenderer& renderer, int x, int y, int boxSize, RubyExpression expression) {
  const RubyEmbeddedArt::Asset asset = RubyEmbeddedArt::forExpression(expression);
  if (!asset.data || asset.size == 0) return false;

  MemorySource source(asset.data, asset.size);
  Bitmap bitmap(source, /*dithering=*/true);
  const BmpReaderError err = bitmap.parseHeaders();
  if (err != BmpReaderError::Ok) {
    LOG_ERR("RUBYART", "Failed to parse embedded art for expression %u: %s",
            static_cast<unsigned>(expression), Bitmap::errorToString(err));
    return false;
  }
  if (bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    LOG_ERR("RUBYART", "Embedded art for expression %u parsed with bad dimensions %dx%d",
            static_cast<unsigned>(expression), bitmap.getWidth(), bitmap.getHeight());
    return false;
  }

  drawScaledAndCentered(renderer, bitmap, x, y, boxSize, "embedded art");
  return true;
}

bool tryDrawBitmap(const GfxRenderer& renderer, int x, int y, int boxSize, RubyExpression expression) {
  if (tryDrawFromSd(renderer, x, y, boxSize, expression)) return true;
  return tryDrawFromEmbedded(renderer, x, y, boxSize, expression);
}

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

// One fixed silhouette — unlike a pet that grows through stages, Ruby is already fully herself;
// what changes frame to frame is the *expression* (see drawFace), and what changes every
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
// RubyExpression. Both are cut into the silhouette as white (state=false) shapes.
void drawFace(const GfxRenderer& renderer, int cx, int cy, int radiusPx, RubyExpression expression,
             uint8_t animFrame) {
  const int spread = (radiusPx * kEyeSpreadPct) / 100;
  const int eyeY = cy - radiusPx / 6;
  const int mouthY = cy + radiusPx / 4;

  const bool wideOpen = expression == RubyExpression::EXCITED || expression == RubyExpression::CURIOUS;
  const bool activeBlink = wideOpen || expression == RubyExpression::CONTENT;
  const bool blinkClosed = activeBlink && (animFrame % 2 == 1);
  const bool halfLidded = expression == RubyExpression::BORED;
  const bool droopy = expression == RubyExpression::LONELY;
  const bool fullyClosed = expression == RubyExpression::SLEEPING;

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
    case RubyExpression::EXCITED:
      renderer.fillRect(cx - 2, mouthY - 1, 4, 3, /*state=*/false);  // small open "o" — startled
      break;
    case RubyExpression::CURIOUS:
      renderer.drawLine(cx - 3, mouthY + 1, cx + 3, mouthY - 1, /*state=*/false);  // tilted, alert
      break;
    case RubyExpression::CONTENT:
      renderer.drawLine(cx - 3, mouthY, cx + 3, mouthY, /*state=*/false);
      break;
    case RubyExpression::BORED:
      renderer.drawLine(cx - 2, mouthY, cx + 2, mouthY, /*state=*/false);
      break;
    case RubyExpression::LONELY:
      renderer.drawLine(cx - 3, mouthY + 2, cx, mouthY, /*state=*/false);  // downturned corners
      renderer.drawLine(cx, mouthY, cx + 3, mouthY + 2, /*state=*/false);
      break;
    case RubyExpression::SLEEPING:
      break;  // unreachable (handled above); kept so this switch stays exhaustive
  }
}

void drawStaticNoise(const GfxRenderer& renderer, int x, int y, int boxSize, RubyExpression expression,
                     uint8_t animFrame) {
  int density = 0;
  switch (expression) {
    case RubyExpression::EXCITED:
      density = 14;
      break;
    case RubyExpression::CURIOUS:
      density = 9;
      break;
    case RubyExpression::CONTENT:
      density = 6;
      break;
    case RubyExpression::BORED:
      density = 3;
      break;
    case RubyExpression::LONELY:
      density = 1;
      break;
    case RubyExpression::SLEEPING:
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

// "RUBY" used to be the dashboard's header title; it now lives here, pinned to the box's own
// top-left corner instead, so the header itself can stay uncluttered. Backed by a small white
// chip (rather than relying on blank margin in the art) so it stays legible regardless of
// whether the box is showing bitmap art, the procedural silhouette fallback, or dark pixels from
// either one happen to land in that corner.
void drawNameChip(const GfxRenderer& renderer, int x, int y, uint8_t level) {
  constexpr int kChipInset = 5;
  constexpr int kChipPadX = 4;
  constexpr int kChipPadY = 2;
  constexpr int kBadgeGap = 4;  // gap between the RUBY chip and the level badge beside it
  // FONT_SMALL_ID only ships a Regular face (see fontIds.h) — requesting BOLD here silently falls
  // back to Regular (EpdFontFamily::getFont()), so "RUBY" never actually rendered bold on real
  // hardware. Faked below by drawing the glyphs twice, offset one pixel right, the same as
  // DashboardActivity's card titles use for the same reason — the +1px is folded into the chip's
  // own padding, so it doesn't need to widen the chip further.
  const int textW = renderer.getTextWidth(FONT_SMALL_ID, "RUBY");
  const int lineH = renderer.getLineHeight(FONT_SMALL_ID);
  const int chipX = x + kChipInset;
  const int chipY = y + kChipInset;
  const int chipH = lineH + kChipPadY * 2;
  renderer.fillRect(chipX, chipY, textW + kChipPadX * 2, chipH, /*state=*/false);
  renderer.drawText(FONT_SMALL_ID, chipX + kChipPadX, chipY + kChipPadY, "RUBY", true);
  renderer.drawText(FONT_SMALL_ID, chipX + kChipPadX + 1, chipY + kChipPadY, "RUBY", true);

  // Level badge — same chip styling, sitting right next to the name chip rather than folded into
  // it, so it reads as a separate, at-a-glance stat rather than part of the creature's name.
  char levelBuf[8];
  snprintf(levelBuf, sizeof(levelBuf), "Lv.%u", level);
  const int levelTextW = renderer.getTextWidth(FONT_SMALL_ID, levelBuf);
  const int badgeX = chipX + textW + kChipPadX * 2 + kBadgeGap;
  renderer.fillRect(badgeX, chipY, levelTextW + kChipPadX * 2, chipH, /*state=*/false);
  renderer.drawText(FONT_SMALL_ID, badgeX + kChipPadX, chipY + kChipPadY, levelBuf, true);
  renderer.drawText(FONT_SMALL_ID, badgeX + kChipPadX + 1, chipY + kChipPadY, levelBuf, true);
}

}  // namespace

void RubySpriteRenderer::draw(GfxRenderer& renderer, int x, int y, int boxSize, RubyExpression expression,
                                    uint8_t animFrame, uint8_t level) {
  renderer.fillRect(x, y, boxSize, boxSize, /*state=*/false);  // clear to white before redrawing

  if (!tryDrawBitmap(renderer, x, y, boxSize, expression)) {
    const int halfBox = boxSize / 2;
    const int radiusPx = (kRadiusPct * halfBox) / 100;
    const int cx = x + halfBox;
    const int cy = y + halfBox;

    int jitterAmplitude = 1;
    if (expression == RubyExpression::EXCITED) jitterAmplitude = 4;
    if (expression == RubyExpression::CURIOUS) jitterAmplitude = 2;
    if (expression == RubyExpression::BORED) jitterAmplitude = 0;
    if (expression == RubyExpression::LONELY) jitterAmplitude = 0;
    if (expression == RubyExpression::SLEEPING) jitterAmplitude = 0;

    const uint32_t frameSeed = hash32(static_cast<uint32_t>(expression) * 4001 + animFrame * 17);
    drawSilhouette(renderer, cx, cy, radiusPx, animFrame, jitterAmplitude, frameSeed);
    drawFace(renderer, cx, cy, radiusPx, expression, animFrame);
    if (expression != RubyExpression::SLEEPING) {
      drawStaticNoise(renderer, x, y, boxSize, expression, animFrame);
    }
  }

  if (expression != RubyExpression::SLEEPING) drawNameChip(renderer, x, y, level);

  // The box's rounded frame is drawn last, on top of the bitmap/silhouette/noise/name chip, and
  // lives here rather than in the caller because this is also what runs on the box-only "strict
  // partial refresh" tick (see the class comment) — anything drawn by the caller instead would
  // get wiped by this function's own fillRect() clear above and never redrawn. Drawing it last
  // keeps it crisp regardless of how far noise/silhouette pixels wander near the edge. The 1px
  // stroke sits fully inside [x, y, boxSize, boxSize].
  renderer.drawRoundedRect(x, y, boxSize, boxSize, 1, 10, /*state=*/true);
}

void RubySpriteRenderer::drawPortrait(GfxRenderer& renderer, int x, int y, int boxSize) {
  // level is irrelevant here — draw() only shows the badge when expression != SLEEPING.
  draw(renderer, x, y, boxSize, RubyExpression::SLEEPING, 0, 1);
}
