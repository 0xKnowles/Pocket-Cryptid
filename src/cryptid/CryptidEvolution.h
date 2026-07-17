#pragma once

#include "CryptidState.h"
#include "SignalCatalog.h"

namespace CryptidEvolution {

CryptidStage stageForXp(uint32_t xp);
uint32_t xpForEvent(RfEventType type);

// xpIntoStage / xpNeededForNextStage — for drawing a progress bar toward the next evolution.
// Both return 0 when already at the final stage.
uint32_t xpIntoCurrentStage(uint32_t xp);
uint32_t xpNeededForNextStage(uint32_t xp);

CryptidMood moodForDrought(unsigned long msSinceLastCapture);

const char* stageName(CryptidStage stage);
const char* moodLabel(CryptidMood mood);

}  // namespace CryptidEvolution
