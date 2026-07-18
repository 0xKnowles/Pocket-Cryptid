#pragma once

#include <GfxRenderer.h>

#include <cstdint>

// Shared dashboard chrome — header bar, footer tab bar, section dividers. Ruby has
// exactly one visual theme (an analog-horror "instrument readout" look: monospace type, rounded
// outline cards, pill-shaped tabs/badges), so unlike upstream CrossPlant's UITheme this is not a
// themeable/JSON-configurable system — just a handful of small drawing helpers so every screen
// looks consistent without pulling in a whole theming engine that no setting ever changes.
namespace Chrome {

constexpr int kMarginX = 12;
// Titles draw with FONT_UI_12_ID BOLD at a fixed y=6 (see drawHeader() below); that font's real
// ascender is 28px, so a title's baseline lands at y=34 — well past the old kHeaderHeight of 28,
// which put the divider rule right through the lower quarter of every header title's glyphs
// (most visible on screens with a real title, e.g. "DEVICE LOG"/"LOG VIEWER" — Dashboard's empty
// title hid it). 40 leaves the baseline (34) a clean 6px above the divider.
constexpr int kHeaderHeight = 40;
// Tall enough for a full line of FONT_SMALL_ID text inside a padded pill tab without clipping —
// the old 22px band clipped descenders on real hardware (see MappedInputManager.h for the other
// footer-related hardware bug this shipped alongside).
constexpr int kFooterHeight = 40;

// Shared rounded-corner radius for cards/tabs/badges, so every "boxed" element reads as the same
// visual language instead of each screen picking its own curvature.
constexpr int kCardRadius = 6;

// Draws the title bar: battery percentage as a rounded pill badge on the left, bold title
// right-aligned to the content edge, thin rule below. batteryPercent < 0 hides the battery
// readout (used on screens where it'd be visual noise). Returns the x just past the battery
// badge (or kMarginX if it wasn't drawn) — callers with a blank title (Dashboard) can use that to
// place more content in the header row without recomputing the badge's width themselves.
int drawHeader(const GfxRenderer& renderer, const char* title, int batteryPercent = -1);

// Draws a bottom row of up to 4 button-hint tabs — rounded-outline pills, evenly spaced with a
// visible gap between them so each one reads as its own pressable button rather than a slice of a
// solid bar. Pass nullptr/"" to skip a slot.
void drawFooterHints(const GfxRenderer& renderer, const char* back, const char* confirm, const char* left,
                     const char* right);

// A short horizontal rule, full content width, at logical y.
void drawDivider(const GfxRenderer& renderer, int y);

// A single "label ......... value" stat row: label starts at `leftX` (the shared left margin if
// <= 0), value right-aligned to `rightX` (content-area right edge if <= 0). Pass explicit column
// edges for anything narrower than the full content width — e.g. DashboardActivity's side-by-side
// SIGNALS/CAPTURE STATUS columns — otherwise both rows in a two-column layout draw their labels
// at the same global-left-margin x regardless of which column they're actually in. Returns the y
// of the next row (y + row height) so callers can chain calls without recomputing layout by hand.
int drawStatRow(const GfxRenderer& renderer, int y, const char* label, const char* value, bool bold = false,
                int rightX = -1, int leftX = -1);

// A rounded-outline "button" highlight box behind a selectable row (e.g. the focused row in
// Settings) — an outlined pill rather than a solid inverted fill, so it reads as "this is a
// button" without the harsh full-black flash a filled highlight causes on e-ink refresh.
void drawSelectionHighlight(const GfxRenderer& renderer, int x, int y, int width, int height);

// Compact 4-bar signal-strength icon (like a phone's WiFi/cell bars), drawn with plain fillRect
// rather than a font glyph — Space Mono has no signal-bar character, and a hand-drawn icon
// sidesteps needing to verify one exists in the font's Unicode intervals at all. Only filled bars
// are drawn (unfilled ones are left blank, not outlined): at this icon's necessarily tiny scale,
// an outlined bar only 2-3px wide has no visible hollow interior — it reads as solid, making
// "filled" and "empty" indistinguishable. Floors at 1 bar rather than 0, since every call here
// represents a frame Ruby actually heard — a real, if weak, reading, not "no signal." Thresholds
// are tuned for typical WiFi/BLE RSSI ranges — this device only ever sees RSSI in that same dBm
// scale either way, so one mapping serves both observation types. Occupies exactly
// (kSignalBarsWidth, kSignalBarsHeight) starting at (x, y), regardless of how many bars are lit.
constexpr int kSignalBarsWidth = 15;
constexpr int kSignalBarsHeight = 13;
void drawSignalBars(const GfxRenderer& renderer, int x, int y, int8_t rssi);

// Bubble chrome for Ruby's mood/reaction callouts (see RubySpriteRenderer.h) — a white-filled,
// black-outlined rounded box (so it stays legible over whatever's underneath: bitmap art, the
// procedural silhouette, or its static noise) sized to fit up to kBubbleMaxLines wrapped lines of
// FONT_SMALL_ID text, at (x, y). (tailX, tailY) is a point on Ruby's own face the bubble visibly
// "belongs" to, regardless of exactly where it's anchored — the tail/dots sprout from whichever of
// the box's top or bottom edges sits closer to that point, so the same call works whether the
// bubble is anchored above or below the target (e.g. a bubble sitting near the bottom of Ruby's
// box, tailing up into her face, vs. one near the top, tailing down). Speech gets a solid
// triangular pointer (the classic "I'm saying this" convention, used for one-shot event
// reactions); thought gets two shrinking trailing dots instead (the classic "I'm thinking this"
// convention, used for the ambient mood-driven quip) — same box chrome either way, only the tail
// differs.
void drawSpeechBubble(const GfxRenderer& renderer, int x, int y, int width, int tailX, int tailY, const char* text);
void drawThoughtBubble(const GfxRenderer& renderer, int x, int y, int width, int tailX, int tailY, const char* text);

// Exposed so callers can reserve the box's worst-case (2-line) height before drawing, when they
// need to anchor the bubble's position by its bottom edge (e.g. flush against the bottom of a
// container) rather than its top-left corner.
constexpr int kBubblePadY = 4;
constexpr int kBubbleMaxLines = 2;

int contentTop();     // y just below the header
int contentBottom(const GfxRenderer& renderer);  // y just above the footer
int contentLeft();
int contentRight(const GfxRenderer& renderer);

}  // namespace Chrome
