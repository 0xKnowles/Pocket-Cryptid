#pragma once

#include <cstddef>
#include <cstdint>

// On-disk record format for EncryptedLog. Bump kLogFormatVersion if this layout ever changes —
// the companion decrypt script (scripts/decrypt_log.py) keys its parsing off that byte, and old
// logs on an SD card must stay readable after a firmware update.
constexpr uint8_t kLogFormatVersion = 1;

enum class LogRecordType : uint8_t {
  WifiAp = 0,
  WifiClient = 1,
  WifiHandshake = 2,
  BleDevice = 3,
};

// Shared short label used by both the live RecentSightings view and the on-device log viewer, so
// the two screens describe record types the same way.
inline const char* logRecordTypeShortName(LogRecordType type) {
  switch (type) {
    case LogRecordType::WifiAp:
      return "AP";
    case LogRecordType::WifiClient:
      return "CLIENT";
    case LogRecordType::WifiHandshake:
      return "EAPOL";
    case LogRecordType::BleDevice:
      return "BLE";
  }
  return "?";
}

// Even shorter than logRecordTypeShortName() above — used by every dense multi-column grid
// (Dashboard's RECENT DEVICES card, DeviceListActivity, LogViewerActivity) where every character
// of a fixed-width column is worth reclaiming; logRecordTypeShortName()'s "CLIENT"/"EAPOL" are too
// wide to fit those columns' worst-case sizing.
inline const char* logRecordTypeCompactName(LogRecordType type) {
  switch (type) {
    case LogRecordType::WifiAp:
      return "AP";
    case LogRecordType::WifiClient:
      return "STA";
    case LogRecordType::WifiHandshake:
      return "EAP";
    case LogRecordType::BleDevice:
      return "BLE";
  }
  return "?";
}

// Fixed-size plaintext payload, AES-256-GCM encrypted as a single unit before it ever reaches
// the SD card. 39 bytes today; keep it POD and packed so the size is portable across compilers.
#pragma pack(push, 1)
struct LogRecordPlaintext {
  uint32_t unixTime = 0;
  LogRecordType type = LogRecordType::WifiAp;
  uint8_t mac[6] = {};
  int8_t rssi = 0;
  uint8_t extra = 0;       // WiFi: channel number. BLE: 1 = random address, 0 = public.
  uint8_t label[24] = {};  // SSID (WiFi) or advertised name (BLE), truncated, not null-padded past labelLen
  uint8_t labelLen = 0;
  uint8_t eapolMsgNum = 0;  // WifiHandshake only: 1-4
};
#pragma pack(pop)

static_assert(sizeof(LogRecordPlaintext) == 39, "LogRecordPlaintext layout changed size unexpectedly");

// On-disk envelope around one encrypted LogRecordPlaintext:
//   [1B version][12B GCM nonce][2B ciphertext length, little-endian][ciphertext][16B GCM tag]
constexpr size_t kLogNonceLen = 12;
constexpr size_t kLogTagLen = 16;
constexpr size_t kLogEnvelopeOverhead = 1 + kLogNonceLen + 2 + kLogTagLen;

// Every record is written back-to-back at this exact size (LogRecordPlaintext never varies in
// length, so ciphertext length never varies either) — that constant stride is what lets a reader
// seek directly to record N as `N * kLogRecordEnvelopeSize` instead of scanning the file from the
// start. See EncryptedLog::decryptRecordRange().
constexpr size_t kLogRecordEnvelopeSize = kLogEnvelopeOverhead + sizeof(LogRecordPlaintext);
