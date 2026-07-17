#pragma once

#include <cstddef>
#include <cstdint>

#include "RfTypes.h"

// Persistent, SD-backed history of every WiFi AP ever seen across the device's lifetime — first
// seen, last seen, and how many times — as opposed to RecentSightings (RAM-only, 16-entry ring,
// gone on reboot) or SignalCatalog (dedup counts only, no per-BSSID detail retained). Backed by a
// small human-readable text file (see kFilePath in the .cpp) so it survives power cycles and SD
// card moves; not routed through the AES-256-GCM encrypted log since none of this is more
// sensitive than what's already broadcast in the clear by every AP's own beacon, and being
// human-readable makes it directly useful without a decrypt step.
class ApHistory {
 public:
  struct Entry {
    MacAddress bssid;
    char ssid[33] = {};
    uint32_t firstSeenUnix = 0;
    uint32_t lastSeenUnix = 0;
    uint32_t sightings = 0;
    bool inUse = false;
  };

  static constexpr size_t kCapacity = 256;

  // Loads from SD, creating an empty file if none exists yet.
  bool begin();

  // Feed every WiFi observation through here — only Beacon/ProbeResponse update history (the
  // same gating ApScanCache uses), since those are the frames that actually confirm a BSSID and
  // carry its SSID.
  void observe(const WifiObservation& obs);

  // Flushes to SD if anything changed since the last save, and at most once per save interval —
  // call this every loop() iteration; it's a cheap no-op the rest of the time. A busy capture
  // session sees a beacon roughly every ~100ms per AP across many APs, and history only needs to
  // survive a reboot, not stay byte-for-byte live on SD, so batching writes this way keeps SD
  // wear/latency bounded regardless of capture density.
  void tick();

  size_t count() const { return entryCount; }
  const Entry& at(size_t index) const;

 private:
  void parseContents(const char* text);
  bool save();

  Entry entries[kCapacity] = {};
  size_t entryCount = 0;
  bool dirty = false;
  unsigned long lastSaveMs = 0;
};

extern ApHistory apHistory;  // singleton, defined in ApHistory.cpp
