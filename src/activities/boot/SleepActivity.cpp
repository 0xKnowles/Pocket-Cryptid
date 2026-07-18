#include "SleepActivity.h"

#include <GfxRenderer.h>

#include <cstdio>

#include "SignalCatalog.h"
#include "fontIds.h"
#include "ruby/RubySpriteRenderer.h"

void SleepActivity::onEnter() {
  Activity::onEnter();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  // Landscape has much more width than height to work with, so the portrait and the summary text
  // sit side by side instead of stacked — that's what lets the portrait stay at its native
  // 400x400 resolution (RubySpriteRenderer::drawPortrait never scales up, only down) rather than
  // having to shrink it to fit a canvas that's now shorter than the art is tall.
  constexpr int kPortraitSize = 400;
  const int portraitX = 40;
  const int portraitY = (pageHeight - kPortraitSize) / 2;
  RubySpriteRenderer::drawPortrait(renderer, portraitX, portraitY, kPortraitSize);

  const int textLeft = portraitX + kPortraitSize + 40;
  int y = pageHeight / 2 - 70;
  renderer.drawText(FONT_UI_12_ID, textLeft, y, "GONE QUIET", true, EpdFontFamily::BOLD);
  y += 28;
  renderer.drawText(FONT_UI_10_ID, textLeft, y, fromTimeout ? "Auto-sleep - capture paused" : "Capture paused");
  y += 40;

  // Truncated to the actual remaining width rather than drawn raw: at 2+ digits (any session with
  // more than a handful of sightings) the untruncated string ran past the screen's right edge,
  // since drawText has no wrapping and this layout's text column is narrower than the full screen.
  const int maxTextWidth = pageWidth - textLeft - 20;
  const auto& stats = SIGNAL_CATALOG.getStats();
  char line[64];
  snprintf(line, sizeof(line), "%lu unique devices catalogued", static_cast<unsigned long>(stats.uniqueWifiAPs +
                                                                                            stats.uniqueWifiClients +
                                                                                            stats.uniqueBleDevices));
  renderer.drawText(FONT_UI_10_ID, textLeft, y, renderer.truncatedText(FONT_UI_10_ID, line, maxTextWidth).c_str());
  y += 26;
  snprintf(line, sizeof(line), "%lu handshakes captured", static_cast<unsigned long>(stats.handshakesCaptured));
  renderer.drawText(FONT_UI_10_ID, textLeft, y, renderer.truncatedText(FONT_UI_10_ID, line, maxTextWidth).c_str());
  y += 50;

  renderer.drawText(FONT_UI_10_ID, textLeft, y, "Hold power to wake", true, EpdFontFamily::BOLD);

  // Full, not fast, refresh — this is the one screen meant to sit unchanged on the panel for
  // hours at a time (until a long power-button press wakes it), so it's worth the slower flash to
  // actually clear whatever was on screen before (typically the Dashboard) via a complete waveform
  // cycle. FAST_REFRESH's partial-update LUT leaves exactly that kind of prior content visible as
  // ghosting/burn-in for the entire time the device sits asleep, which is what this was doing
  // before this fix — see periodic-wake comment above for the same fight-ghosting intent.
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}
