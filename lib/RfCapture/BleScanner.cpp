#include "BleScanner.h"

#include <Logging.h>
#include <NimBLEDevice.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

BleScanner bleScanner;

namespace {

// NimBLE-Arduino 2.x callback shim: forwards each advertisement into BleScanner as a plain
// RfCapture observation, decoupling the rest of the firmware from the NimBLE API surface.
class ObservationForwarder : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    BleObservation obs;

    const NimBLEAddress addr = device->getAddress();
    // Parse the "aa:bb:cc:dd:ee:ff" form rather than reaching into NimBLEAddress's internal
    // byte order/representation, which has shifted across NimBLE-Arduino versions — toString()
    // is the one part of this API that's stayed stable.
    {
      unsigned int b[6] = {};
      sscanf(addr.toString().c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
      for (int i = 0; i < 6; i++) obs.address.bytes[i] = static_cast<uint8_t>(b[i]);
    }
    obs.addressIsRandom = addr.getType() != BLE_ADDR_PUBLIC;
    obs.rssi = static_cast<int8_t>(device->getRSSI());
    obs.connectable = device->isConnectable();

    if (device->haveName()) {
      const std::string name = device->getName();
      const size_t copyLen = name.size() > sizeof(obs.name) - 1 ? sizeof(obs.name) - 1 : name.size();
      memcpy(obs.name, name.data(), copyLen);
      obs.name[copyLen] = '\0';
      obs.nameLen = static_cast<uint8_t>(copyLen);
    }

    if (device->haveManufacturerData()) {
      const std::string mfg = device->getManufacturerData();
      obs.manufacturerDataLen = static_cast<uint8_t>(mfg.size());
      if (mfg.size() >= 2) {
        obs.manufacturerId = static_cast<uint8_t>(mfg[0]) | (static_cast<uint8_t>(mfg[1]) << 8);
      }
    }

    bleScanner.handleObservation(obs);
  }
};

}  // namespace

bool BleScanner::begin() {
  if (initialized) return true;

  // Empty device name: this firmware never advertises, but an explicit empty name keeps the
  // NimBLE host from picking a default that would show up if anything ever queried it (e.g. a
  // GATT read triggered by a future maintenance feature).
  NimBLEDevice::init("");

  auto* forwarder = new ObservationForwarder();
  scanCallbacks = forwarder;

  NimBLEScan* scan = NimBLEDevice::getScan();
  // wantDuplicates=true: keep delivering onResult() for repeat advertisements from an
  // already-seen device instead of NimBLE's internal cache suppressing them. Uniqueness
  // dedup is SignalCatalog's job (a separate, bounded hash set); the log wants every sighting.
  scan->setScanCallbacks(forwarder, /*wantDuplicates=*/true);
  scan->setActiveScan(false);  // passive: never sends SCAN_REQ
  scan->setInterval(160);      // 100ms (units of 0.625ms)
  scan->setWindow(80);         // 50ms — 50% duty cycle keeps average draw down
  scan->setMaxResults(0);      // stream via callback only, don't buffer a results list in RAM

  initialized = true;
  LOG_INF("BLESCAN", "BLE passive scan initialized");
  return true;
}

void BleScanner::end() {
  stop();
  if (initialized) {
    NimBLEDevice::deinit(true);
    initialized = false;
  }
  delete static_cast<ObservationForwarder*>(scanCallbacks);
  scanCallbacks = nullptr;
}

bool BleScanner::start() {
  if (!initialized && !begin()) return false;
  if (running) return true;

  NimBLEScan* scan = NimBLEDevice::getScan();
  // Duration 0 = scan indefinitely; results stream to onResult() until stop() is called.
  const bool ok = scan->start(0, false);
  running = ok;
  if (ok) {
    LOG_INF("BLESCAN", "BLE passive scan started");
  } else {
    LOG_ERR("BLESCAN", "Failed to start BLE scan");
  }
  return ok;
}

void BleScanner::stop() {
  if (!running) return;
  NimBLEDevice::getScan()->stop();
  running = false;
  LOG_INF("BLESCAN", "BLE passive scan stopped");
}

void BleScanner::handleObservation(const BleObservation& obs) {
  totalAdvertisements++;
  if (callback) callback(obs);
}
