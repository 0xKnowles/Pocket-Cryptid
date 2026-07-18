#pragma once

#include <cstddef>

#include "RfTypes.h"

// Optional vendor-name lookup for a MAC's OUI (first 3 bytes), read from an SD file the owner
// supplies themselves — /.ruby/oui.txt, one "AABBCC<TAB>Vendor Name" entry per line (the same
// format IEEE's public OUI registry export and Wireshark's manuf file both use, so either can be
// dropped in directly). No database ships in firmware, and this can't assume the file is sorted
// (a hand-curated or concatenated one might not be), so a cold lookup still scans it linearly —
// but a small per-OUI-prefix result cache (see VendorOui.cpp) means the *same* vendor, looked up
// repeatedly across redraws of the same handful of on-screen devices (the common case — Recent
// Devices/Device Log redraw the same ~16 entries every tick, not a fresh batch each time), only
// ever touches the SD card once. A full ~50,000-entry registry scanned from scratch on every
// visible row on every redraw was real, noticeable SD latency before this cache existed; keep the
// file to a curated subset of vendors you actually care about if screens still feel sluggish with
// it installed — see README.
//
// Writes up to outSize-1 bytes of the vendor name into `out` and returns true on a match; returns
// false (leaving `out` untouched) if the file doesn't exist or no entry matches.
bool lookupVendorOui(const MacAddress& mac, char* out, size_t outSize);
