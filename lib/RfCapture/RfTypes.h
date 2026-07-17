#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Shared observation types produced by WifiSniffer and BleScanner. Both capture paths are
// receive-only — nothing here associates, pairs, or advertises, and BleScanner never transmits
// at all. DeauthEngine (see DeauthEngine.h) is the one exception, transmitting 802.11
// deauthentication frames on the WiFi side via a transient TX-capable mode switch scoped to each
// burst — see its class comment for the full reasoning and its current real-hardware-confirmation
// status. A MAC address here is a raw 6-byte hardware identifier as heard over the air; callers
// decide whether/how to hash it before it touches persistent storage.

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
  Deauth,     // someone's deauthentication frame — not necessarily this device's own DeauthEngine
  Disassoc,   // same as above, disassociation subtype
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
  uint8_t manufacturerDataLen = 0;   // total length of the manufacturer-specific AD structure
  uint16_t manufacturerId = 0;       // 0xFFFF if none present
  // Bytes after the 2-byte company ID, capped — just enough for TrackerDetector to recognize
  // known tracker-network protocols (e.g. Apple Find My's type-0x12 continuity frames) without
  // needing to retain an entire advertisement's payload.
  static constexpr uint8_t kManufacturerPayloadCap = 24;
  uint8_t manufacturerPayload[kManufacturerPayloadCap] = {};
  uint8_t manufacturerPayloadLen = 0;
};

// Bound on raw frame bytes preserved for .pcap export. EAPOL key frames run well under this;
// beacons occasionally carry vendor IEs that push them close to it. Longer frames are truncated
// rather than growing this per-slot size, since the raw-capture queue is sized off it — the SSID
// element (what beacons are captured for) always sits near the front of the IE block, so a
// truncated beacon still yields a usable SSID.
constexpr size_t kRawFrameMaxLen = 400;

// A verbatim copy of an over-the-air frame, produced only when RubySettings::rawHandshakeCaptureEnabled
// is on, and only for EAPOL handshake frames plus the one beacon/probe-response per BSSID needed to
// recover its SSID (see WifiSniffer::promiscuousRxCallback). This is the one observation type that
// carries actual key material (ANonce/SNonce/MIC) — everything else Ruby captures is deliberately
// metadata-only. That's why it's opt-in and written out as plaintext .pcap (see PcapWriter) rather
// than through EncryptedLog: a cracking tool needs the exact bytes, which encryption would defeat.
struct RawFrameCapture {
  uint8_t bytes[kRawFrameMaxLen] = {};
  uint16_t len = 0;
  uint32_t unixTime = 0;
};
