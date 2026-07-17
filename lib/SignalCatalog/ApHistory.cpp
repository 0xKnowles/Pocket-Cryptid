#include "ApHistory.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <ctime>

ApHistory apHistory;

namespace {
constexpr char kFilePath[] = "/.ruby/ap_history.txt";
constexpr char kDefaultContents[] =
    "# Ruby AP history — every WiFi AP seen across this device's lifetime.\n"
    "# Format: BSSID<TAB>firstSeenUnix<TAB>lastSeenUnix<TAB>sightings<TAB>SSID\n";
constexpr unsigned long kSaveIntervalMs = 60000;

bool parseMac(const char* text, MacAddress& out) {
  unsigned int b[6];
  if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out.bytes[i] = static_cast<uint8_t>(b[i]);
  return true;
}

void formatMac(const MacAddress& mac, char* out, size_t outSize) {
  snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X", mac.bytes[0], mac.bytes[1], mac.bytes[2], mac.bytes[3],
           mac.bytes[4], mac.bytes[5]);
}

// Replaces any control byte (a stray literal tab/newline in a maliciously-crafted SSID, most
// plausibly) with '?' so a single bad entry can't corrupt this line's own tab-delimited fields.
void sanitizeForLine(char* s) {
  for (char* p = s; *p; p++) {
    if (static_cast<unsigned char>(*p) < 0x20) *p = '?';
  }
}
}  // namespace

void ApHistory::parseContents(const char* text) {
  entryCount = 0;
  const char* lineStart = text;
  while (*lineStart && entryCount < kCapacity) {
    const char* lineEnd = strchr(lineStart, '\n');
    const size_t lineLen = lineEnd ? static_cast<size_t>(lineEnd - lineStart) : strlen(lineStart);

    char line[96];
    size_t copyLen = lineLen < sizeof(line) - 1 ? lineLen : sizeof(line) - 1;
    memcpy(line, lineStart, copyLen);
    line[copyLen] = '\0';
    while (copyLen > 0 && line[copyLen - 1] == '\r') line[--copyLen] = '\0';

    if (line[0] != '\0' && line[0] != '#') {
      char macBuf[18] = {};
      char ssidBuf[33] = {};
      unsigned int firstSeen = 0;
      unsigned int lastSeen = 0;
      unsigned int sightings = 0;
      const int matched = sscanf(line, "%17[^\t]\t%u\t%u\t%u\t%32[^\n]", macBuf, &firstSeen, &lastSeen, &sightings,
                                  ssidBuf);
      MacAddress mac;
      if (matched >= 4 && parseMac(macBuf, mac)) {
        Entry& e = entries[entryCount++];
        e = Entry{};
        e.bssid = mac;
        e.firstSeenUnix = firstSeen;
        e.lastSeenUnix = lastSeen;
        e.sightings = sightings;
        if (matched == 5) {
          strncpy(e.ssid, ssidBuf, sizeof(e.ssid) - 1);
        }
        e.inUse = true;
      }
    }

    if (!lineEnd) break;
    lineStart = lineEnd + 1;
  }
}

bool ApHistory::begin() {
  if (!Storage.exists(kFilePath)) {
    Storage.ensureDirectoryExists("/.ruby");
    if (!Storage.writeFile(kFilePath, String(kDefaultContents))) {
      LOG_ERR("APHIST", "Failed to create default history file: %s", kFilePath);
    }
  }
  const String contents = Storage.readFile(kFilePath);
  parseContents(contents.c_str());
  LOG_INF("APHIST", "Loaded %u known AP(s)", static_cast<unsigned>(entryCount));
  lastSaveMs = millis();
  return true;
}

void ApHistory::observe(const WifiObservation& obs) {
  if (obs.kind != WifiFrameKind::Beacon && obs.kind != WifiFrameKind::ProbeResponse) return;

  const uint32_t now = static_cast<uint32_t>(time(nullptr));

  for (size_t i = 0; i < entryCount; i++) {
    if (entries[i].bssid == obs.bssid) {
      Entry& e = entries[i];
      e.lastSeenUnix = now;
      e.sightings++;
      if (obs.ssidLen > 0 && e.ssid[0] == '\0') {
        // A hidden network's later probe response can reveal the SSID a beacon didn't carry —
        // only overwrite once we actually have one, never blank out a previously-learned name.
        memcpy(e.ssid, obs.ssid, obs.ssidLen);
        e.ssid[obs.ssidLen] = '\0';
      }
      dirty = true;
      return;
    }
  }

  int freeSlot = -1;
  int lruSlot = -1;
  uint32_t lruTime = 0xFFFFFFFFu;
  if (entryCount < kCapacity) {
    freeSlot = static_cast<int>(entryCount);
  } else {
    lruTime = entries[0].lastSeenUnix;
    lruSlot = 0;
    for (size_t i = 1; i < kCapacity; i++) {
      if (entries[i].lastSeenUnix < lruTime) {
        lruTime = entries[i].lastSeenUnix;
        lruSlot = static_cast<int>(i);
      }
    }
  }

  const size_t slot = freeSlot >= 0 ? static_cast<size_t>(freeSlot) : static_cast<size_t>(lruSlot);
  Entry& e = entries[slot];
  e = Entry{};
  e.bssid = obs.bssid;
  e.firstSeenUnix = now;
  e.lastSeenUnix = now;
  e.sightings = 1;
  if (obs.ssidLen > 0) {
    memcpy(e.ssid, obs.ssid, obs.ssidLen);
    e.ssid[obs.ssidLen] = '\0';
  }
  e.inUse = true;
  if (freeSlot >= 0) entryCount++;
  dirty = true;
}

bool ApHistory::save() {
  String out;
  out.reserve(96 + entryCount * 64);
  out += kDefaultContents;
  char macBuf[18];
  char ssidBuf[33];
  char lineBuf[112];
  for (size_t i = 0; i < entryCount; i++) {
    const Entry& e = entries[i];
    formatMac(e.bssid, macBuf, sizeof(macBuf));
    strncpy(ssidBuf, e.ssid, sizeof(ssidBuf) - 1);
    ssidBuf[sizeof(ssidBuf) - 1] = '\0';
    sanitizeForLine(ssidBuf);
    snprintf(lineBuf, sizeof(lineBuf), "%s\t%lu\t%lu\t%lu\t%s\n", macBuf, static_cast<unsigned long>(e.firstSeenUnix),
             static_cast<unsigned long>(e.lastSeenUnix), static_cast<unsigned long>(e.sightings), ssidBuf);
    out += lineBuf;
  }
  if (!Storage.writeFile(kFilePath, out)) {
    LOG_ERR("APHIST", "Failed to save %s", kFilePath);
    return false;
  }
  dirty = false;
  return true;
}

void ApHistory::tick() {
  if (!dirty) return;
  const unsigned long now = millis();
  if (now - lastSaveMs < kSaveIntervalMs) return;
  save();
  lastSaveMs = now;
}

const ApHistory::Entry& ApHistory::at(size_t index) const { return entries[index]; }
