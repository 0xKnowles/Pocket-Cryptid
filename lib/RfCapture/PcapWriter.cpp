#include "PcapWriter.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <ctime>

PcapWriter pcapWriter;

namespace {
constexpr char kPcapDir[] = "/.ruby/pcap";
constexpr uint32_t kPcapMagic = 0xa1b2c3d4;
constexpr uint16_t kPcapVersionMajor = 2;
constexpr uint16_t kPcapVersionMinor = 4;
constexpr uint32_t kPcapSnaplen = 65535;
constexpr uint32_t kDltIeee80211 = 105;  // raw 802.11, no radiotap — see class comment

struct PcapGlobalHeader {
  uint32_t magic;
  uint16_t versionMajor;
  uint16_t versionMinor;
  int32_t thiszone;
  uint32_t sigfigs;
  uint32_t snaplen;
  uint32_t network;
} __attribute__((packed));

struct PcapRecordHeader {
  uint32_t tsSec;
  uint32_t tsUsec;
  uint32_t inclLen;
  uint32_t origLen;
} __attribute__((packed));
}  // namespace

bool PcapWriter::begin() {
  if (!Storage.ensureDirectoryExists(kPcapDir)) {
    LOG_ERR("PCAP", "Failed to create capture directory %s", kPcapDir);
    return false;
  }
  ready = openTodaysFile();
  return ready;
}

bool PcapWriter::openTodaysFile() {
  const time_t now = time(nullptr);
  struct tm tmNow;
  gmtime_r(&now, &tmNow);
  const int32_t dayOfEpoch = static_cast<int32_t>(now / 86400);

  if (dayOfEpoch == currentFileDay && !currentFilePath.empty()) {
    return true;  // already on the right day's file
  }

  char path[64];
  snprintf(path, sizeof(path), "%s/%04d%02d%02d.pcap", kPcapDir, tmNow.tm_year + 1900, tmNow.tm_mon + 1,
           tmNow.tm_mday);
  const bool isNewFile = !Storage.exists(path);
  currentFilePath = path;
  currentFileDay = dayOfEpoch;

  if (isNewFile) {
    HalFile file = Storage.open(currentFilePath.c_str(), O_WRONLY | O_CREAT | O_APPEND);
    if (!file) {
      LOG_ERR("PCAP", "Failed to create capture file: %s", currentFilePath.c_str());
      return false;
    }
    const PcapGlobalHeader hdr{kPcapMagic, kPcapVersionMajor, kPcapVersionMinor, 0, 0, kPcapSnaplen, kDltIeee80211};
    file.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    file.close();
  }
  return true;
}

void PcapWriter::tick() {
  if (!ready) return;
  openTodaysFile();  // cheap check; only does work when the day actually rolled over
}

bool PcapWriter::writeFrames(const RawFrameCapture* frames, size_t count) {
  if (!ready || count == 0) return false;
  openTodaysFile();

  // One open/write-many/close per batch rather than per frame — a real handshake bursts 4 EAPOL
  // captures within milliseconds, and each Storage.open() heap-allocates a file-handle object, so
  // this cuts that churn (and its contribution to DMA-pool fragmentation elsewhere — see
  // CHANGELOG) by up to 4x during raw capture's highest-density moment. Still open/write/close
  // rather than a held-open handle overall, for the same concurrent-reader-correctness reason
  // EncryptedLog::writeEnvelope reverted to that pattern.
  HalFile file = Storage.open(currentFilePath.c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("PCAP", "Failed to open capture file for append: %s", currentFilePath.c_str());
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    const RawFrameCapture& frame = frames[i];
    if (frame.len == 0) continue;
    const PcapRecordHeader rec{frame.unixTime, 0, static_cast<uint32_t>(frame.len), static_cast<uint32_t>(frame.len)};
    file.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
    file.write(frame.bytes, frame.len);
  }
  file.close();
  return true;
}

const char* PcapWriter::captureDirectory() { return kPcapDir; }
