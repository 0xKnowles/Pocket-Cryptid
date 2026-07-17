#pragma once

#include <cstdint>
#include <functional>

#include "RfTypes.h"

// Passive BLE advertisement capture, built on NimBLE-Arduino (see platformio.ini lib_deps).
//
// The scan is configured PASSIVE (setActiveScan(false)): the radio only listens to
// ADV_IND/ADV_NONCONN_IND/SCAN_RSP packets that nearby devices broadcast on their own; it never
// sends a SCAN_REQ, never initiates a connection, and never pairs. That is a deliberate
// stealth/battery choice, not just a default — active scanning would make this device visibly
// transmit every time it notices a new peripheral.
class BleScanner {
 public:
  using ObservationCallback = std::function<void(const BleObservation&)>;

  bool begin();
  void end();

  bool start();
  void stop();
  bool isRunning() const { return running; }

  // No-op placeholder kept for symmetry with WifiSniffer::tick() — NimBLE delivers advertisement
  // callbacks from its own host task, so nothing needs draining here today. Reserved in case a
  // future revision moves callback delivery onto a queue like the WiFi path.
  void tick() {}

  void setObservationCallback(ObservationCallback cb) { callback = std::move(cb); }

  uint32_t advertisementsSeen() const { return totalAdvertisements; }

  // Called by the internal NimBLE scan-callback shim; not part of the public capture API.
  void handleObservation(const BleObservation& obs);

 private:
  bool running = false;
  bool initialized = false;
  uint32_t totalAdvertisements = 0;
  ObservationCallback callback;
  void* scanCallbacks = nullptr;  // owns the NimBLEScanCallbacks subclass instance
};

extern BleScanner bleScanner;  // singleton, defined in BleScanner.cpp
