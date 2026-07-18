#include "VendorOui.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr char kOuiPath[] = "/.ruby/oui.txt";

char upperHex(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }

// Per-OUI-prefix result cache — RAM-only, forgotten on reboot, same ring-with-oldest-eviction
// pattern as RecentSightings/ApScanCache elsewhere in this codebase. Caches misses too (an empty
// `name` with matched=false), since an unrecognized OUI would otherwise re-scan all the way to
// EOF on every single redraw, the single worst case for this file's latency.
constexpr size_t kCacheCapacity = 32;
struct CacheEntry {
  uint8_t ouiBytes[3] = {};
  char name[32] = {};
  bool matched = false;
  bool occupied = false;
};
CacheEntry cache[kCacheCapacity];
size_t cacheNext = 0;

int findInCache(const uint8_t* ouiBytes) {
  for (size_t i = 0; i < kCacheCapacity; i++) {
    if (cache[i].occupied && memcmp(cache[i].ouiBytes, ouiBytes, 3) == 0) return static_cast<int>(i);
  }
  return -1;
}

void insertIntoCache(const uint8_t* ouiBytes, const char* name, bool matched) {
  CacheEntry& slot = cache[cacheNext];
  memcpy(slot.ouiBytes, ouiBytes, 3);
  strncpy(slot.name, name, sizeof(slot.name) - 1);
  slot.name[sizeof(slot.name) - 1] = '\0';
  slot.matched = matched;
  slot.occupied = true;
  cacheNext = (cacheNext + 1) % kCacheCapacity;
}
}  // namespace

bool lookupVendorOui(const MacAddress& mac, char* out, size_t outSize) {
  if (outSize == 0) return false;

  const int cachedIdx = findInCache(mac.bytes);
  if (cachedIdx >= 0) {
    if (!cache[cachedIdx].matched) return false;
    const size_t len = std::min(strlen(cache[cachedIdx].name), outSize - 1);
    memcpy(out, cache[cachedIdx].name, len);
    out[len] = '\0';
    return true;
  }

  if (!Storage.exists(kOuiPath)) return false;

  HalFile file = Storage.open(kOuiPath);
  if (!file) return false;

  char prefix[7];
  snprintf(prefix, sizeof(prefix), "%02X%02X%02X", mac.bytes[0], mac.bytes[1], mac.bytes[2]);

  char line[128];
  size_t idx = 0;
  bool found = false;
  char matchedName[32] = {};
  int c;
  while (!found && (c = file.read()) >= 0) {
    if (c == '\n' || idx >= sizeof(line) - 1) {
      line[idx] = '\0';
      if (idx >= 6) {
        bool match = true;
        for (int i = 0; i < 6; i++) {
          if (upperHex(line[i]) != prefix[i]) {
            match = false;
            break;
          }
        }
        if (match) {
          size_t nameStart = 6;
          while (nameStart < idx && (line[nameStart] == '\t' || line[nameStart] == ' ')) nameStart++;
          if (nameStart < idx) {
            const size_t fullLen = std::min(idx - nameStart, sizeof(matchedName) - 1);
            memcpy(matchedName, line + nameStart, fullLen);
            matchedName[fullLen] = '\0';
            found = true;
          }
        }
      }
      idx = 0;
    } else if (idx < sizeof(line) - 1) {
      line[idx++] = static_cast<char>(c);
    }
  }
  file.close();

  insertIntoCache(mac.bytes, matchedName, found);

  if (found) {
    const size_t copyLen = std::min(strlen(matchedName), outSize - 1);
    memcpy(out, matchedName, copyLen);
    out[copyLen] = '\0';
  }
  return found;
}
