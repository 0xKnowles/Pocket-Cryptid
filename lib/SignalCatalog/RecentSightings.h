#pragma once

#include <cstddef>
#include <cstdint>

#include "LogRecord.h"
#include "RfTypes.h"

// A small RAM-only ring of the most recently observed WiFi/BLE devices, kept purely so
// DeviceListActivity has something to show for "what is my device seeing right now". This is
// deliberately separate from both SignalCatalog (which intentionally never retains which specific
// MACs it has seen — see SignalCatalog.h) and EncryptedLog (which retains everything, but only
// ever at rest, encrypted, on the SD card). Nothing in this class ever touches storage — it is
// lost on reboot, by design, same as the screen it feeds.
class RecentSightings {
 public:
  struct Entry {
    unsigned long seenAtMs = 0;
    LogRecordType type = LogRecordType::WifiAp;
    MacAddress mac;
    int8_t rssi = 0;
    char label[25] = {};  // SSID/advertised name, always null-terminated (unlike LogRecordPlaintext::label)
  };

  static constexpr size_t kCapacity = 16;

  void recordWifi(const WifiObservation& obs, LogRecordType type);
  void recordBle(const BleObservation& obs);

  size_t count() const { return entryCount; }

  // indexFromNewest 0 = most recently seen. Caller must ensure indexFromNewest < count().
  const Entry& at(size_t indexFromNewest) const;

 private:
  void push(const Entry& entry);

  Entry entries[kCapacity];
  size_t head = 0;  // slot the *next* push() will write to
  size_t entryCount = 0;
};

extern RecentSightings recentSightings;  // singleton, defined in RecentSightings.cpp
