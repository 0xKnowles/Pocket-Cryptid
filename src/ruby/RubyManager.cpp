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
  // lastCaptureMillis/lastHandshakeMillis/recentEventTimes below are session-only timers
  // RubyBehavior uses for the live expression, not saved to disk. state.totalExp is the one
  // persisted field this touches — see RubyState.h's class comment for why a handshake and a
  // unique device are weighted so differently.
  lastCaptureMillis = millis();
  recentEventTimes[recentEventNext] = lastCaptureMillis;
  recentEventNext = (recentEventNext + 1) % RubyConfig::kRecentEventCapacity;

  const uint8_t levelBefore = level();
  if (type == RfEventType::HandshakeCaptured) {
    lastHandshakeMillis = millis();
    justCapturedHandshake = true;
    state.totalExp += RubyConfig::kExpPerHandshake;
    // Recorded here (not by DashboardActivity polling consumeJustCapturedHandshake()) so a
    // handshake captured while a different screen is active still lands in the persisted
    // history — skipped entirely if the wall clock has never been set, since 0 is this ring's
    // "slot never used" sentinel and a real capture must never be indistinguishable from one.
    const uint32_t nowUnix = static_cast<uint32_t>(time(nullptr));
    if (nowUnix != 0) {
      state.handshakeTimestamps[state.handshakeHistoryNext] = nowUnix;
      state.handshakeHistoryNext = static_cast<uint8_t>((state.handshakeHistoryNext + 1) % kHandshakeHistoryCapacity);
    }
    LOG_INF("RUBY", "%s: handshake captured", state.designation);
  } else {
    state.totalExp += RubyConfig::kExpPerUniqueDevice;
  }
  const uint8_t levelAfter = level();
  if (levelAfter > levelBefore) {
    justLeveledUpTo = levelAfter;
    LOG_INF("RUBY", "%s: leveled up to %u", state.designation, levelAfter);
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

float RubyManager::expProgress() const {
  const uint8_t lvl = level();
  if (lvl >= RubyConfig::kMaxLevel) return 1.0f;
  const uint32_t floor = RubyConfig::kLevelThresholds[lvl - 1];
  const uint32_t nextFloor = RubyConfig::kLevelThresholds[lvl];
  if (state.totalExp <= floor) return 0.0f;
  return static_cast<float>(state.totalExp - floor) / static_cast<float>(nextFloor - floor);
}

void RubyManager::expIntoLevel(uint32_t& intoLevel, uint32_t& neededForLevel) const {
  const uint8_t lvl = level();
  if (lvl >= RubyConfig::kMaxLevel) {
    intoLevel = 0;
    neededForLevel = 0;
    return;
  }
  const uint32_t floor = RubyConfig::kLevelThresholds[lvl - 1];
  const uint32_t nextFloor = RubyConfig::kLevelThresholds[lvl];
  intoLevel = state.totalExp - floor;
  neededForLevel = nextFloor - floor;
}

RubyExpression RubyManager::currentExpression(bool deviceSleeping) const {
  if (deviceSleeping) return RubyExpression::SLEEPING;
  const unsigned long now = millis();
  uint8_t burstCount = 0;
  for (unsigned long t : recentEventTimes) {
    if (t != 0 && now - t <= RubyConfig::kCuriousWindowMs) burstCount++;
  }
  return RubyBehavior::expressionFor(now - lastCaptureMillis, now - lastHandshakeMillis, burstCount);
}

bool RubyManager::consumeJustCapturedHandshake() {
  const bool result = justCapturedHandshake;
  justCapturedHandshake = false;
  return result;
}

bool RubyManager::consumeJustLeveledUp(uint8_t& newLevel) {
  if (justLeveledUpTo == 0) return false;
  newLevel = justLeveledUpTo;
  justLeveledUpTo = 0;
  return true;
}

void RubyManager::resetExp() {
  state.totalExp = 0;
  justLeveledUpTo = 0;
  memset(state.handshakeTimestamps, 0, sizeof(state.handshakeTimestamps));
  state.handshakeHistoryNext = 0;
  if (saveToFile()) {
    dirty = false;
    lastSaveMs = millis();
    LOG_INF("RUBY", "%s: EXP and handshake history reset", state.designation);
  } else {
    LOG_ERR("RUBY", "Failed to persist EXP/handshake-history reset");
  }
}

uint8_t RubyManager::animFrame() const {
  constexpr unsigned long kFrameMs = 1200;
  return static_cast<uint8_t>((millis() / kFrameMs) % 2);
}

void RubyManager::toJson(JsonDocument& doc) const {
  doc["initialized"] = state.initialized;
  doc["birthUnixTime"] = state.birthUnixTime;
  doc["designation"] = state.designation;
  doc["totalExp"] = state.totalExp;
  JsonArray handshakeTimes = doc["handshakeTimestamps"].to<JsonArray>();
  for (uint32_t t : state.handshakeTimestamps) handshakeTimes.add(t);
  doc["handshakeHistoryNext"] = state.handshakeHistoryNext;
}

bool RubyManager::fromJson(JsonVariantConst doc) {
  state.initialized = doc["initialized"] | false;
  state.birthUnixTime = doc["birthUnixTime"] | 0;
  const char* designation = doc["designation"] | "";
  strncpy(state.designation, designation, sizeof(state.designation) - 1);
  state.totalExp = doc["totalExp"] | 0;

  // Missing entirely on a state file saved before this ring existed — an empty/absent array here
  // just leaves every slot at its zero-initialized default, correctly read as "no captures yet".
  JsonArrayConst handshakeTimes = doc["handshakeTimestamps"];
  size_t i = 0;
  for (JsonVariantConst v : handshakeTimes) {
    if (i >= kHandshakeHistoryCapacity) break;
    state.handshakeTimestamps[i++] = v.as<uint32_t>();
  }
  state.handshakeHistoryNext = doc["handshakeHistoryNext"] | 0;
  return true;
}
