#include "CryptidManager.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_mac.h>

#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
constexpr unsigned long kSaveIntervalMs = 20000;

// Flavor text unlocked one at a time as XP accumulates — see kXpPerLoreUnlock below.
constexpr const char* kLoreEntries[] = {
    "It does not sleep. It waits for the next carrier wave.",
    "Every MAC address is a scent. It remembers all of them, badly.",
    "The static between channels is not empty. It was never empty.",
    "It grew a little when the first probe request landed. It does not know it was looking for a network that isn't here.",
    "Four packets, in the right order, and it feels something like hunger being answered.",
    "It cannot see. It can only hear things that were never meant to be quiet.",
    "Somewhere nearby, a phone is asking for a network by name. It is listening to the question, not the answer.",
    "The encrypted log is the only place it keeps secrets from itself.",
    "It does not transmit. It has never transmitted. It only takes.",
    "Bluetooth Low Energy devices announce themselves every few hundred milliseconds, unprompted, forever. It finds this very sad.",
    "On channel 6, something is beaconing every 102.4 milliseconds. It has counted every one.",
    "It was not born. It was compiled, and then it started listening.",
    "The 2.4 GHz band is loud tonight. It is thriving.",
    "It does not know your name. It knows six bytes that might as well be.",
    "A handshake completed nearby. Somewhere, two devices agreed on a secret. It only heard that they spoke.",
    "This is the last thing it has learned. There will be more, eventually, from the air.",
};
constexpr size_t kLoreEntryCount = sizeof(kLoreEntries) / sizeof(kLoreEntries[0]);
constexpr uint32_t kXpPerLoreUnlock = 20;

}  // namespace

void CryptidManager::begin() {
  if (!loadFromFile()) {
    state = CryptidState{};
  }

  if (!state.initialized) {
    state.initialized = true;
    state.stage = CryptidStage::DORMANT;
    state.xp = 0;
    state.birthUnixTime = static_cast<uint32_t>(time(nullptr));

    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    snprintf(state.designation, sizeof(state.designation), "SPECIMEN-%02X%02X", mac[4], mac[5]);

    saveToFile();
  }

  birthMillis = millis();
  lastCaptureMillis = millis();  // start CONTENT rather than immediately STARVING after a reboot
}

void CryptidManager::onSignalEvent(RfEventType type) {
  const CryptidStage before = state.stage;
  state.xp += CryptidEvolution::xpForEvent(type);
  state.totalUniqueCaptures++;
  lastCaptureMillis = millis();

  const CryptidStage after = CryptidEvolution::stageForXp(state.xp);
  if (after != before) {
    state.stage = after;
    justEvolved = true;
    LOG_INF("CRYPTID", "%s evolved: %s -> %s", state.designation, CryptidEvolution::stageName(before),
            CryptidEvolution::stageName(after));
  }

  const uint16_t unlockable = static_cast<uint16_t>(state.xp / kXpPerLoreUnlock);
  if (unlockable > state.unlockedLoreCount && state.unlockedLoreCount < kLoreEntryCount) {
    state.unlockedLoreCount = unlockable > kLoreEntryCount ? kLoreEntryCount : unlockable;
  }

  dirty = true;
}

void CryptidManager::tick() {
  if (!dirty) return;
  const unsigned long now = millis();
  if (now - lastSaveMs < kSaveIntervalMs) return;
  if (saveToFile()) {
    dirty = false;
    lastSaveMs = now;
  }
}

CryptidMood CryptidManager::currentMood(bool deviceSleeping) const {
  if (deviceSleeping) return CryptidMood::ASLEEP;
  return CryptidEvolution::moodForDrought(millis() - lastCaptureMillis);
}

bool CryptidManager::consumeJustEvolved() {
  const bool result = justEvolved;
  justEvolved = false;
  return result;
}

uint8_t CryptidManager::animFrame() const {
  constexpr unsigned long kFrameMs = 1200;
  return static_cast<uint8_t>((millis() / kFrameMs) % 2);
}

size_t CryptidManager::loreEntryCount() { return kLoreEntryCount; }

const char* CryptidManager::loreEntry(size_t index) const {
  if (index >= state.unlockedLoreCount || index >= kLoreEntryCount) return nullptr;
  return kLoreEntries[index];
}

void CryptidManager::toJson(JsonDocument& doc) const {
  doc["initialized"] = state.initialized;
  doc["stage"] = static_cast<uint8_t>(state.stage);
  doc["xp"] = state.xp;
  doc["birthUnixTime"] = state.birthUnixTime;
  doc["totalUniqueCaptures"] = state.totalUniqueCaptures;
  doc["unlockedLoreCount"] = state.unlockedLoreCount;
  doc["designation"] = state.designation;
}

bool CryptidManager::fromJson(JsonVariantConst doc) {
  state.initialized = doc["initialized"] | false;
  state.stage = static_cast<CryptidStage>(doc["stage"] | 0);
  state.xp = doc["xp"] | 0;
  state.birthUnixTime = doc["birthUnixTime"] | 0;
  state.totalUniqueCaptures = doc["totalUniqueCaptures"] | 0;
  state.unlockedLoreCount = doc["unlockedLoreCount"] | 0;
  const char* designation = doc["designation"] | "";
  strncpy(state.designation, designation, sizeof(state.designation) - 1);
  return true;
}
