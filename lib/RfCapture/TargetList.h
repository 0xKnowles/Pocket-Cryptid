#pragma once

#include <cstddef>
#include <cstdint>

#include "RfTypes.h"

// Whitelist=attack only listed BSSIDs; Blacklist=attack every BSSID except listed ones.
// Whitelist is the default (see TargetList::begin) specifically so turning DeauthEngine on
// can never accidentally attack every network in range just because the list is empty.
enum class TargetListMode : uint8_t { Whitelist = 0, Blacklist = 1 };

// The BSSID allow/deny list that gates which networks DeauthEngine is permitted to send
// deauthentication frames at. Backed by a small human-editable text file on the SD card
// (see kFilePath in the .cpp) so the owner can build/edit it on a PC with the card out, in
// addition to the on-device add/remove support in DeviceListActivity and SettingsActivity's
// mode toggle.
//
// File format (one directive/entry per line, '#' starts a comment, blank lines ignored):
//   mode=whitelist        (or mode=blacklist)
//   AA:BB:CC:DD:EE:FF
//   11:22:33:44:55:66
class TargetList {
 public:
  static constexpr size_t kCapacity = 64;

  // Loads from SD, creating a default (empty, whitelist-mode) file if none exists yet.
  bool begin();
  // Re-reads the file from SD — call after the owner may have edited it externally (e.g. the
  // card was pulled, edited on a PC, and reinserted).
  bool reload();
  // Writes the current mode + list back to SD. Called automatically by add()/remove()/setMode().
  bool save();

  bool isAllowed(const MacAddress& bssid) const;

  TargetListMode mode() const { return listMode; }
  void setMode(TargetListMode newMode);

  size_t count() const { return entryCount; }
  const MacAddress& at(size_t index) const;
  bool contains(const MacAddress& mac) const;
  // Returns false if already present or the list is full.
  bool add(const MacAddress& mac);
  // Returns false if not present.
  bool remove(const MacAddress& mac);

 private:
  void parseContents(const char* text);

  TargetListMode listMode = TargetListMode::Whitelist;
  MacAddress entries[kCapacity];
  size_t entryCount = 0;
  bool ready = false;
};

extern TargetList targetList;  // singleton, defined in TargetList.cpp
