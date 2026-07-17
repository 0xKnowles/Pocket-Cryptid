#pragma once

// Skinwalker ships Space Mono (SIL OFL 1.1, Google Fonts) as its only UI font family —
// dashboard chrome + stat text. A monospace terminal-style face reads as "instrument readout"
// rather than "app UI," which fits a device whose whole job is displaying raw RF telemetry. There
// is no book-reading surface, so the Lexend Deca / Bitter / Charein reading-typography families
// from upstream CrossPlant were dropped to keep firmware size and flash usage down (~57 MB of
// glyph tables removed).
#include <builtinFonts/space_mono_10_bold.h>
#include <builtinFonts/space_mono_10_regular.h>
#include <builtinFonts/space_mono_12_bold.h>
#include <builtinFonts/space_mono_12_regular.h>
#include <builtinFonts/space_mono_8_regular.h>
