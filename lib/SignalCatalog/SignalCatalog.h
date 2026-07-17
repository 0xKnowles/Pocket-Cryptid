#pragma once

#include <PersistableStore.h>

#include <cstddef>
#include <cstdint>
#include <functional>

#include "RfTypes.h"

// What kind of "first time we've ever seen this" event just happened. RubyManager listens
// for these to update its expression and to unlock lore entries.
enum class RfEventType : uint8_t {
  NewWifiAP,
  NewWifiClient,
  NewBleDevice,
  HandshakeCaptured,
};

// Tracks how many *unique* WiFi access points, WiFi clients (from probe requests), BLE devices,
// and WPA handshakes this device has observed, without ever storing which specific ones — that
// detail lives only in EncryptedLog, at rest, encrypted. SignalCatalog's job is strictly
// deduplication + counting: "have I seen this MAC before" and "how many distinct ones so far".
//
// Dedup state is bounded, in-RAM only (see kCapacity below) — it exists to avoid double-counting
// and double-logging within a session, not as a permanent record. The lifetime counters below are
// what's persisted to survive reboots and drive Ruby's expression.
class SignalCatalog : public PersistableStore<SignalCatalog> {
  friend class PersistableStore<SignalCatalog>;

 public:
  using NewUniqueCallback = std::function<void(RfEventType type)>;

  struct Stats {
    uint32_t uniqueWifiAPs = 0;
    uint32_t uniqueWifiClients = 0;
    uint32_t uniqueBleDevices = 0;
    uint32_t handshakesCaptured = 0;
    uint32_t wifiFramesObserved = 0;
    uint32_t bleAdvertisementsObserved = 0;
  };

  // Feed a raw observation in. Returns true if this specific record was novel (SignalCatalog
  // hadn't seen this MAC/role before) and was therefore logged + counted.
  bool observeWifi(const WifiObservation& obs);
  bool observeBle(const BleObservation& obs);

  const Stats& getStats() const { return stats; }
  void setNewUniqueCallback(NewUniqueCallback cb) { onNewUnique = std::move(cb); }

  // Debounced persistence — call periodically (e.g. once per second) from the main loop. Only
  // actually writes to SD when the counters have changed and kSaveIntervalMs has elapsed, so a
  // busy RF environment doesn't turn into a write-every-loop-iteration problem.
  void tick();

  static const char* getFilePath() { return kStatePath; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  SignalCatalog() = default;

  static constexpr const char* kStatePath = "/.ruby/signal_catalog.json";
  static constexpr uint32_t kSaveIntervalMs = 15000;
  static constexpr size_t kApCapacity = 768;
  static constexpr size_t kClientCapacity = 768;
  static constexpr size_t kBleCapacity = 512;
  static constexpr size_t kHandshakeTrackerCapacity = 32;

  // Smallest power of two >= n — used to size each ring's open-addressing index table below.
  static constexpr size_t nextPowerOfTwo(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
  }

  // Fixed-capacity ring of FNV hashes: membership test + insert-with-oldest-eviction. Once full,
  // the oldest entry is overwritten, so a MAC that scrolled out of the ring can register as
  // "new" again on rediscovery — an accepted tradeoff for keeping this RAM-bounded on a ~380 KB
  // heap budget instead of growing unbounded over a multi-day capture session.
  //
  // `slots`/`occupied`/`nextSlot` are the ring itself (insertion order, oldest-slot eviction,
  // unchanged from before). `table` is a separate open-addressing index — hash -> slot index,
  // linear-probed with tombstones for removal — so contains()/insert() are an O(1) average hash
  // lookup instead of an O(Capacity) scan over every slot on *every single observed packet*. Sized
  // at 2x capacity (rounded up to a power of two) to keep the load factor under 50% even when the
  // ring is completely full, which keeps probe chains short.
  template <size_t Capacity>
  struct HashRing {
    static constexpr size_t kTableSize = nextPowerOfTwo(Capacity * 2);
    static constexpr uint16_t kEmpty = 0xFFFF;
    static constexpr uint16_t kTombstone = 0xFFFE;
    static_assert(Capacity < kTombstone, "slot index must fit below the sentinel values");

    uint32_t slots[Capacity] = {};
    bool occupied[Capacity] = {};
    size_t nextSlot = 0;
    size_t count = 0;
    uint16_t table[kTableSize];

    HashRing() {
      for (auto& entry : table) entry = kEmpty;
    }

    static size_t bucketFor(uint32_t hash) { return hash & (kTableSize - 1); }

    bool contains(uint32_t hash) const {
      size_t b = bucketFor(hash);
      for (size_t probes = 0; probes < kTableSize; probes++) {
        const uint16_t entry = table[b];
        if (entry == kEmpty) return false;
        if (entry != kTombstone && slots[entry] == hash) return true;
        b = (b + 1) & (kTableSize - 1);
      }
      return false;
    }

    // Removes slotIdx's entry from the index (found by re-deriving its bucket from its hash, the
    // standard way to locate a specific entry in a linear-probed table without storing back-links).
    void tableRemove(uint32_t hash, size_t slotIdx) {
      const uint16_t target = static_cast<uint16_t>(slotIdx);
      size_t b = bucketFor(hash);
      for (size_t probes = 0; probes < kTableSize; probes++) {
        if (table[b] == target) {
          table[b] = kTombstone;
          return;
        }
        if (table[b] == kEmpty) return;  // consistent state implies it isn't present
        b = (b + 1) & (kTableSize - 1);
      }
    }

    void tableInsert(uint32_t hash, size_t slotIdx) {
      size_t b = bucketFor(hash);
      for (size_t probes = 0; probes < kTableSize; probes++) {
        if (table[b] == kEmpty || table[b] == kTombstone) {
          table[b] = static_cast<uint16_t>(slotIdx);
          return;
        }
        b = (b + 1) & (kTableSize - 1);
      }
      // Unreachable at <=50% max load factor; if it somehow triggers, the hash is just dropped
      // from the index (a false-negative contains() next time, i.e. same as a ring eviction).
    }

    // Returns true if this was a new insertion.
    bool insert(uint32_t hash) {
      if (contains(hash)) return false;
      if (occupied[nextSlot]) {
        tableRemove(slots[nextSlot], nextSlot);  // evicting the oldest entry at this ring position
      } else {
        count++;
      }
      slots[nextSlot] = hash;
      occupied[nextSlot] = true;
      tableInsert(hash, nextSlot);
      nextSlot = (nextSlot + 1) % Capacity;
      return true;
    }
  };

  struct HandshakeTracker {
    uint32_t bssidHash = 0;
    uint8_t messageMask = 0;   // bit (n-1) set for each EAPOL message n seen
    bool counted = false;
    unsigned long lastSeenMs = 0;
    bool inUse = false;
  };

  HashRing<kApCapacity> apRing;
  HashRing<kClientCapacity> clientRing;
  HashRing<kBleCapacity> bleRing;
  HandshakeTracker handshakeTrackers[kHandshakeTrackerCapacity];

  Stats stats;
  NewUniqueCallback onNewUnique;
  bool dirty = false;
  unsigned long lastSaveMs = 0;

  HandshakeTracker& trackerFor(uint32_t bssidHash);
};

#define SIGNAL_CATALOG SignalCatalog::getInstance()
