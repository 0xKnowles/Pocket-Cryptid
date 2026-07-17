#include "TrackerDetector.h"

#include <Arduino.h>

TrackerDetector trackerDetector;

namespace {
// Bluetooth SIG-assigned company identifiers (little-endian in the AD structure, already
// decoded into BleObservation::manufacturerId by BleScanner).
constexpr uint16_t kAppleCompanyId = 0x004C;
constexpr uint16_t kSamsungCompanyId = 0x0075;
constexpr uint16_t kTileCompanyId = 0x02E5;

// Apple Continuity protocol type byte for Find My / offline finding — the first byte of the
// manufacturer-specific payload after the company ID. Well-documented by the OpenHaystack/Find-My
// reverse-engineering community; not Apple's own published spec, since Apple doesn't publish one.
// This one match is fairly specific to the Find My protocol itself, though it still can't tell an
// AirTag apart from any other Find-My-network accessory (AirPods, Apple Watch, etc. in
// separated/offline mode all advertise this same way).
constexpr uint8_t kAppleFindMyContinuityType = 0x12;

// Samsung and Tile's company IDs, unlike Apple's continuity type above, aren't specific to their
// tracker products — Samsung ships far more BLE hardware than just SmartTags under 0x0075, for
// instance — so a match on either just means "a BLE advertisement from that company", a weaker
// signal than the Apple case. Flagging on company ID alone is still useful as a coarse heuristic
// (SmartTag/Tile are two-way more common everyday reasons a stranger's device broadcasts under
// these IDs near a passerby than most other products from the same vendors), but treat these two
// as "possible", not confirmed.
bool classify(const BleObservation& obs) {
  if (obs.manufacturerId == kAppleCompanyId) {
    return obs.manufacturerPayloadLen >= 1 && obs.manufacturerPayload[0] == kAppleFindMyContinuityType;
  }
  return obs.manufacturerId == kSamsungCompanyId || obs.manufacturerId == kTileCompanyId;
}
}  // namespace

void TrackerDetector::observe(const BleObservation& obs) {
  if (!classify(obs)) return;

  const unsigned long now = millis();

  for (size_t i = 0; i < entryCount; i++) {
    if (entries[i].address == obs.address) {
      entries[i].lastSeenMs = now;
      return;
    }
  }

  // New tracker address. Fill order: lowest free index first while under capacity; once full,
  // evict the least-recently-seen entry in place — same invariant ApScanCache relies on.
  size_t slot;
  if (entryCount < kCapacity) {
    slot = entryCount++;
  } else {
    slot = 0;
    unsigned long oldest = entries[0].lastSeenMs;
    for (size_t i = 1; i < kCapacity; i++) {
      if (entries[i].lastSeenMs < oldest) {
        oldest = entries[i].lastSeenMs;
        slot = i;
      }
    }
  }

  entries[slot] = Entry{obs.address, now};
  pendingAlert = true;
}

bool TrackerDetector::consumeNewTrackerAlert() {
  const bool had = pendingAlert;
  pendingAlert = false;
  return had;
}
