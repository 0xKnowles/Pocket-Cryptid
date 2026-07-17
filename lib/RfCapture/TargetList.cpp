#include "TargetList.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

TargetList targetList;

namespace {
constexpr char kFilePath[] = "/.ruby/targets.txt";
constexpr char kDefaultContents[] =
    "# Ruby active-mode target list\n"
    "#\n"
    "# mode=whitelist -> DeauthEngine only attacks BSSIDs listed below.\n"
    "# mode=blacklist -> DeauthEngine attacks every BSSID it sees EXCEPT those listed below.\n"
    "# Empty whitelist (the default) means DeauthEngine attacks nothing until you add a BSSID —\n"
    "# either by editing this file directly or from the on-device device list / settings screen.\n"
    "#\n"
    "mode=whitelist\n";

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
}  // namespace

void TargetList::parseContents(const char* text) {
  listMode = TargetListMode::Whitelist;
  entryCount = 0;

  const char* lineStart = text;
  while (*lineStart) {
    const char* lineEnd = strchr(lineStart, '\n');
    const size_t lineLen = lineEnd ? static_cast<size_t>(lineEnd - lineStart) : strlen(lineStart);

    char line[40];
    size_t copyLen = lineLen < sizeof(line) - 1 ? lineLen : sizeof(line) - 1;
    memcpy(line, lineStart, copyLen);
    line[copyLen] = '\0';
    // Trim trailing carriage return / whitespace.
    while (copyLen > 0 && (line[copyLen - 1] == '\r' || line[copyLen - 1] == ' ')) {
      line[--copyLen] = '\0';
    }
    // Trim leading whitespace.
    char* trimmed = line;
    while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

    if (trimmed[0] == '\0' || trimmed[0] == '#') {
      // blank/comment
    } else if (strncmp(trimmed, "mode=", 5) == 0) {
      const char* value = trimmed + 5;
      if (strcmp(value, "blacklist") == 0) {
        listMode = TargetListMode::Blacklist;
      } else {
        listMode = TargetListMode::Whitelist;
      }
    } else if (entryCount < kCapacity) {
      MacAddress mac;
      if (parseMac(trimmed, mac)) {
        entries[entryCount++] = mac;
      }
    }

    if (!lineEnd) break;
    lineStart = lineEnd + 1;
  }
}

bool TargetList::begin() {
  if (!Storage.exists(kFilePath)) {
    Storage.ensureDirectoryExists("/.ruby");
    if (!Storage.writeFile(kFilePath, String(kDefaultContents))) {
      LOG_ERR("TARGETS", "Failed to create default target list: %s", kFilePath);
    }
  }
  ready = reload();
  return ready;
}

bool TargetList::reload() {
  const String contents = Storage.readFile(kFilePath);
  parseContents(contents.c_str());
  LOG_INF("TARGETS", "Loaded %u target(s), mode=%s", static_cast<unsigned>(entryCount),
          listMode == TargetListMode::Whitelist ? "whitelist" : "blacklist");
  return true;
}

bool TargetList::save() {
  String out;
  out.reserve(64 + entryCount * 19);
  out += "# Ruby active-mode target list\n";
  out += "mode=";
  out += (listMode == TargetListMode::Whitelist) ? "whitelist" : "blacklist";
  out += "\n";
  char macBuf[18];
  for (size_t i = 0; i < entryCount; i++) {
    formatMac(entries[i], macBuf, sizeof(macBuf));
    out += macBuf;
    out += "\n";
  }
  if (!Storage.writeFile(kFilePath, out)) {
    LOG_ERR("TARGETS", "Failed to save target list: %s", kFilePath);
    return false;
  }
  return true;
}

bool TargetList::isAllowed(const MacAddress& bssid) const {
  const bool listed = contains(bssid);
  return listMode == TargetListMode::Whitelist ? listed : !listed;
}

void TargetList::setMode(TargetListMode newMode) {
  if (listMode == newMode) return;
  listMode = newMode;
  save();
}

const MacAddress& TargetList::at(size_t index) const { return entries[index]; }

bool TargetList::contains(const MacAddress& mac) const {
  for (size_t i = 0; i < entryCount; i++) {
    if (entries[i] == mac) return true;
  }
  return false;
}

bool TargetList::add(const MacAddress& mac) {
  if (contains(mac) || entryCount >= kCapacity) return false;
  entries[entryCount++] = mac;
  return save();
}

bool TargetList::remove(const MacAddress& mac) {
  for (size_t i = 0; i < entryCount; i++) {
    if (entries[i] == mac) {
      for (size_t j = i; j + 1 < entryCount; j++) entries[j] = entries[j + 1];
      entryCount--;
      return save();
    }
  }
  return false;
}
