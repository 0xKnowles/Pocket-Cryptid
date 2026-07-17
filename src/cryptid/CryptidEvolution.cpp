#include "CryptidEvolution.h"

#include <cstddef>

namespace CryptidEvolution {

CryptidStage stageForXp(uint32_t xp) {
  uint8_t stage = 0;
  for (size_t i = 0; i < CryptidConfig::kStageCount; i++) {
    if (xp >= CryptidConfig::kStageXpThresholds[i]) {
      stage = static_cast<uint8_t>(i);
    }
  }
  return static_cast<CryptidStage>(stage);
}

uint32_t xpForEvent(RfEventType type) {
  switch (type) {
    case RfEventType::NewWifiAP:
      return CryptidConfig::kXpNewWifiAp;
    case RfEventType::NewWifiClient:
      return CryptidConfig::kXpNewWifiClient;
    case RfEventType::NewBleDevice:
      return CryptidConfig::kXpNewBleDevice;
    case RfEventType::HandshakeCaptured:
      return CryptidConfig::kXpHandshakeCaptured;
  }
  return 0;
}

uint32_t xpIntoCurrentStage(uint32_t xp) {
  const CryptidStage stage = stageForXp(xp);
  return xp - CryptidConfig::kStageXpThresholds[static_cast<uint8_t>(stage)];
}

uint32_t xpNeededForNextStage(uint32_t xp) {
  const uint8_t stage = static_cast<uint8_t>(stageForXp(xp));
  if (stage + 1 >= CryptidConfig::kStageCount) return 0;  // already APEX
  return CryptidConfig::kStageXpThresholds[stage + 1] - CryptidConfig::kStageXpThresholds[stage];
}

CryptidMood moodForDrought(unsigned long msSinceLastCapture) {
  if (msSinceLastCapture <= CryptidConfig::kMoodThrivingMs) return CryptidMood::THRIVING;
  if (msSinceLastCapture <= CryptidConfig::kMoodContentMs) return CryptidMood::CONTENT;
  if (msSinceLastCapture <= CryptidConfig::kMoodRestlessMs) return CryptidMood::RESTLESS;
  return CryptidMood::STARVING;
}

const char* stageName(CryptidStage stage) {
  switch (stage) {
    case CryptidStage::DORMANT:
      return "DORMANT SIGNAL";
    case CryptidStage::LARVA:
      return "STATIC LARVA";
    case CryptidStage::WRAITH:
      return "SIGNAL WRAITH";
    case CryptidStage::STALKER:
      return "BROADCAST STALKER";
    case CryptidStage::APEX:
      return "THE RELAY";
  }
  return "UNKNOWN";
}

const char* moodLabel(CryptidMood mood) {
  switch (mood) {
    case CryptidMood::THRIVING:
      return "FEEDING";
    case CryptidMood::CONTENT:
      return "CONTENT";
    case CryptidMood::RESTLESS:
      return "RESTLESS";
    case CryptidMood::STARVING:
      return "STARVING";
    case CryptidMood::ASLEEP:
      return "DORMANT";
  }
  return "";
}

}  // namespace CryptidEvolution
