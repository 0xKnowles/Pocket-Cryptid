#pragma once

#include <GfxRenderer.h>

// Shared dashboard chrome — header bar, footer tab bar, section dividers. Skinwalker has
// exactly one visual theme (an analog-horror "instrument readout" look: monospace type, rounded
// outline cards, pill-shaped tabs/badges), so unlike upstream CrossPlant's UITheme this is not a
// themeable/JSON-configurable system — just a handful of small drawing helpers so every screen
// looks consistent without pulling in a whole theming engine that no setting ever changes.
namespace Chrome {

constexpr int kMarginX = 12;
constexpr int kHeaderHeight = 28;
// Tall enough for a full line of FONT_SMALL_ID text inside a padded pill tab without clipping —
// the old 22px band clipped descenders on real hardware (see MappedInputManager.h for the other
// footer-related hardware bug this shipped alongside).
constexpr int kFooterHeight = 40;

// Shared rounded-corner radius for cards/tabs/badges, so every "boxed" element reads as the same
// visual language instead of each screen picking its own curvature.
constexpr int kCardRadius = 6;

// Draws the title bar: bold title on the left, battery percentage as a rounded pill badge on the
// right, thin rule below. batteryPercent < 0 hides the battery readout (used on screens where
// it'd be visual noise).
void drawHeader(const GfxRenderer& renderer, const char* title, int batteryPercent = -1);

// Draws a bottom row of up to 4 button-hint tabs — rounded-outline pills, evenly spaced with a
// visible gap between them so each one reads as its own pressable button rather than a slice of a
// solid bar. Pass nullptr/"" to skip a slot.
void drawFooterHints(const GfxRenderer& renderer, const char* back, const char* confirm, const char* left,
                     const char* right);

// A short horizontal rule, full content width, at logical y.
void drawDivider(const GfxRenderer& renderer, int y);

// A single "label ......... value" stat row, value right-aligned to `rightX` (screen width minus
// margin if <= 0 — pass an explicit column edge when the row must not run under other content,
// e.g. DashboardActivity's skinwalker corner panel). Returns the y of the next row (y + row height)
// so callers can chain calls without recomputing layout by hand.
int drawStatRow(const GfxRenderer& renderer, int y, const char* label, const char* value, bool bold = false,
                int rightX = -1);

// A rounded-outline "button" highlight box behind a selectable row (e.g. the focused row in
// Settings) — an outlined pill rather than a solid inverted fill, so it reads as "this is a
// button" without the harsh full-black flash a filled highlight causes on e-ink refresh.
void drawSelectionHighlight(const GfxRenderer& renderer, int x, int y, int width, int height);

int contentTop();     // y just below the header
int contentBottom(const GfxRenderer& renderer);  // y just above the footer
int contentLeft();
int contentRight(const GfxRenderer& renderer);

}  // namespace Chrome
