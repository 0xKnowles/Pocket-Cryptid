#include "TargetList.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

TargetList targetList;

namespace {
constexpr char kFilePath[] = "/.ruby/targets.txt";
constexpr char kDefaultContents[] =
    "# Ruby active-mode target lists\n"
    "#\n"
    "# Whitelist non-empty -> only these BSSIDs are attacked (blacklist ignored).\n"
    "# Whitelist empty, blacklist non-empty -> every BSSID except these is attacked.\n"
    "# Both empty (the default) -> nothing is attacked, even with active deauth switched on.\n"
    "# Add entries from a PC with the SD card out, or on-device from Settings.\n"
    "#\n"
    "[whitelist]\n"
    "[blacklist]\n";

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

TargetList::List& TargetList::listFor(TargetListKind kind) {
  return kind == TargetListKind::Whitelist ? whitelist : blacklist;
}

const TargetList::List& TargetList::listFor(TargetListKind kind) const {
  return kind == TargetListKind::Whitelist ? whitelist : blacklist;
}

void TargetList::parseContents(const char* text) {
  whitelist.count = 0;
  blacklist.count = 0;
  List* active = &whitelist;

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
    } else if (strcmp(trimmed, "[whitelist]") == 0) {
      active = &whitelist;
    } else if (strcmp(trimmed, "[blacklist]") == 0) {
      active = &blacklist;
    } else if (active->count < kCapacity) {
      MacAddress mac;
      if (parseMac(trimmed, mac)) {
        active->entries[active->count++] = mac;
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
  LOG_INF("TARGETS", "Loaded %u whitelist, %u blacklist target(s)", static_cast<unsigned>(whitelist.count),
          static_cast<unsigned>(blacklist.count));
  return true;
}

bool TargetList::save() {
  String out;
  out.reserve(96 + (whitelist.count + blacklist.count) * 19);
  out += "# Ruby active-mode target lists\n";
  out += "[whitelist]\n";
  char macBuf[18];
  for (size_t i = 0; i < whitelist.count; i++) {
    formatMac(whitelist.entries[i], macBuf, sizeof(macBuf));
    out += macBuf;
    out += "\n";
  }
  out += "[blacklist]\n";
  for (size_t i = 0; i < blacklist.count; i++) {
    formatMac(blacklist.entries[i], macBuf, sizeof(macBuf));
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
  if (whitelist.count > 0) return contains(TargetListKind::Whitelist, bssid);
  if (blacklist.count > 0) return !contains(TargetListKind::Blacklist, bssid);
  return false;  // both empty: attack nothing — see class comment
}

size_t TargetList::count(TargetListKind kind) const { return listFor(kind).count; }

const MacAddress& TargetList::at(TargetListKind kind, size_t index) const { return listFor(kind).entries[index]; }

bool TargetList::contains(TargetListKind kind, const MacAddress& mac) const {
  const List& list = listFor(kind);
  for (size_t i = 0; i < list.count; i++) {
    if (list.entries[i] == mac) return true;
  }
  return false;
}

bool TargetList::add(TargetListKind kind, const MacAddress& mac) {
  if (contains(kind, mac)) return false;
  List& list = listFor(kind);
  if (list.count >= kCapacity) return false;
  list.entries[list.count++] = mac;
  return save();
}

bool TargetList::remove(TargetListKind kind, const MacAddress& mac) {
  List& list = listFor(kind);
  for (size_t i = 0; i < list.count; i++) {
    if (list.entries[i] == mac) {
      for (size_t j = i; j + 1 < list.count; j++) list.entries[j] = list.entries[j + 1];
      list.count--;
      return save();
    }
  }
  return false;
}
