#include "BootActivity.h"

#include <GfxRenderer.h>

#include "AppVersion.h"
#include "fontIds.h"
#include "ruby/RubySpriteRenderer.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  constexpr int kPortraitSize = 120;
  const int portraitX = (pageWidth - kPortraitSize) / 2;
  const int portraitY = pageHeight / 2 - kPortraitSize - 30;
  RubySpriteRenderer::drawPortrait(renderer, portraitX, portraitY, kPortraitSize);

  const int textTop = portraitY + kPortraitSize + 24;
  renderer.drawCenteredText(FONT_UI_12_ID, textTop, "RUBY", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(FONT_UI_10_ID, textTop + 26, "passive RF analyzer");
  renderer.drawCenteredText(FONT_SMALL_ID, textTop + 48, "listening...");
  renderer.drawCenteredText(FONT_SMALL_ID, pageHeight - 30, RUBY_VERSION);
  renderer.displayBuffer();
}
