#include "SignalCatalog.h"

#include <Arduino.h>
#include <Logging.h>

#include <climits>
#include <cstddef>

SignalCatalog::HandshakeTracker& SignalCatalog::trackerFor(uint32_t bssidHash) {
  int oldestIdx = 0;
  unsigned long oldestMs = ULONG_MAX;
  for (size_t i = 0; i < kHandshakeTrackerCapacity; i++) {
    if (handshakeTrackers[i].inUse && handshakeTrackers[i].bssidHash == bssidHash) {
      return handshakeTrackers[i];
    }
    if (!handshakeTrackers[i].inUse) {
      oldestIdx = static_cast<int>(i);
      oldestMs = 0;
      break;
    }
    if (handshakeTrackers[i].lastSeenMs < oldestMs) {
      oldestMs = handshakeTrackers[i].lastSeenMs;
      oldestIdx = static_cast<int>(i);
    }
  }
  handshakeTrackers[oldestIdx] = HandshakeTracker{};
  handshakeTrackers[oldestIdx].bssidHash = bssidHash;
  handshakeTrackers[oldestIdx].inUse = true;
  return handshakeTrackers[oldestIdx];
}

bool SignalCatalog::observeWifi(const WifiObservation& obs) {
  stats.wifiFramesObserved++;
  dirty = true;

  switch (obs.kind) {
    case WifiFrameKind::Beacon:
    case WifiFrameKind::ProbeResponse: {
      const uint32_t hash = obs.bssid.fnv1a();
      if (apRing.insert(hash)) {
        stats.uniqueWifiAPs++;
        if (onNewUnique) onNewUnique(RfEventType::NewWifiAP, obs.bssid);
        return true;
      }
      return false;
    }
    case WifiFrameKind::ProbeRequest: {
      const uint32_t hash = obs.transmitter.fnv1a();
      if (clientRing.insert(hash)) {
        stats.uniqueWifiClients++;
        if (onNewUnique) onNewUnique(RfEventType::NewWifiClient, obs.transmitter);
        return true;
      }
      return false;
    }
    case WifiFrameKind::EapolHandshake: {
      if (obs.eapolMessageNum < 1 || obs.eapolMessageNum > 4) return false;
      const uint32_t bssidHash = obs.bssid.fnv1a();
      HandshakeTracker& tracker = trackerFor(bssidHash);
      tracker.messageMask |= 1u << (obs.eapolMessageNum - 1);
      tracker.lastSeenMs = millis();

      // "Captured" heuristic: we've heard from both halves of the 4-way exchange (either
      // M1-or-M2 plus M3-or-M4). Best-effort classification of frames already broadcast in the
      // clear — this never touches key material and can't itself crack anything.
      constexpr uint8_t kFirstHalf = 0b0011;
      constexpr uint8_t kSecondHalf = 0b1100;
      if (!tracker.counted && (tracker.messageMask & kFirstHalf) && (tracker.messageMask & kSecondHalf)) {
        tracker.counted = true;
        stats.handshakesCaptured++;
        if (onNewUnique) onNewUnique(RfEventType::HandshakeCaptured, obs.bssid);
        return true;
      }
      return false;
    }
    case WifiFrameKind::Other:
    default:
      return false;
  }
}

bool SignalCatalog::observeBle(const BleObservation& obs) {
  stats.bleAdvertisementsObserved++;
  dirty = true;

  const uint32_t hash = obs.address.fnv1a();
  if (bleRing.insert(hash)) {
    stats.uniqueBleDevices++;
    if (onNewUnique) onNewUnique(RfEventType::NewBleDevice, obs.address);
    return true;
  }
  return false;
}

void SignalCatalog::resetStats() {
  stats = Stats{};
  apRing = HashRing<kApCapacity>{};
  clientRing = HashRing<kClientCapacity>{};
  bleRing = HashRing<kBleCapacity>{};
  for (auto& tracker : handshakeTrackers) tracker = HandshakeTracker{};

  if (saveToFile()) {
    dirty = false;
    lastSaveMs = millis();
    LOG_INF("SIGCAT", "Signal stats reset");
  } else {
    LOG_ERR("SIGCAT", "Failed to persist signal stats reset");
  }
}

void SignalCatalog::tick() {
  if (!dirty) return;
  const unsigned long now = millis();
  if (now - lastSaveMs < kSaveIntervalMs) return;

  if (saveToFile()) {
    dirty = false;
    lastSaveMs = now;
  } else {
    LOG_ERR("SIGCAT", "Failed to persist signal catalog stats");
  }
}

void SignalCatalog::toJson(JsonDocument& doc) const {
  doc["uniqueWifiAPs"] = stats.uniqueWifiAPs;
  doc["uniqueWifiClients"] = stats.uniqueWifiClients;
  doc["uniqueBleDevices"] = stats.uniqueBleDevices;
  doc["handshakesCaptured"] = stats.handshakesCaptured;
  doc["wifiFramesObserved"] = stats.wifiFramesObserved;
  doc["bleAdvertisementsObserved"] = stats.bleAdvertisementsObserved;
}

bool SignalCatalog::fromJson(JsonVariantConst doc) {
  stats.uniqueWifiAPs = doc["uniqueWifiAPs"] | 0;
  stats.uniqueWifiClients = doc["uniqueWifiClients"] | 0;
  stats.uniqueBleDevices = doc["uniqueBleDevices"] | 0;
  stats.handshakesCaptured = doc["handshakesCaptured"] | 0;
  stats.wifiFramesObserved = doc["wifiFramesObserved"] | 0;
  stats.bleAdvertisementsObserved = doc["bleAdvertisementsObserved"] | 0;
  return true;
}
