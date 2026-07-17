#include "Chrome.h"

#include <cstdio>

#include "fontIds.h"

namespace Chrome {

void drawHeader(GfxRenderer& renderer, const char* title, int batteryPercent) {
  renderer.drawText(FONT_UI_12_ID, kMarginX, 6, title, true, EpdFontFamily::BOLD);

  if (batteryPercent >= 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", batteryPercent);
    const int w = renderer.getTextWidth(FONT_UI_10_ID, buf);
    renderer.drawText(FONT_UI_10_ID, renderer.getScreenWidth() - kMarginX - w, 8, buf);
  }

  drawDivider(renderer, kHeaderHeight);
}

void drawFooterHints(GfxRenderer& renderer, const char* back, const char* confirm, const char* left,
                     const char* right) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int y = screenH - kFooterHeight + 4;
  drawDivider(renderer, screenH - kFooterHeight);

  const char* labels[4] = {back, left, right, confirm};
  const int slotW = (screenW - 2 * kMarginX) / 4;
  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int slotX = kMarginX + i * slotW;
    const int textW = renderer.getTextWidth(FONT_SMALL_ID, labels[i]);
    const int centeredX = slotX + (slotW - textW) / 2;
    renderer.drawText(FONT_SMALL_ID, centeredX, y, labels[i]);
  }
}

void drawDivider(const GfxRenderer& renderer, int y) { renderer.drawLine(0, y, renderer.getScreenWidth(), y, true); }

int drawStatRow(const GfxRenderer& renderer, int y, const char* label, const char* value, bool bold, int rightX) {
  constexpr int kRowHeight = 20;
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int edge = rightX > 0 ? rightX : renderer.getScreenWidth() - kMarginX;
  renderer.drawText(FONT_UI_10_ID, kMarginX, y, label, true, style);
  const int valueW = renderer.getTextWidth(FONT_UI_10_ID, value, style);
  renderer.drawText(FONT_UI_10_ID, edge - valueW, y, value, true, style);
  return y + kRowHeight;
}

int contentTop() { return kHeaderHeight + 8; }
int contentBottom(const GfxRenderer& renderer) { return renderer.getScreenHeight() - kFooterHeight - 4; }
int contentLeft() { return kMarginX; }
int contentRight(const GfxRenderer& renderer) { return renderer.getScreenWidth() - kMarginX; }

}  // namespace Chrome
