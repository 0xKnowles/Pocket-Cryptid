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
