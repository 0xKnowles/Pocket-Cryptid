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
  if (type == RfEventType::HandshakeCaptured) {
    lastHandshakeMillis = millis();
    justCapturedHandshake = true;
    state.totalExp += RubyConfig::kExpPerHandshake;
    LOG_INF("RUBY", "%s: handshake captured", state.designation);
  } else {
    state.totalExp += RubyConfig::kExpPerUniqueDevice;
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

uint8_t RubyManager::animFrame() const {
  constexpr unsigned long kFrameMs = 1200;
  return static_cast<uint8_t>((millis() / kFrameMs) % 2);
}

void RubyManager::toJson(JsonDocument& doc) const {
  doc["initialized"] = state.initialized;
  doc["birthUnixTime"] = state.birthUnixTime;
  doc["designation"] = state.designation;
  doc["totalExp"] = state.totalExp;
}

bool RubyManager::fromJson(JsonVariantConst doc) {
  state.initialized = doc["initialized"] | false;
  state.birthUnixTime = doc["birthUnixTime"] | 0;
  const char* designation = doc["designation"] | "";
  strncpy(state.designation, designation, sizeof(state.designation) - 1);
  state.totalExp = doc["totalExp"] | 0;
  return true;
}
