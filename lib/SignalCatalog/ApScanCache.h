#pragma once

#include <cstddef>
#include <cstdint>

#include "RfTypes.h"

// Live, deduplicated-by-BSSID cache of currently-visible WiFi access points, fed from
// WifiSniffer's observation stream (main.cpp) for exactly one purpose: TargetPickerActivity's
// "pick a network like you're connecting to it" UI. Deliberately separate from RecentSightings
// (a 16-entry ring of *every* observation, WiFi and BLE mixed, that duplicates an AP's entry
// every time its beacon repeats, and would just as easily show the same loud neighbor 12 times
// as it would every real network in range) — a network picker needs one row per real access
// point, refreshed in place as it's seen again, not a churny recent-activity log.
class ApScanCache {
 public:
  struct Entry {
    MacAddress bssid;
    char ssid[33] = {};
    uint8_t ssidLen = 0;
    int8_t rssi = 0;
    uint8_t channel = 0;
    unsigned long lastSeenMs = 0;
    bool inUse = false;
  };

  static constexpr size_t kCapacity = 32;

  // Feed every WiFi observation through here — same stream WifiSniffer's ObservationCallback
  // already produces. Only Beacon/ProbeResponse kinds (the ones that carry an SSID) update the
  // cache; anything else is ignored. Hidden-SSID beacons (ssidLen == 0) are still tracked, shown
  // as "(hidden)" by the picker, since they're still valid targets.
  void observe(const WifiObservation& obs);

  size_t count() const { return entryCount; }
  // Raw storage order (not sorted) — cheap for TargetPickerActivity to re-sort by RSSI on each
  // render itself given how small kCapacity is, rather than paying to keep this sorted on every
  // single observation, which happens far more often than the picker screen is ever open.
  const Entry& at(size_t index) const;

 private:
  Entry entries[kCapacity] = {};
  size_t entryCount = 0;
};

extern ApScanCache apScanCache;  // singleton, defined in ApScanCache.cpp
