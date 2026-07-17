#pragma once

#include <cstddef>
#include <cstdint>

#include "RubyState.h"
#include "embeddedArt/boot_bmp.h"
#include "embeddedArt/bored_bmp.h"
#include "embeddedArt/content_bmp.h"
#include "embeddedArt/curious_bmp.h"
#include "embeddedArt/excited_bmp.h"
#include "embeddedArt/lonely_bmp.h"
#include "embeddedArt/sleep_bmp.h"

// Ruby's expression art, baked directly into the firmware image (see
// scripts/generate_embedded_art.py) rather than requiring users to provision an SD card with a
// /bmp/ folder before it shows up. RubySpriteRenderer still checks the SD card first, so this is
// the always-available floor, not a replacement for that override path.
namespace RubyEmbeddedArt {

struct Asset {
  const uint8_t* data;
  size_t size;
};

inline Asset forExpression(RubyExpression expression) {
  switch (expression) {
    case RubyExpression::EXCITED:
      return {excited_bmp_data, sizeof(excited_bmp_data)};
    case RubyExpression::CURIOUS:
      return {curious_bmp_data, sizeof(curious_bmp_data)};
    case RubyExpression::CONTENT:
      return {content_bmp_data, sizeof(content_bmp_data)};
    case RubyExpression::BORED:
      return {bored_bmp_data, sizeof(bored_bmp_data)};
    case RubyExpression::LONELY:
      return {lonely_bmp_data, sizeof(lonely_bmp_data)};
    case RubyExpression::SLEEPING:
      return {sleep_bmp_data, sizeof(sleep_bmp_data)};
  }
  return {nullptr, 0};
}

// The full-screen boot splash — not tied to a RubyExpression, so it's a separate accessor rather
// than another switch case.
inline Asset boot() { return {boot_bmp_data, sizeof(boot_bmp_data)}; }

}  // namespace RubyEmbeddedArt
