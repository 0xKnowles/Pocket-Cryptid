#pragma once

#include <cstddef>
#include <cstdint>

#include "RfTypes.h"

// Flags BLE advertisements matching known Bluetooth tracker-network protocols (Apple Find My —
// the protocol AirTags and Find-My-enabled accessories both use, Samsung SmartTag, Tile) by
// manufacturer-specific data — see the .cpp for exactly which company IDs/payload patterns count,
// and why some are labeled with more confidence than others in the class comment there. Purely
// informational and purely passive — BleScanner never transmits regardless of what this
// classifies. Tracks only enough to answer two questions: how many distinct tracker-looking
// devices are currently around (count()), and has a brand new one just shown up
// (consumeNewTrackerAlert()) — not a full device browser, so no per-entry detail is retained.
class TrackerDetector {
 public:
  // Feed every BLE observation through here — same stream BleScanner's ObservationCallback
  // already produces. Observations that don't match a known tracker protocol are ignored
  // entirely (not tracked, not counted).
  void observe(const BleObservation& obs);

  size_t count() const { return entryCount; }

  // True once per newly-classified tracker address (i.e. the first sighting of that MAC this
  // session) until consumed — mirrors RubyManager::consumeJustCapturedHandshake()'s pattern, so
  // DashboardActivity can show a one-shot alert rather than polling for "is anything new".
  bool consumeNewTrackerAlert();

 private:
  static constexpr size_t kCapacity = 16;
  struct Entry {
    MacAddress address;
    unsigned long lastSeenMs = 0;
  };

  Entry entries[kCapacity] = {};
  size_t entryCount = 0;
  bool pendingAlert = false;
};

extern TrackerDetector trackerDetector;  // singleton, defined in TrackerDetector.cpp
