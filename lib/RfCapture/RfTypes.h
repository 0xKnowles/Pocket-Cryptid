#pragma once

#include <cstdint>
#include <cstring>

// Shared observation types produced by WifiSniffer and BleScanner. Both capture paths are
// strictly passive/receive-only — nothing in this library ever transmits, associates, pairs,
// or advertises. A MAC address here is a raw 6-byte hardware identifier as heard over the air;
// callers decide whether/how to hash it before it touches persistent storage.

struct MacAddress {
  uint8_t bytes[6] = {0, 0, 0, 0, 0, 0};

  bool operator==(const MacAddress& other) const { return memcmp(bytes, other.bytes, 6) == 0; }

  // FNV-1a over the 6 address bytes. Used as the SignalCatalog hash-set key; not a
  // cryptographic digest and not what gets written to the encrypted log (see EncryptedLog).
  uint32_t fnv1a() const {
    uint32_t hash = 2166136261u;
    for (uint8_t b : bytes) {
      hash ^= b;
      hash *= 16777619u;
    }
    return hash;
  }
};

enum class WifiFrameKind : uint8_t {
  Beacon,
  ProbeRequest,
  ProbeResponse,
  EapolHandshake,
  Other,
};

struct WifiObservation {
  WifiFrameKind kind = WifiFrameKind::Other;
  MacAddress transmitter;   // addr2 — the radio that sent the frame
  MacAddress bssid;         // addr3 — network/AP this frame belongs to
  int8_t rssi = 0;
  uint8_t channel = 0;
  char ssid[33] = {};       // beacons/probes only; empty for hidden/unavailable
  uint8_t ssidLen = 0;
  uint8_t eapolMessageNum = 0;  // 1-4 for EapolHandshake, 0 otherwise
};

struct BleObservation {
  MacAddress address;
  int8_t rssi = 0;
  bool addressIsRandom = false;
  bool connectable = false;
  char name[32] = {};   // empty if the device didn't advertise a name
  uint8_t nameLen = 0;
  uint8_t manufacturerDataLen = 0;   // length only is recorded, never the payload bytes
  uint16_t manufacturerId = 0;       // 0xFFFF if none present
};
