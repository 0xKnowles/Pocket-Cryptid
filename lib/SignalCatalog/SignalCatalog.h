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
  // mac identifies the specific device/network behind the event — obs.bssid for NewWifiAP/
  // HandshakeCaptured, obs.transmitter for NewWifiClient, obs.address for NewBleDevice. Lets
  // listeners (e.g. RubyManager, for the Dashboard's "HANDSHAKE CAPTURED" banner) show which
  // device the event was actually about, not just that the event type happened.
  using NewUniqueCallback = std::function<void(RfEventType type, const MacAddress& mac)>;

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

  // Zeroes the lifetime counters (Dashboard's SIGNALS card) and clears the dedup rings/handshake
  // trackers that back them — a counter-only reset without also clearing the rings would leave
  // every already-seen MAC unable to register as "new" again, permanently undercounting from that
  // point on. Saves immediately rather than waiting for tick()'s debounce, since this is a
  // deliberate one-off action, not a high-frequency mutation.
  void resetStats();

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

  // Fixed-capacity ring of FNV hashes: membership test + insert-with-oldest-eviction. Once full,
  // the oldest entry is overwritten, so a MAC that scrolled out of the ring can register as
  // "new" again on rediscovery — an accepted tradeoff for keeping this RAM-bounded on a ~380 KB
  // heap budget instead of growing unbounded over a multi-day capture session.
  //
  // contains()/insert() are an O(Capacity) linear scan rather than a hash-indexed lookup —
  // deliberately simple. An open-addressing index was tried here to cut CPU time per observed
  // packet, but its ~10 KB of extra static RAM (across all three rings) turned out to matter far
  // more than the CPU savings on a device already running with only a few KB of free heap;
  // reverted after that surfaced as "esp-aes: Failed to allocate memory" aborts on real hardware.
  template <size_t Capacity>
  struct HashRing {
    uint32_t slots[Capacity] = {};
    bool occupied[Capacity] = {};
    size_t nextSlot = 0;
    size_t count = 0;

    bool contains(uint32_t hash) const {
      for (size_t i = 0; i < Capacity; i++) {
        if (occupied[i] && slots[i] == hash) return true;
      }
      return false;
    }

    // Returns true if this was a new insertion.
    bool insert(uint32_t hash) {
      if (contains(hash)) return false;
      if (!occupied[nextSlot]) count++;
      slots[nextSlot] = hash;
      occupied[nextSlot] = true;
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
