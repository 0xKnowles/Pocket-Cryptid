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

bool PcapWriter::writeFrame(const uint8_t* frame, size_t len, uint32_t unixTime) {
  if (!ready || len == 0) return false;
  openTodaysFile();

  // Open/write/close per record rather than holding a handle open — same reasoning as
  // EncryptedLog::writeEnvelope (see that file's comment): keeping a persistent handle caused a
  // concurrent-reader correctness bug there, and these writes are rare enough (a handful of
  // frames per handshake) that the per-call open() cost doesn't matter.
  HalFile file = Storage.open(currentFilePath.c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("PCAP", "Failed to open capture file for append: %s", currentFilePath.c_str());
    return false;
  }
  const PcapRecordHeader rec{unixTime, 0, static_cast<uint32_t>(len), static_cast<uint32_t>(len)};
  file.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
  file.write(frame, len);
  file.close();
  return true;
}

const char* PcapWriter::captureDirectory() { return kPcapDir; }
