#pragma once

#include "RubyState.h"

namespace RubyBehavior {

// Derives the current expression from how long ago each kind of event last happened, plus how
// many new-unique sightings landed within the CURIOUS window (see RubyConfig::kCuriousBurstThreshold
// — a single sighting reads as CONTENT's steady baseline, only a real burst reads as CURIOUS).
// Handshake recency wins over everything else (a handshake 20s ago should still read as EXCITED
// even mid-burst), which is why this takes both timers rather than one "last event" timestamp.
RubyExpression expressionFor(unsigned long msSinceLastCapture, unsigned long msSinceLastHandshake,
                             uint8_t recentSightingBurstCount);

const char* expressionLabel(RubyExpression expression);

}  // namespace RubyBehavior
