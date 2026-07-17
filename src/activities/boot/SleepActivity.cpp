#include "SleepActivity.h"

#include <GfxRenderer.h>

#include <cstdio>

#include "SignalCatalog.h"
#include "fontIds.h"
#include "ruby/RubySpriteRenderer.h"
#include "ui/Chrome.h"

void SleepActivity::onEnter() {
  Activity::onEnter();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  // Much bigger across the board than the original 96px/all-FONT_SMALL_ID layout — this screen
  // sits untouched for potentially hours, so it should read clearly from across a room, not just
  // up close.
  constexpr int kPortraitSize = 280;
  const int portraitX = (pageWidth - kPortraitSize) / 2;
  const int portraitY = 90;
  RubySpriteRenderer::drawPortrait(renderer, portraitX, portraitY, kPortraitSize);

  int y = portraitY + kPortraitSize + 30;
  renderer.drawCenteredText(FONT_UI_12_ID, y, "GONE QUIET", true, EpdFontFamily::BOLD);
  y += 32;
  renderer.drawCenteredText(FONT_UI_12_ID, y, fromTimeout ? "Auto-sleep - capture paused" : "Capture paused");
  y += 42;

  const auto& stats = SIGNAL_CATALOG.getStats();
  char line[64];
  snprintf(line, sizeof(line), "%lu unique devices catalogued", static_cast<unsigned long>(stats.uniqueWifiAPs +
                                                                                            stats.uniqueWifiClients +
                                                                                            stats.uniqueBleDevices));
  renderer.drawCenteredText(FONT_UI_10_ID, y, line);
  y += 26;
  snprintf(line, sizeof(line), "%lu handshakes captured", static_cast<unsigned long>(stats.handshakesCaptured));
  renderer.drawCenteredText(FONT_UI_10_ID, y, line);

  renderer.drawCenteredText(FONT_UI_10_ID, pageHeight - 50, "Hold power to wake", true, EpdFontFamily::BOLD);
  renderer.displayBuffer();
}
