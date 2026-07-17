#pragma once

#include <cstdint>
#include <string>

#include "LogRecord.h"
#include "RfTypes.h"

// Append-only, AES-256-GCM encrypted catalog of every unique observation SignalCatalog reports.
// Design goals, in order:
//
//  1. Nothing readable in plaintext ever touches the SD card. Every record is encrypted before
//     the write() call.
//  2. No keyboard/passphrase UI. The AES key is derived from a random seed generated on first
//     boot (esp_fill_random, i.e. the hardware TRNG) plus this chip's eFuse-burned MAC address,
//     via SHA-256. The seed lives in internal NVS flash, not on the removable SD card — pulling
//     the SD card out gets you ciphertext with no key alongside it.
//  3. The key is still recoverable by the device's legitimate owner: MaintenanceActivity can
//     display it (see revealDecryptionKeyHex()) for one-time use with scripts/decrypt_log.py.
//  4. GCM nonce reuse under a fixed key is catastrophic, and this key is not rotated per boot —
//     so the nonce counter itself is persisted in NVS and reserved in blocks (see .cpp), making
//     nonce collision structurally impossible across the key's lifetime, at the cost of a small
//     number of skipped counter values after an unclean shutdown.
class EncryptedLog {
 public:
  bool begin();  // loads or generates the key, opens today's rotation for append

  // Encrypts and appends one record. Every observation is logged here, not just first-seen ones
  // — the log is meant to be a full local capture record. SignalCatalog's dedup only gates the
  // creature's XP/lore, not what reaches disk; wire both up to WifiSniffer/BleScanner's
  // callbacks independently (see main.cpp).
  bool appendWifi(const WifiObservation& obs, LogRecordType type);
  bool appendBle(const BleObservation& obs);

  // Call periodically from the main loop: rotates to a new day's file when the date changes.
  void tick();

  uint64_t currentFileSizeBytes() const;
  uint32_t recordsWrittenThisBoot() const { return recordsWritten; }

  // Returns the 64-hex-char AES-256 key as ASCII into `out` (needs >= 65 bytes). Intended for a
  // one-time on-screen reveal in MaintenanceActivity so the owner can copy it down.
  bool revealDecryptionKeyHex(char* out, size_t outSize) const;

  // Rotates in a brand new random key seed, permanently orphaning every previously-written log
  // file (they remain on disk but become undecryptable) and deletes the log directory contents.
  // Used by the "wipe capture history" settings action.
  bool wipeAndResetKey();

 private:
  bool loadOrCreateKeySeed();
  void deriveKey();
  bool reserveNonceBlock();  // pulls the next block of nonce-counter values from NVS
  bool openTodaysFile();
  bool writeEnvelope(const LogRecordPlaintext& plaintext);
  void plaintextFromWifi(const WifiObservation& obs, LogRecordType type, LogRecordPlaintext& out) const;
  void plaintextFromBle(const BleObservation& obs, LogRecordPlaintext& out) const;

  uint8_t aesKey[32] = {};
  uint64_t nonceCounter = 0;
  uint64_t nonceBlockEnd = 0;
  uint32_t bootNonceSalt = 0;  // random per-boot value mixed into the nonce, see .cpp

  std::string currentFilePath;
  int32_t currentFileDay = -1;  // day-of-epoch the open file was rotated for; -1 = none open
  uint32_t recordsWritten = 0;
  bool ready = false;
};

extern EncryptedLog encryptedLog;  // singleton, defined in EncryptedLog.cpp
