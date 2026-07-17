#pragma once

#include "SkinwalkerState.h"

namespace SkinwalkerBehavior {

// Derives the current expression from how long ago each kind of event last happened. Handshake
// recency wins over general capture recency (a handshake 20s ago should still read as EXCITED
// even if a plain probe request landed 5s ago), which is why this takes both timers rather than
// one "last event" timestamp.
SkinwalkerExpression expressionFor(unsigned long msSinceLastCapture, unsigned long msSinceLastHandshake);

const char* expressionLabel(SkinwalkerExpression expression);

}  // namespace SkinwalkerBehavior
