#pragma once

// Pocket Cryptid only ships the Inter UI font family (dashboard chrome + stat
// text) — there is no book-reading surface, so the Lexend Deca / Bitter /
// Charein reading-typography families from upstream CrossPlant were dropped
// to keep firmware size and flash usage down (~57 MB of glyph tables removed).
#include <builtinFonts/inter_10_bold.h>
#include <builtinFonts/inter_10_regular.h>
#include <builtinFonts/inter_12_bold.h>
#include <builtinFonts/inter_12_regular.h>
#include <builtinFonts/inter_8_regular.h>
