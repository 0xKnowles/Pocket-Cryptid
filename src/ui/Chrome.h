#pragma once

#include <GfxRenderer.h>

// Shared dashboard chrome — header bar, footer button hints, section dividers. Pocket Cryptid
// has exactly one visual theme (the analog-horror "shadowy monster" look described in the
// README), so unlike upstream CrossPlant's UITheme this is not a themeable/JSON-configurable
// system — just a handful of small drawing helpers so every screen looks consistent without
// pulling in a whole theming engine that no setting ever changes.
namespace Chrome {

constexpr int kMarginX = 12;
constexpr int kHeaderHeight = 28;
constexpr int kFooterHeight = 22;

// Draws the title bar: bold title on the left, battery percentage on the right, thin rule below.
// batteryPercent < 0 hides the battery readout (used on screens where it'd be visual noise).
void drawHeader(const GfxRenderer& renderer, const char* title, int batteryPercent = -1);

// Draws a bottom row of up to 4 short button-hint labels, evenly spaced, in a small font. Pass
// nullptr/"" to skip a slot.
void drawFooterHints(const GfxRenderer& renderer, const char* back, const char* confirm, const char* left,
                     const char* right);

// A short horizontal rule, full content width, at logical y.
void drawDivider(const GfxRenderer& renderer, int y);

// A single "label ......... value" stat row, value right-aligned to `rightX` (screen width minus
// margin if <= 0 — pass an explicit column edge when the row must not run under other content,
// e.g. DashboardActivity's cryptid corner panel). Returns the y of the next row (y + row height)
// so callers can chain calls without recomputing layout by hand.
int drawStatRow(const GfxRenderer& renderer, int y, const char* label, const char* value, bool bold = false,
                int rightX = -1);

int contentTop();     // y just below the header
int contentBottom(const GfxRenderer& renderer);  // y just above the footer
int contentLeft();
int contentRight(const GfxRenderer& renderer);

}  // namespace Chrome
