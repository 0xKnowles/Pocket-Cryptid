#include "BootActivity.h"

#include <GfxRenderer.h>

#include "AppVersion.h"
#include "cryptid/CryptidSpriteRenderer.h"
#include "cryptid/CryptidState.h"
#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  constexpr int kPortraitSize = 120;
  const int portraitX = (pageWidth - kPortraitSize) / 2;
  const int portraitY = pageHeight / 2 - kPortraitSize - 30;
  CryptidSpriteRenderer::drawPortrait(renderer, portraitX, portraitY, kPortraitSize, CryptidStage::DORMANT);

  const int textTop = portraitY + kPortraitSize + 24;
  renderer.drawCenteredText(FONT_UI_12_ID, textTop, "POCKET CRYPTID", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(FONT_UI_10_ID, textTop + 26, "passive RF analyzer");
  renderer.drawCenteredText(FONT_SMALL_ID, textTop + 48, "listening...");
  renderer.drawCenteredText(FONT_SMALL_ID, pageHeight - 30, POCKET_CRYPTID_VERSION);
  renderer.displayBuffer();
}
