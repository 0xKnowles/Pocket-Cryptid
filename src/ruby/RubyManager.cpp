#include "RubyManager.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_mac.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
constexpr unsigned long kSaveIntervalMs = 20000;

// Flavor text unlocked one at a time as lifetime captures accumulate — see kCapturesPerLoreUnlock.
constexpr const char* kLoreEntries[] = {
    "It does not sleep. It waits for the next carrier wave.",
    "Every MAC address is a scent. It remembers all of them, badly.",
    "The static between channels is not empty. It was never empty.",
    "It noticed the first probe request. It does not know it was looking for a network that isn't here.",
    "Four packets, in the right order, and something in it goes very still, then very alert.",
    "It cannot see. It can only hear things that were never meant to be quiet.",
    "Somewhere nearby, a phone is asking for a network by name. It is listening to the question, not the answer.",
    "The encrypted log is the only place it keeps secrets from itself.",
    "It does not transmit. It has never transmitted. It only takes.",
    "Bluetooth Low Energy devices announce themselves every few hundred milliseconds, unprompted, forever. It finds this very sad.",
    "On channel 6, something is beaconing every 102.4 milliseconds. It has counted every one.",
    "It was not born. It was compiled, and then it started listening.",
    "The 2.4 GHz band is loud tonight. Its expression keeps changing.",
    "It does not know your name. It knows six bytes that might as well be.",
    "A handshake completed nearby. Somewhere, two devices agreed on a secret. It only heard that they spoke.",
    "This is the last thing it has learned. There will be more, eventually, from the air.",
};
constexpr size_t kLoreEntryCount = sizeof(kLoreEntries) / sizeof(kLoreEntries[0]);

}  // namespace

void RubyManager::begin() {
  if (!loadFromFile()) {
    state = RubyState{};
  }

  if (!state.initialized) {
    state.initialized = true;
    state.birthUnixTime = static_cast<uint32_t>(time(nullptr));

    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    snprintf(state.designation, sizeof(state.designation), "SPECIMEN-%02X%02X", mac[4], mac[5]);

    saveToFile();
  }

  birthMillis = millis();
  lastCaptureMillis = millis();  // start CONTENT rather than immediately LONELY after a reboot
  // Deliberately far in the past (relying on unsigned wraparound, safe across a millis() rollover
  // too): a fresh boot must not look like a handshake just happened.
  lastHandshakeMillis = millis() - RubyConfig::kExcitedWindowMs - 1;
}

void RubyManager::onSignalEvent(RfEventType type) {
  state.totalCaptures++;
  lastCaptureMillis = millis();
  if (type == RfEventType::HandshakeCaptured) {
    lastHandshakeMillis = millis();
    justCapturedHandshake = true;
    LOG_INF("RUBY", "%s: handshake captured", state.designation);
  }

  const uint16_t unlockable =
      static_cast<uint16_t>(state.totalCaptures / RubyConfig::kCapturesPerLoreUnlock);
  if (unlockable > state.unlockedLoreCount && state.unlockedLoreCount < kLoreEntryCount) {
    state.unlockedLoreCount = unlockable > kLoreEntryCount ? kLoreEntryCount : unlockable;
  }

  dirty = true;
}

void RubyManager::tick() {
  if (!dirty) return;
  const unsigned long now = millis();
  if (now - lastSaveMs < kSaveIntervalMs) return;
  if (saveToFile()) {
    dirty = false;
    lastSaveMs = now;
  }
}

RubyExpression RubyManager::currentExpression(bool deviceSleeping) const {
  if (deviceSleeping) return RubyExpression::SLEEPING;
  return RubyBehavior::expressionFor(millis() - lastCaptureMillis, millis() - lastHandshakeMillis);
}

bool RubyManager::consumeJustCapturedHandshake() {
  const bool result = justCapturedHandshake;
  justCapturedHandshake = false;
  return result;
}

uint8_t RubyManager::animFrame() const {
  constexpr unsigned long kFrameMs = 1200;
  return static_cast<uint8_t>((millis() / kFrameMs) % 2);
}

size_t RubyManager::loreEntryCount() { return kLoreEntryCount; }

const char* RubyManager::loreEntry(size_t index) const {
  if (index >= state.unlockedLoreCount || index >= kLoreEntryCount) return nullptr;
  return kLoreEntries[index];
}

void RubyManager::toJson(JsonDocument& doc) const {
  doc["initialized"] = state.initialized;
  doc["birthUnixTime"] = state.birthUnixTime;
  doc["totalCaptures"] = state.totalCaptures;
  doc["unlockedLoreCount"] = state.unlockedLoreCount;
  doc["designation"] = state.designation;
}

bool RubyManager::fromJson(JsonVariantConst doc) {
  state.initialized = doc["initialized"] | false;
  state.birthUnixTime = doc["birthUnixTime"] | 0;
  state.totalCaptures = doc["totalCaptures"] | 0;
  state.unlockedLoreCount = doc["unlockedLoreCount"] | 0;
  const char* designation = doc["designation"] | "";
  strncpy(state.designation, designation, sizeof(state.designation) - 1);
  return true;
}
