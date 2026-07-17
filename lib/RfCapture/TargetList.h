#pragma once

#include <cstddef>
#include <cstdint>

#include "RfTypes.h"

// Which of TargetList's two independent lists an operation applies to.
enum class TargetListKind : uint8_t { Whitelist = 0, Blacklist = 1 };

// The BSSID allow/deny lists that gate which networks DeauthEngine is permitted to send
// deauthentication frames at — two genuinely independent lists, not one list with a mode switch:
//   - Whitelist non-empty: ONLY BSSIDs on it are attacked. Blacklist is ignored in this case —
//     once you've named exactly what to attack, an exclusion list doesn't add anything.
//   - Whitelist empty (the default): every BSSID DeauthEngine sees is attacked EXCEPT those on
//     the blacklist. An empty whitelist AND empty blacklist means "attack everything" is
//     deliberately *not* the resting state here — see isAllowed()'s doc comment for how the
//     empty/empty case is actually handled.
//
// Backed by a small human-editable text file on the SD card (see kFilePath in the .cpp) so the
// owner can build/edit it on a PC with the card out, in addition to the on-device
// TargetPickerActivity (reached from Settings) that adds entries from a live scan.
//
// File format ('#' starts a comment, blank lines ignored, entries grouped under a section
// header):
//   [whitelist]
//   AA:BB:CC:DD:EE:FF
//   [blacklist]
//   11:22:33:44:55:66
class TargetList {
 public:
  static constexpr size_t kCapacity = 64;  // per list

  // Loads from SD, creating a default (both lists empty) file if none exists yet.
  bool begin();
  // Re-reads the file from SD — call after the owner may have edited it externally (e.g. the
  // card was pulled, edited on a PC, and reinserted).
  bool reload();
  // Writes both lists back to SD. Called automatically by add()/remove().
  bool save();

  // Three-way: whitelist non-empty -> whitelist-only. Else blacklist non-empty ->
  // attack-all-except-blacklist. Else (both empty, the fresh-install default) -> attack nothing.
  // That last case is deliberate, not a fallthrough: it's what keeps turning DeauthEngine on
  // from ever defaulting to "attack every network in range."
  bool isAllowed(const MacAddress& bssid) const;
  bool isEmpty() const { return whitelist.count == 0 && blacklist.count == 0; }
  bool whitelistEmpty() const { return whitelist.count == 0; }

  size_t count(TargetListKind kind) const;
  const MacAddress& at(TargetListKind kind, size_t index) const;
  bool contains(TargetListKind kind, const MacAddress& mac) const;
  // Returns false if already present or the list is full.
  bool add(TargetListKind kind, const MacAddress& mac);
  // Returns false if not present.
  bool remove(TargetListKind kind, const MacAddress& mac);

 private:
  struct List {
    MacAddress entries[kCapacity];
    size_t count = 0;
  };

  void parseContents(const char* text);
  List& listFor(TargetListKind kind);
  const List& listFor(TargetListKind kind) const;

  List whitelist;
  List blacklist;
  bool ready = false;
};

extern TargetList targetList;  // singleton, defined in TargetList.cpp
