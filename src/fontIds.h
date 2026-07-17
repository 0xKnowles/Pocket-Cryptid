#pragma once

// Font IDs registered with GfxRenderer::insertFont() in main.cpp. Skinwalker only ships the
// Space Mono UI family (see lib/EpdFont/builtinFonts/all.h) — there is no reading surface, so
// unlike upstream CrossPlant these don't need to be content-hashed against a large generated font
// set; three stable small constants are enough.
constexpr int FONT_SMALL_ID = 1;   // Space Mono 8 Regular  — dense stat rows, footnotes
constexpr int FONT_UI_10_ID = 2;   // Space Mono 10 Regular/Bold — body text, list rows
constexpr int FONT_UI_12_ID = 3;   // Space Mono 12 Regular/Bold — headings, dashboard title

static_assert(FONT_SMALL_ID != 0 && FONT_UI_10_ID != 0 && FONT_UI_12_ID != 0,
             "Font ID 0 is reserved as the GfxRenderer 'not found' sentinel");
