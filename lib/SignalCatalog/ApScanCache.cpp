#include "ApScanCache.h"

#include <Arduino.h>

#include <cstring>

ApScanCache apScanCache;

void ApScanCache::observe(const WifiObservation& obs) {
  if (obs.kind != WifiFrameKind::Beacon && obs.kind != WifiFrameKind::ProbeResponse) return;

  int freeSlot = -1;
  int lruSlot = -1;
  unsigned long lruMs = 0xFFFFFFFFUL;
  for (size_t i = 0; i < kCapacity; i++) {
    Entry& e = entries[i];
    if (e.inUse && e.bssid == obs.bssid) {
      e.rssi = obs.rssi;
      e.channel = obs.channel;
      e.lastSeenMs = millis();
      if (obs.ssidLen > 0) {
        // A hidden network's later probe response can reveal the SSID a beacon didn't carry —
        // only overwrite once we actually have one, never blank out a previously-learned name.
        memcpy(e.ssid, obs.ssid, obs.ssidLen);
        e.ssid[obs.ssidLen] = '\0';
        e.ssidLen = obs.ssidLen;
      }
      return;
    }
    if (!e.inUse) {
      if (freeSlot < 0) freeSlot = static_cast<int>(i);
    } else if (e.lastSeenMs < lruMs) {
      lruMs = e.lastSeenMs;
      lruSlot = static_cast<int>(i);
    }
  }

  const size_t slot = freeSlot >= 0 ? static_cast<size_t>(freeSlot) : static_cast<size_t>(lruSlot);
  Entry& e = entries[slot];
  e = Entry{};
  e.bssid = obs.bssid;
  e.rssi = obs.rssi;
  e.channel = obs.channel;
  e.lastSeenMs = millis();
  if (obs.ssidLen > 0) {
    memcpy(e.ssid, obs.ssid, obs.ssidLen);
    e.ssid[obs.ssidLen] = '\0';
    e.ssidLen = obs.ssidLen;
  }
  e.inUse = true;
  if (freeSlot >= 0) entryCount++;
}

const ApScanCache::Entry& ApScanCache::at(size_t index) const { return entries[index]; }

const ApScanCache::Entry* ApScanCache::findByBssid(const MacAddress& bssid) const {
  for (size_t i = 0; i < kCapacity; i++) {
    if (entries[i].inUse && entries[i].bssid == bssid) return &entries[i];
  }
  return nullptr;
}
