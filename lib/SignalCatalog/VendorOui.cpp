#include "VendorOui.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr char kOuiPath[] = "/.ruby/oui.txt";

char upperHex(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }
}  // namespace

bool lookupVendorOui(const MacAddress& mac, char* out, size_t outSize) {
  if (outSize == 0 || !Storage.exists(kOuiPath)) return false;

  HalFile file = Storage.open(kOuiPath);
  if (!file) return false;

  char prefix[7];
  snprintf(prefix, sizeof(prefix), "%02X%02X%02X", mac.bytes[0], mac.bytes[1], mac.bytes[2]);

  char line[128];
  size_t idx = 0;
  bool found = false;
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
            const size_t copyLen = std::min(idx - nameStart, outSize - 1);
            memcpy(out, line + nameStart, copyLen);
            out[copyLen] = '\0';
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
  return found;
}
