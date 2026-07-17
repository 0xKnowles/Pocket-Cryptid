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
constexpr int kTitleMarginX = 16;
constexpr int kTitleMarginY = 14;

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
    // boot.bmp is now exactly screen-sized (792x528) so it renders edge-to-edge with no blank
    // letterboxing margin — the bottom third is the dark e-waste pile in the art itself, so this
    // line is drawn white (false) rather than the plain black used everywhere else, or it'd be
    // unreadable against it.
    renderer.drawCenteredText(FONT_SMALL_ID, pageHeight - 50, "listening...", false);
  } else {
    // No boot art at all (e.g. the generator never ran) — the small procedural portrait this
    // screen used before boot.bmp existed.
    constexpr int kPortraitSize = 120;
    const int portraitX = (pageWidth - kPortraitSize) / 2;
    const int portraitY = pageHeight / 2 - kPortraitSize - 30;
    RubySpriteRenderer::drawPortrait(renderer, portraitX, portraitY, kPortraitSize);
    renderer.drawCenteredText(FONT_SMALL_ID, portraitY + kPortraitSize + 24, "listening...");
  }

  // "Ruby" / version sit directly on whatever's underneath rather than an opaque band. With
  // full art, boot.bmp's own composition leaves the top corners open white space (above the
  // character's shoulders); without it, the whole background is the plain white clearScreen()
  // fill. Either way plain black text reads fine without covering anything.
  char versionLabel[16];
  snprintf(versionLabel, sizeof(versionLabel), "v%s", RUBY_BASE_VERSION);
  renderer.drawText(FONT_UI_12_ID, kTitleMarginX, kTitleMarginY, "Ruby", true, EpdFontFamily::BOLD);
  const int versionW = renderer.getTextWidth(FONT_UI_12_ID, versionLabel, EpdFontFamily::BOLD);
  renderer.drawText(FONT_UI_12_ID, pageWidth - kTitleMarginX - versionW, kTitleMarginY, versionLabel, true,
                    EpdFontFamily::BOLD);

  // "passive RF analyzer" centered under the Ruby/version row — same open top margin (verified
  // clear via pixel sampling down to y=40 before the character's hair/horns start), so plain
  // black text reads fine here in both branches.
  renderer.drawCenteredText(FONT_SMALL_ID, kTitleMarginY + 22, "passive RF analyzer");

  renderer.displayBuffer();
}
