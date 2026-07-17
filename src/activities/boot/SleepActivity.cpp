#include "SleepActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>

#include "SignalCatalog.h"
#include "fontIds.h"
#include "ruby/RubySpriteRenderer.h"
#include "ui/Chrome.h"

void SleepActivity::onEnter() {
  Activity::onEnter();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  // Temporary checkpoint logging while tracking down a GFX "Outside range" flood + crash on
  // entering sleep — pinpoints exactly which draw call it happens on. Remove once confirmed fixed.
  LOG_INF("SLEEP", "onEnter: page=%dx%d", pageWidth, pageHeight);
  renderer.clearScreen();

  // Much bigger across the board than the original 96px/all-FONT_SMALL_ID layout — this screen
  // sits untouched for potentially hours, so it should read clearly from across a room, not just
  // up close. 400px matches sleep.bmp's native 400x400 resolution exactly — RubySpriteRenderer's
  // scale-to-fit never scales up, only down, so this is the largest size that stays pixel-crisp
  // instead of adding blank padding around a smaller image.
  constexpr int kPortraitSize = 400;
  const int portraitX = (pageWidth - kPortraitSize) / 2;
  const int portraitY = 40;
  LOG_INF("SLEEP", "before portrait: x=%d y=%d size=%d", portraitX, portraitY, kPortraitSize);
  RubySpriteRenderer::drawPortrait(renderer, portraitX, portraitY, kPortraitSize);
  LOG_INF("SLEEP", "after portrait");

  int y = portraitY + kPortraitSize + 30;
  renderer.drawCenteredText(FONT_UI_12_ID, y, "GONE QUIET", true, EpdFontFamily::BOLD);
  y += 32;
  renderer.drawCenteredText(FONT_UI_12_ID, y, fromTimeout ? "Auto-sleep - capture paused" : "Capture paused");
  y += 42;
  LOG_INF("SLEEP", "after headline text, y=%d", y);

  const auto& stats = SIGNAL_CATALOG.getStats();
  char line[64];
  snprintf(line, sizeof(line), "%lu unique devices catalogued", static_cast<unsigned long>(stats.uniqueWifiAPs +
                                                                                            stats.uniqueWifiClients +
                                                                                            stats.uniqueBleDevices));
  renderer.drawCenteredText(FONT_UI_10_ID, y, line);
  y += 26;
  snprintf(line, sizeof(line), "%lu handshakes captured", static_cast<unsigned long>(stats.handshakesCaptured));
  renderer.drawCenteredText(FONT_UI_10_ID, y, line);
  LOG_INF("SLEEP", "after stat text, y=%d", y);

  renderer.drawCenteredText(FONT_UI_10_ID, pageHeight - 50, "Hold power to wake", true, EpdFontFamily::BOLD);
  LOG_INF("SLEEP", "before displayBuffer");
  renderer.displayBuffer();
  LOG_INF("SLEEP", "after displayBuffer");
}
