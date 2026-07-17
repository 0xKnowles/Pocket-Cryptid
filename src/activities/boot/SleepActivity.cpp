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

  constexpr int kPortraitSize = 96;
  const int portraitX = (pageWidth - kPortraitSize) / 2;
  const int portraitY = 60;
  RubySpriteRenderer::drawPortrait(renderer, portraitX, portraitY, kPortraitSize);

  const int textTop = portraitY + kPortraitSize + 20;
  renderer.drawCenteredText(FONT_UI_12_ID, textTop, "GONE QUIET", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(FONT_UI_10_ID, textTop + 26,
                            fromTimeout ? "Auto-sleep — capture paused" : "Capture paused");

  const auto& stats = SIGNAL_CATALOG.getStats();
  char line[64];
  int y = textTop + 60;
  snprintf(line, sizeof(line), "%lu unique devices catalogued", static_cast<unsigned long>(stats.uniqueWifiAPs +
                                                                                            stats.uniqueWifiClients +
                                                                                            stats.uniqueBleDevices));
  renderer.drawCenteredText(FONT_SMALL_ID, y, line);
  y += 18;
  snprintf(line, sizeof(line), "%lu handshakes captured", static_cast<unsigned long>(stats.handshakesCaptured));
  renderer.drawCenteredText(FONT_SMALL_ID, y, line);

  renderer.drawCenteredText(FONT_SMALL_ID, pageHeight - 30, "Hold power to wake");
  renderer.displayBuffer();
}
