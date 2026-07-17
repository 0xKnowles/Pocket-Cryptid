#include "BootActivity.h"

#include <BitmapSource.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "AppVersion.h"
#include "fontIds.h"
#include "ruby/RubyEmbeddedArt.h"
#include "ruby/RubySpriteRenderer.h"

namespace {
constexpr int kTitleBandHeight = 34;

// Scales bitmap to fit the whole screen (shrink-only) and anchors it to the top-left, so the
// title band drawn afterwards overlays its top edge predictably regardless of aspect ratio.
bool drawFullScreen(const GfxRenderer& renderer, const Bitmap& bitmap) {
  const int bw = bitmap.getWidth();
  const int bh = bitmap.getHeight();
  if (bw <= 0 || bh <= 0) return false;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const float scale = std::min(1.0f, std::min(static_cast<float>(screenW) / bw, static_cast<float>(screenH) / bh));
  const int drawnW = static_cast<int>(bw * scale);
  const int offsetX = (screenW - drawnW) / 2;
  renderer.drawBitmap(bitmap, offsetX, 0, screenW, screenH);
  return true;
}

// Optional SD-card override for the boot splash, checked before the art baked into the firmware
// — same pattern as RubySpriteRenderer's per-expression art (see RubySpriteRenderer.cpp).
bool tryDrawBootArtFromSd(const GfxRenderer& renderer) {
  constexpr const char* kPath = "/bmp/boot.bmp";
  if (!Storage.exists(kPath)) return false;

  HalFile file = Storage.open(kPath);
  if (!file) {
    LOG_ERR("RUBYART", "Failed to open SD override %s", kPath);
    return false;
  }

  HalFileSource source(file);
  Bitmap bitmap(source, /*dithering=*/true);
  const BmpReaderError err = bitmap.parseHeaders();
  if (err != BmpReaderError::Ok) {
    LOG_ERR("RUBYART", "Failed to parse SD override %s: %s", kPath, Bitmap::errorToString(err));
    file.close();
    return false;
  }
  const bool drawn = drawFullScreen(renderer, bitmap);
  file.close();
  return drawn;
}

bool tryDrawBootArtFromEmbedded(const GfxRenderer& renderer) {
  const RubyEmbeddedArt::Asset asset = RubyEmbeddedArt::boot();
  if (!asset.data || asset.size == 0) return false;

  MemorySource source(asset.data, asset.size);
  Bitmap bitmap(source, /*dithering=*/true);
  const BmpReaderError err = bitmap.parseHeaders();
  if (err != BmpReaderError::Ok) {
    LOG_ERR("RUBYART", "Failed to parse embedded boot art: %s", Bitmap::errorToString(err));
    return false;
  }
  return drawFullScreen(renderer, bitmap);
}
}  // namespace

void BootActivity::onEnter() {
  Activity::onEnter();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  const bool hasFullArt = tryDrawBootArtFromSd(renderer) || tryDrawBootArtFromEmbedded(renderer);

  if (hasFullArt) {
    // boot.bmp's aspect ratio doesn't quite match the screen's, so shrink-to-fit leaves a blank
    // margin at the bottom — a natural spot for these two lines without needing an opaque band.
    renderer.drawCenteredText(FONT_SMALL_ID, pageHeight - 72, "passive RF analyzer");
    renderer.drawCenteredText(FONT_SMALL_ID, pageHeight - 50, "listening...");
  } else {
    // No boot art at all (e.g. the generator never ran) — the small procedural portrait this
    // screen used before boot.bmp existed.
    constexpr int kPortraitSize = 120;
    const int portraitX = (pageWidth - kPortraitSize) / 2;
    const int portraitY = pageHeight / 2 - kPortraitSize - 30;
    RubySpriteRenderer::drawPortrait(renderer, portraitX, portraitY, kPortraitSize);
    const int textTop = portraitY + kPortraitSize + 24;
    renderer.drawCenteredText(FONT_UI_10_ID, textTop, "passive RF analyzer");
    renderer.drawCenteredText(FONT_SMALL_ID, textTop + 26, "listening...");
  }

  // Title band overlays the top edge of the art (or the blank top of the fallback layout) so
  // "RUBY vX.Y.Z" stays legible no matter what's underneath — same white-on-black technique as
  // the dashboard's "HANDSHAKE CAPTURED" flash.
  renderer.fillRect(0, 0, pageWidth, kTitleBandHeight, true);
  char title[24];
  snprintf(title, sizeof(title), "RUBY v%s", RUBY_BASE_VERSION);
  const int titleTextY = (kTitleBandHeight - renderer.getLineHeight(FONT_UI_12_ID)) / 2;
  renderer.drawCenteredText(FONT_UI_12_ID, titleTextY, title, false, EpdFontFamily::BOLD);

  renderer.drawCenteredText(FONT_SMALL_ID, pageHeight - 24, RUBY_VERSION);

  renderer.displayBuffer();
}
