#pragma once

#include <cstdint>

#include "LogRecord.h"
#include "RubyState.h"

// Flavor text for Ruby's mood/reaction bubbles (see RubySpriteRenderer.h and Chrome::drawThoughtBubble
// / drawSpeechBubble). Kept as pure lookup functions, not a class, since none of this needs state —
// every quip is either a fixed function of `expression` alone (the ambient thought bubble) or picked
// from a small fixed set by a caller-supplied `variant` (the one-shot event reactions), never derived
// from wall-clock time or anything else that would vary between two back-to-back identical redraws.
namespace RubyThoughts {

// Ambient, always-on quip reflecting Ruby's current mood — deliberately the *same* string every call
// for a given expression (a pure function of it), so it never disturbs RubySpriteRenderer's
// "identical inputs -> identical pixels" contract for the box's own fast partial-refresh tick.
// Returns "" for SLEEPING (no thought bubble while asleep, matching the name chip's own behavior).
const char* thoughtFor(RubyExpression expression);

// Number of options `variant` can select between for every speechFor*() function below — callers
// pick which one with e.g. `millis() % kVariantCount` once, at the moment the reaction fires, and
// hold onto the returned pointer for as long as the bubble stays on screen (same one-shot pattern
// DashboardActivity's timed banners already use), rather than re-picking on every redraw.
constexpr uint8_t kVariantCount = 3;

const char* speechForNewDevice(LogRecordType type, uint8_t variant);
const char* speechForHandshake(uint8_t variant);
const char* speechForLevelUp(uint8_t variant);
const char* speechForDeauthAlert(uint8_t variant);

}  // namespace RubyThoughts
