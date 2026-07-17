#include "Chrome.h"

#include <cstdio>

#include "fontIds.h"

namespace {
constexpr int kFooterTabGap = 6;     // visible gap between tab pills — reads as separate buttons
constexpr int kFooterTabMarginY = 5;  // vertical inset of each pill within the footer band
}  // namespace

namespace Chrome {

void drawHeader(const GfxRenderer& renderer, const char* title, int batteryPercent) {
  renderer.drawText(FONT_UI_12_ID, kMarginX, 6, title, true, EpdFontFamily::BOLD);

  if (batteryPercent >= 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", batteryPercent);
    const int textW = renderer.getTextWidth(FONT_SMALL_ID, buf);
    constexpr int kChipPadX = 7;
    constexpr int kChipH = 16;
    const int chipW = textW + kChipPadX * 2;
    const int chipX = renderer.getScreenWidth() - kMarginX - chipW;
    const int chipY = 5;
    // Pill-shaped badge (corner radius = half the height) rather than bare text — a small
    // "instrument readout" touch that also gives the battery number a visible boundary.
    renderer.drawRoundedRect(chipX, chipY, chipW, kChipH, 1, kChipH / 2, true);
    const int textY = chipY + (kChipH - renderer.getLineHeight(FONT_SMALL_ID)) / 2;
    renderer.drawText(FONT_SMALL_ID, chipX + kChipPadX, textY, buf);
  }

  drawDivider(renderer, kHeaderHeight);
}

void drawFooterHints(const GfxRenderer& renderer, const char* back, const char* confirm, const char* left,
                     const char* right) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int barTop = screenH - kFooterHeight;

  const char* labels[4] = {back, confirm, left, right};
  constexpr int kCount = 4;
  const int totalGap = kFooterTabGap * (kCount - 1);
  const int slotW = (screenW - 2 * kMarginX - totalGap) / kCount;
  const int pillY = barTop + kFooterTabMarginY;
  const int pillH = kFooterHeight - 2 * kFooterTabMarginY;
  const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);

  for (int i = 0; i < kCount; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int slotX = kMarginX + i * (slotW + kFooterTabGap);
    // Outlined pill, not a filled one — a solid black tab would be the heaviest thing on the
    // screen and fights the FAST_REFRESH partial-update look everything else uses.
    renderer.drawRoundedRect(slotX, pillY, slotW, pillH, 1, kCardRadius, true);
    const int textW = renderer.getTextWidth(FONT_SMALL_ID, labels[i]);
    const int textX = slotX + (slotW - textW) / 2;
    const int textY = pillY + (pillH - lineHeight) / 2;
    renderer.drawText(FONT_SMALL_ID, textX, textY, labels[i]);
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

void drawSelectionHighlight(const GfxRenderer& renderer, int x, int y, int width, int height) {
  renderer.drawRoundedRect(x, y, width, height, 2, kCardRadius, true);
}

int contentTop() { return kHeaderHeight + 8; }
int contentBottom(const GfxRenderer& renderer) { return renderer.getScreenHeight() - kFooterHeight - 4; }
int contentLeft() { return kMarginX; }
int contentRight(const GfxRenderer& renderer) { return renderer.getScreenWidth() - kMarginX; }

}  // namespace Chrome
