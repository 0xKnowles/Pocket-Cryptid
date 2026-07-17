#include "SkinwalkerBehavior.h"

namespace SkinwalkerBehavior {

SkinwalkerExpression expressionFor(unsigned long msSinceLastCapture, unsigned long msSinceLastHandshake) {
  if (msSinceLastHandshake <= SkinwalkerConfig::kExcitedWindowMs) return SkinwalkerExpression::EXCITED;
  if (msSinceLastCapture <= SkinwalkerConfig::kCuriousWindowMs) return SkinwalkerExpression::CURIOUS;
  if (msSinceLastCapture <= SkinwalkerConfig::kContentWindowMs) return SkinwalkerExpression::CONTENT;
  if (msSinceLastCapture <= SkinwalkerConfig::kBoredWindowMs) return SkinwalkerExpression::BORED;
  return SkinwalkerExpression::LONELY;
}

const char* expressionLabel(SkinwalkerExpression expression) {
  switch (expression) {
    case SkinwalkerExpression::EXCITED:
      return "EXCITED";
    case SkinwalkerExpression::CURIOUS:
      return "CURIOUS";
    case SkinwalkerExpression::CONTENT:
      return "CONTENT";
    case SkinwalkerExpression::BORED:
      return "BORED";
    case SkinwalkerExpression::LONELY:
      return "LONELY";
    case SkinwalkerExpression::SLEEPING:
      return "DORMANT";
  }
  return "";
}

}  // namespace SkinwalkerBehavior
