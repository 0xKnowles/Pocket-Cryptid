#pragma once

#include <cstddef>

#include "RfTypes.h"

// Optional vendor-name lookup for a MAC's OUI (first 3 bytes), read from an SD file the owner
// supplies themselves — /.ruby/oui.txt, one "AABBCC<TAB>Vendor Name" entry per line (the same
// format IEEE's public OUI registry export and Wireshark's manuf file both use, so either can be
// dropped in directly). No database ships in firmware; this reads and scans the file directly
// on every call, so it costs nothing when the owner hasn't bothered to add one — but a full
// ~50,000-entry registry scanned once per visible row per redraw is real SD latency. Keep the
// file to a curated subset of vendors you actually care about if you notice screens feeling
// sluggish with it installed; see README.
//
// Writes up to outSize-1 bytes of the vendor name into `out` and returns true on a match; returns
// false (leaving `out` untouched) if the file doesn't exist or no entry matches.
bool lookupVendorOui(const MacAddress& mac, char* out, size_t outSize);
