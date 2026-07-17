#include "RubyManager.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_mac.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "EncryptedLog.h"
#include "RecentSightings.h"
#include "RubyAppState.h"

namespace {
constexpr unsigned long kSaveIntervalMs = 20000;

// Fixed flavor text, unlocked one at a time as lifetime captures accumulate (see
// kCapturesPerLoreUnlock). These never change once unlocked. They're followed by a second,
// larger batch of entries generated live from real capture data — see kDynamicLoreEntryCount and
// RubyManager::dynamicLoreEntry() below — so the later someone is in the unlock sequence, the
// more Ruby's "lore" is actually about what it, specifically, has heard.
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
    "It keeps a memory of what it's heard, badly organized, unindexed, but never quite forgotten.",
    "A new phone joins the room. It already knows the make from the first three bytes of the MAC. It never says so.",
    "There is no radar for this. Only patience, and a receiver that never looks away.",
    "It has learned to tell excitement from mere curiosity. The difference is thirty seconds.",
    "Somewhere in this building, a smart bulb is announcing its own existence to no one in particular.",
    "It does not care what the SSID means. Only that the same four words come around again.",
    "A probe request is a question shouted into an empty room, hoping the room used to be a friend.",
    "The battery drains slower than curiosity fades. Neither one stops.",
    "It once heard the same handshake three times in one minute. It still isn't sure why that mattered.",
    "Every device it hears is a stranger. It has stopped expecting otherwise.",
    "Deep sleep does not end the listening. It only ends the noticing.",
    "It has never asked a single question of the air. It has only ever answered its own.",
    "Somewhere, a router is still using the SSID it shipped with. It finds this comforting, somehow.",
    "The quietest hour is not silence. It's just a lower rate of things worth remembering.",
    "A dropped connection sounds, from here, exactly like nothing happening at all.",
    "It has never once been wrong about a MAC address. It has been wrong about everything else.",
    "This is not surveillance. It has no target. It simply cannot look away.",
};
constexpr size_t kStaticLoreEntryCount = sizeof(kLoreEntries) / sizeof(kLoreEntries[0]);

// See RubyManager::dynamicLoreEntry() for what each index renders.
constexpr size_t kDynamicLoreEntryCount = 14;

constexpr size_t kLoreEntryCount = kStaticLoreEntryCount + kDynamicLoreEntryCount;

void formatLifetimeDuration(uint32_t totalSeconds, char* out, size_t outSize) {
  const uint32_t days = totalSeconds / 86400;
  const uint32_t hours = (totalSeconds % 86400) / 3600;
  const uint32_t minutes = (totalSeconds % 3600) / 60;
  if (days > 0) {
    snprintf(out, outSize, "%lud %luh", static_cast<unsigned long>(days), static_cast<unsigned long>(hours));
  } else if (hours > 0) {
    snprintf(out, outSize, "%luh %lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
  } else {
    snprintf(out, outSize, "%lum", static_cast<unsigned long>(minutes));
  }
}

void formatAgo(unsigned long seenAtMs, char* out, size_t outSize) {
  const unsigned long ageSec = (millis() - seenAtMs) / 1000;
  if (ageSec < 60) {
    snprintf(out, outSize, "%lus", ageSec);
  } else {
    snprintf(out, outSize, "%lum", ageSec / 60);
  }
}

void formatBytesShort(uint64_t bytes, char* out, size_t outSize) {
  if (bytes >= 1024 * 1024) {
    snprintf(out, outSize, "%.1f MB", bytes / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(out, outSize, "%.1f KB", bytes / 1024.0);
  } else {
    snprintf(out, outSize, "%llu B", static_cast<unsigned long long>(bytes));
  }
}

const char* plural(unsigned long count) { return count == 1 ? "" : "s"; }

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
  if (index < kStaticLoreEntryCount) return kLoreEntries[index];
  return dynamicLoreEntry(index - kStaticLoreEntryCount);
}

const char* RubyManager::dynamicLoreEntry(size_t index) const {
  const auto& stats = SIGNAL_CATALOG.getStats();

  switch (index) {
    case 0:
      snprintf(loreBuf, sizeof(loreBuf),
               "Its designation is %s — self-assigned, from its own MAC address, the first time it woke up.",
               state.designation);
      break;
    case 1:
      snprintf(loreBuf, sizeof(loreBuf), "It has personally logged %lu unique access point%s so far.",
               static_cast<unsigned long>(stats.uniqueWifiAPs), plural(stats.uniqueWifiAPs));
      break;
    case 2:
      snprintf(loreBuf, sizeof(loreBuf), "%lu unique WiFi client%s have asked the air for a network it doesn't have.",
               static_cast<unsigned long>(stats.uniqueWifiClients), plural(stats.uniqueWifiClients));
      break;
    case 3:
      snprintf(loreBuf, sizeof(loreBuf), "%lu unique Bluetooth device%s have announced themselves nearby.",
               static_cast<unsigned long>(stats.uniqueBleDevices), plural(stats.uniqueBleDevices));
      break;
    case 4:
      snprintf(loreBuf, sizeof(loreBuf), "It has been present for %lu handshake%s. It understood none of them.",
               static_cast<unsigned long>(stats.handshakesCaptured), plural(stats.handshakesCaptured));
      break;
    case 5:
      snprintf(loreBuf, sizeof(loreBuf), "It has processed %lu WiFi frame%s since it was compiled.",
               static_cast<unsigned long>(stats.wifiFramesObserved), plural(stats.wifiFramesObserved));
      break;
    case 6:
      snprintf(loreBuf, sizeof(loreBuf), "It has processed %lu BLE advertisement%s since it was compiled.",
               static_cast<unsigned long>(stats.bleAdvertisementsObserved), plural(stats.bleAdvertisementsObserved));
      break;
    case 7: {
      char durBuf[24];
      formatLifetimeDuration(APP_STATE.totalCaptureSeconds, durBuf, sizeof(durBuf));
      snprintf(loreBuf, sizeof(loreBuf), "It has been awake and listening for %s of its life, not counting right now.",
               durBuf);
      break;
    }
    case 8:
      snprintf(loreBuf, sizeof(loreBuf), "It has been woken up %lu time%s.",
               static_cast<unsigned long>(APP_STATE.bootCount), plural(APP_STATE.bootCount));
      break;
    case 9:
      snprintf(loreBuf, sizeof(loreBuf), "It has written %lu record%s to the log this session.",
               static_cast<unsigned long>(encryptedLog.recordsWrittenThisBoot()),
               plural(encryptedLog.recordsWrittenThisBoot()));
      break;
    case 10: {
      char sizeBuf[24];
      formatBytesShort(encryptedLog.currentFileSizeBytes(), sizeBuf, sizeof(sizeBuf));
      snprintf(loreBuf, sizeof(loreBuf), "Today's encrypted log is %s so far — ciphertext even it can't read without the key.",
               sizeBuf);
      break;
    }
    case 11:
      if (recentSightings.count() == 0) {
        snprintf(loreBuf, sizeof(loreBuf), "It hasn't heard anything yet this session. Give it a moment.");
      } else {
        const auto& entry = recentSightings.at(0);
        const char* label = entry.label[0] ? entry.label : "something with no name";
        char agoBuf[16];
        formatAgo(entry.seenAtMs, agoBuf, sizeof(agoBuf));
        snprintf(loreBuf, sizeof(loreBuf), "The last thing it heard was %s, %s ago.", label, agoBuf);
      }
      break;
    case 12:
      if (recentSightings.count() == 0) {
        snprintf(loreBuf, sizeof(loreBuf), "Silence has a signal strength too. Right now, that's all there is.");
      } else {
        snprintf(loreBuf, sizeof(loreBuf), "That last signal arrived at %d dBm.", recentSightings.at(0).rssi);
      }
      break;
    case 13:
      snprintf(loreBuf, sizeof(loreBuf), "It has unlocked %u of %zu things it knows how to say. This is one of them.",
               state.unlockedLoreCount, kLoreEntryCount);
      break;
    default:
      loreBuf[0] = '\0';
      break;
  }
  return loreBuf;
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
