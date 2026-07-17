#include "EncryptedLog.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>

EncryptedLog encryptedLog;

namespace {
constexpr char kLogDir[] = "/.ruby/log";
constexpr char kPrefsNamespace[] = "ruby";
constexpr char kPrefsKeySeed[] = "keyseed";
constexpr char kPrefsNonceBlock[] = "noncenext";
constexpr size_t kSeedLen = 32;
constexpr uint64_t kNonceBlockSize = 4096;  // values reserved per NVS write; bounds flash wear
constexpr char kKeyDomainTag[] = "ruby-log-v1";

void hexEncode(const uint8_t* data, size_t len, char* out) {
  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = kHex[data[i] >> 4];
    out[i * 2 + 1] = kHex[data[i] & 0x0F];
  }
  out[len * 2] = '\0';
}
}  // namespace

bool EncryptedLog::loadOrCreateKeySeed() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    LOG_ERR("ENCLOG", "Failed to open NVS namespace for log key");
    return false;
  }

  uint8_t seed[kSeedLen];
  bool haveSeed = prefs.isKey(kPrefsKeySeed) && prefs.getBytesLength(kPrefsKeySeed) == kSeedLen;
  if (haveSeed) {
    prefs.getBytes(kPrefsKeySeed, seed, kSeedLen);
  } else {
    esp_fill_random(seed, kSeedLen);  // hardware TRNG
    prefs.putBytes(kPrefsKeySeed, seed, kSeedLen);
    LOG_INF("ENCLOG", "Generated new log encryption key seed");
  }

  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, /*is224=*/0);
  mbedtls_sha256_update(&sha, seed, kSeedLen);
  mbedtls_sha256_update(&sha, mac, sizeof(mac));
  mbedtls_sha256_update(&sha, reinterpret_cast<const uint8_t*>(kKeyDomainTag), strlen(kKeyDomainTag));
  mbedtls_sha256_finish(&sha, aesKey);
  mbedtls_sha256_free(&sha);

  prefs.end();
  return true;
}

bool EncryptedLog::reserveNonceBlock() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return false;

  uint64_t next = prefs.getULong64(kPrefsNonceBlock, 0);
  nonceCounter = next;
  nonceBlockEnd = next + kNonceBlockSize;
  prefs.putULong64(kPrefsNonceBlock, nonceBlockEnd);
  prefs.end();
  return true;
}

bool EncryptedLog::begin() {
  if (ready) return true;

  if (!loadOrCreateKeySeed()) return false;
  if (!reserveNonceBlock()) return false;
  bootNonceSalt = esp_random();

  if (!Storage.ensureDirectoryExists(kLogDir)) {
    LOG_ERR("ENCLOG", "Failed to create log directory %s", kLogDir);
    return false;
  }

  ready = openTodaysFile();
  return ready;
}

bool EncryptedLog::openTodaysFile() {
  const time_t now = time(nullptr);
  struct tm tmNow;
  gmtime_r(&now, &tmNow);
  const int32_t dayOfEpoch = static_cast<int32_t>(now / 86400);

  if (dayOfEpoch == currentFileDay && !currentFilePath.empty()) {
    return true;  // already on the right day's file
  }

  char path[64];
  snprintf(path, sizeof(path), "%s/%04d%02d%02d.pclog", kLogDir, tmNow.tm_year + 1900, tmNow.tm_mon + 1,
           tmNow.tm_mday);
  currentFilePath = path;
  currentFileDay = dayOfEpoch;
  return true;
}

void EncryptedLog::tick() {
  if (!ready) return;
  openTodaysFile();  // cheap check; only does work when the day actually rolled over
}

bool EncryptedLog::writeEnvelope(const LogRecordPlaintext& plaintext) {
  if (!ready) return false;
  if (nonceCounter >= nonceBlockEnd && !reserveNonceBlock()) return false;

  uint8_t nonce[kLogNonceLen];
  memcpy(nonce, &bootNonceSalt, 4);
  memcpy(nonce + 4, &nonceCounter, 8);
  nonceCounter++;

  uint8_t ciphertext[sizeof(LogRecordPlaintext)];
  uint8_t tag[kLogTagLen];

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aesKey, 256) != 0) {
    mbedtls_gcm_free(&gcm);
    // Temporary diagnostic while tracking down an "esp-aes: Failed to allocate memory" abort —
    // the hardware AES engine allocates from a small DMA-capable pool, distinct from (and much
    // smaller than) general heap, so seeing both numbers at the exact failure point matters.
    // Remove once diagnosed.
    LOG_ERR("ENCLOG", "GCM setkey failed; heap free=%u dmaFree=%u dmaLargest=%u",
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    return false;
  }
  const int rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, sizeof(plaintext), nonce, kLogNonceLen, nullptr,
                                           0, reinterpret_cast<const uint8_t*>(&plaintext), ciphertext, kLogTagLen,
                                           tag);
  mbedtls_gcm_free(&gcm);
  if (rc != 0) {
    LOG_ERR("ENCLOG", "GCM encrypt failed: %d; heap free=%u dmaFree=%u dmaLargest=%u", rc,
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    return false;
  }

  // Deliberately open/write/close per record rather than holding the file open across writes:
  // an earlier attempt at the latter (to cut per-record filesystem overhead) caused
  // LogViewerActivity's separate read handle to see stale/incomplete data for today's file while
  // this handle held it open — "Could not decrypt this page." Correctness of reading back what
  // was captured matters more than shaving the open() cost, so this reverts to the known-good
  // approach until a proper fix (e.g. routing LogViewerActivity's reads through this same open
  // handle) is worth the complexity.
  HalFile file = Storage.open(currentFilePath.c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("ENCLOG", "Failed to open log file for append: %s", currentFilePath.c_str());
    return false;
  }
  const uint16_t ctLen = sizeof(ciphertext);
  file.write(&kLogFormatVersion, 1);
  file.write(nonce, kLogNonceLen);
  file.write(&ctLen, 2);
  file.write(ciphertext, sizeof(ciphertext));
  file.write(tag, kLogTagLen);
  file.close();

  recordsWritten++;
  return true;
}

void EncryptedLog::plaintextFromWifi(const WifiObservation& obs, LogRecordType type, LogRecordPlaintext& out) const {
  out = LogRecordPlaintext{};
  out.unixTime = static_cast<uint32_t>(time(nullptr));
  out.type = type;
  const MacAddress& mac = (type == LogRecordType::WifiClient) ? obs.transmitter : obs.bssid;
  memcpy(out.mac, mac.bytes, 6);
  out.rssi = obs.rssi;
  out.extra = obs.channel;
  out.labelLen = obs.ssidLen > sizeof(out.label) ? sizeof(out.label) : obs.ssidLen;
  memcpy(out.label, obs.ssid, out.labelLen);
  out.eapolMsgNum = obs.eapolMessageNum;
}

void EncryptedLog::plaintextFromBle(const BleObservation& obs, LogRecordPlaintext& out) const {
  out = LogRecordPlaintext{};
  out.unixTime = static_cast<uint32_t>(time(nullptr));
  out.type = LogRecordType::BleDevice;
  memcpy(out.mac, obs.address.bytes, 6);
  out.rssi = obs.rssi;
  out.extra = obs.addressIsRandom ? 1 : 0;
  out.labelLen = obs.nameLen > sizeof(out.label) ? sizeof(out.label) : obs.nameLen;
  memcpy(out.label, obs.name, out.labelLen);
}

bool EncryptedLog::appendWifi(const WifiObservation& obs, LogRecordType type) {
  if (!ready) return false;
  LogRecordPlaintext plaintext;
  plaintextFromWifi(obs, type, plaintext);
  return writeEnvelope(plaintext);
}

bool EncryptedLog::appendBle(const BleObservation& obs) {
  if (!ready) return false;
  LogRecordPlaintext plaintext;
  plaintextFromBle(obs, plaintext);
  return writeEnvelope(plaintext);
}

uint64_t EncryptedLog::currentFileSizeBytes() const {
  if (currentFilePath.empty() || !Storage.exists(currentFilePath.c_str())) return 0;
  HalFile file = Storage.open(currentFilePath.c_str());
  if (!file) return 0;
  const uint64_t size = file.fileSize64();
  file.close();
  return size;
}

bool EncryptedLog::revealDecryptionKeyHex(char* out, size_t outSize) const {
  if (outSize < sizeof(aesKey) * 2 + 1) return false;
  hexEncode(aesKey, sizeof(aesKey), out);
  return true;
}

const char* EncryptedLog::logDirectory() { return kLogDir; }

uint32_t EncryptedLog::recordCountForFileSize(uint64_t fileSizeBytes) {
  return static_cast<uint32_t>(fileSizeBytes / kLogRecordEnvelopeSize);
}

size_t EncryptedLog::decryptRecordRange(const char* path, uint32_t startIndex, LogRecordPlaintext* out,
                                        size_t maxCount) const {
  HalFile file = Storage.open(path);
  if (!file) return 0;
  if (!file.seek64(static_cast<uint64_t>(startIndex) * kLogRecordEnvelopeSize)) {
    file.close();
    return 0;
  }

  size_t decrypted = 0;
  for (; decrypted < maxCount; decrypted++) {
    uint8_t version = 0;
    uint8_t nonce[kLogNonceLen];
    uint16_t ctLen = 0;
    if (file.read(&version, 1) != 1 || file.read(nonce, kLogNonceLen) != static_cast<int>(kLogNonceLen) ||
        file.read(&ctLen, 2) != 2) {
      break;  // EOF or short read — no more complete records
    }
    if (version != kLogFormatVersion || ctLen != sizeof(LogRecordPlaintext)) break;

    uint8_t ciphertext[sizeof(LogRecordPlaintext)];
    uint8_t tag[kLogTagLen];
    if (file.read(ciphertext, ctLen) != ctLen || file.read(tag, kLogTagLen) != static_cast<int>(kLogTagLen)) break;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aesKey, 256) == 0;
    if (ok) {
      ok = mbedtls_gcm_auth_decrypt(&gcm, ctLen, nonce, kLogNonceLen, nullptr, 0, tag, kLogTagLen, ciphertext,
                                    reinterpret_cast<uint8_t*>(&out[decrypted])) == 0;
    }
    mbedtls_gcm_free(&gcm);
    if (!ok) break;
  }

  file.close();
  return decrypted;
}

bool EncryptedLog::wipeAndResetKey() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.clear();
    prefs.end();
  }

  HalFile dir = Storage.open(kLogDir);
  if (dir) {
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      char name[64] = {};
      entry.getName(name, sizeof(name));
      entry.close();
      char full[96];
      snprintf(full, sizeof(full), "%s/%s", kLogDir, name);
      Storage.remove(full);
    }
    dir.close();
  }

  ready = false;
  currentFilePath.clear();
  currentFileDay = -1;
  recordsWritten = 0;
  return begin();
}
