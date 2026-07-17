#include "Chrome.h"

#include <cstdio>
#include <cstring>

#include "fontIds.h"

namespace {
constexpr int kFooterTabGap = 6;     // visible gap between tab pills — reads as separate buttons
constexpr int kFooterTabMarginY = 5;  // vertical inset of each pill within the footer band

// True for the one landscape orientation this firmware actually uses (LandscapeCounterClockwise
// — see GfxRenderer::Orientation, "native panel orientation"). Checked via aspect ratio rather
// than the enum directly since every screen-shape decision in this file cares about "is this the
// wide layout" and derives everything else (which edge the button row lands on, in what order)
// from the same physical-geometry reasoning below — not about tracking every possible
// orientation enum value in parallel.
bool isLandscape(const GfxRenderer& renderer) { return renderer.getScreenWidth() > renderer.getScreenHeight(); }

// Reverses a short ASCII string in place into `out` (labels here are always plain words like
// "SETTINGS" — no UTF-8 multi-byte handling needed). Used only for the rotated sidebar text
// below, so a top-to-bottom reading order still spells the label forwards — see the comment on
// the landscape branch of drawFooterHints() for why that needs reversing at all.
void reverseAscii(const char* in, char* out, size_t outSize) {
  const size_t len = strlen(in);
  const size_t n = len < outSize - 1 ? len : outSize - 1;
  for (size_t i = 0; i < n; i++) out[i] = in[len - 1 - i];
  out[n] = '\0';
}
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
  const char* labels[4] = {back, confirm, left, right};
  constexpr int kCount = 4;
  const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);

  if (isLandscape(renderer)) {
    // In landscape, the physical button row lands along the panel's far edge — the same edge
    // that sits at the *bottom* of the portrait layout below, per Portrait's coordinate rotation
    // (logical portrait y maps onto that physical edge) — which works out to the *right* edge
    // once the panel is shown in its native (LandscapeCounterClockwise) orientation instead. So
    // the hint pills stack vertically there, with their labels rotated 90° to read top-to-bottom
    // in a narrow strip rather than left-to-right in a wide one. Stacking order is reversed
    // relative to the portrait row (top of the sidebar = the portrait row's rightmost slot) for
    // the same coordinate-rotation reason. drawTextRotated90CW renders forward-order text
    // growing *upward*, so each label is reversed before drawing — read top-to-bottom, that
    // still spells it forwards.
    const int barLeft = screenW - kFooterHeight;
    const int totalGap = kFooterTabGap * (kCount - 1);
    const int slotH = (screenH - 2 * kMarginX - totalGap) / kCount;
    const int pillX = barLeft + kFooterTabMarginY;
    const int pillW = kFooterHeight - 2 * kFooterTabMarginY;

    for (int i = 0; i < kCount; i++) {
      const int labelIdx = kCount - 1 - i;
      const char* label = labels[labelIdx];
      if (label == nullptr || label[0] == '\0') continue;
      const int slotY = kMarginX + i * (slotH + kFooterTabGap);
      renderer.drawRoundedRect(pillX, slotY, pillW, slotH, 1, kCardRadius, true);

      char reversed[16];
      reverseAscii(label, reversed, sizeof(reversed));
      const int textLen = renderer.getTextWidth(FONT_SMALL_ID, reversed);  // becomes vertical extent, rotated
      const int textX = pillX + (pillW - lineHeight) / 2;
      const int textY = slotY + (slotH + textLen) / 2;
      renderer.drawTextRotated90CW(FONT_SMALL_ID, textX, textY, reversed);
    }
    return;
  }

  const int barTop = screenH - kFooterHeight;
  const int totalGap = kFooterTabGap * (kCount - 1);
  const int slotW = (screenW - 2 * kMarginX - totalGap) / kCount;
  const int pillY = barTop + kFooterTabMarginY;
  const int pillH = kFooterHeight - 2 * kFooterTabMarginY;

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
  // Defaults to the shared content-area right edge (not the raw screen edge) so callers that
  // don't pass an explicit column boundary still respect the landscape sidebar reservation — see
  // contentRight() below.
  const int edge = rightX > 0 ? rightX : contentRight(renderer);
  renderer.drawText(FONT_UI_10_ID, kMarginX, y, label, true, style);
  const int valueW = renderer.getTextWidth(FONT_UI_10_ID, value, style);
  renderer.drawText(FONT_UI_10_ID, edge - valueW, y, value, true, style);
  return y + kRowHeight;
}

void drawSelectionHighlight(const GfxRenderer& renderer, int x, int y, int width, int height) {
  renderer.drawRoundedRect(x, y, width, height, 2, kCardRadius, true);
}

int contentTop() { return kHeaderHeight + 8; }

int contentBottom(const GfxRenderer& renderer) {
  // Landscape has no bottom bar to leave room for — the button hints live in the right-edge
  // sidebar instead (see drawFooterHints) — so content can use nearly the full screen height.
  if (isLandscape(renderer)) return renderer.getScreenHeight() - 4;
  return renderer.getScreenHeight() - kFooterHeight - 4;
}

int contentLeft() { return kMarginX; }

int contentRight(const GfxRenderer& renderer) {
  if (isLandscape(renderer)) return renderer.getScreenWidth() - kFooterHeight - kMarginX;
  return renderer.getScreenWidth() - kMarginX;
}

}  // namespace Chrome
