#include "RubyBehavior.h"

namespace RubyBehavior {

RubyExpression expressionFor(unsigned long msSinceLastCapture, unsigned long msSinceLastHandshake) {
  if (msSinceLastHandshake <= RubyConfig::kExcitedWindowMs) return RubyExpression::EXCITED;
  if (msSinceLastCapture <= RubyConfig::kCuriousWindowMs) return RubyExpression::CURIOUS;
  if (msSinceLastCapture <= RubyConfig::kContentWindowMs) return RubyExpression::CONTENT;
  if (msSinceLastCapture <= RubyConfig::kBoredWindowMs) return RubyExpression::BORED;
  return RubyExpression::LONELY;
}

const char* expressionLabel(RubyExpression expression) {
  switch (expression) {
    case RubyExpression::EXCITED:
      return "EXCITED";
    case RubyExpression::CURIOUS:
      return "CURIOUS";
    case RubyExpression::CONTENT:
      return "CONTENT";
    case RubyExpression::BORED:
      return "BORED";
    case RubyExpression::LONELY:
      return "LONELY";
    case RubyExpression::SLEEPING:
      return "DORMANT";
  }
  return "";
}

}  // namespace RubyBehavior
