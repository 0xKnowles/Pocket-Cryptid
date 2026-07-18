#pragma once

#include <cstddef>
#include <cstdint>

#include "RfTypes.h"

// Active WiFi attack capability: transmits 802.11 deauthentication frames at a target BSSID's
// clients to force a re-authentication, so the resulting WPA handshake lands in WifiSniffer's
// raw-capture path (see PcapWriter) instead of waiting — often in vain — for one to happen on
// its own while passively hopping channels.
//
// LEGAL/ETHICAL: transmitting deauthentication frames at a network you do not own or do not
// have explicit authorization to test is illegal in most jurisdictions (wireless interference),
// regardless of how small or "hobbyist" the transmitting device is. This capability is off by
// default (RubySettings::activeDeauthEnabled) and every target is additionally gated through
// TargetList — whose resting state with both lists empty is "attack nothing" — specifically so
// enabling this feature can never itself cause every network in range to be attacked; the owner
// has to explicitly add a BSSID first, either by editing /.ruby/targets.txt or from Settings'
// Whitelist/Blacklist rows (TargetPickerActivity, a live network scan).
//
// Frame transmission uses esp_wifi_80211_tx(), the same raw-TX primitive essentially every
// community ESP32 deauther project uses. ESP-IDF's own doxygen comment for that function lists
// "beacon/probe request/probe response/action and non-QoS data frame" as supported — deauth
// (a different management subtype) is not on that list.
//
// **Transmit capability: confirmed NOT functional on stock ESP-IDF, via real-hardware testing.**
// The transient-WIFI_MODE_STA approach (switching only for the duration of one burst, then
// immediately reverting to WIFI_MODE_NULL) did fix the earlier crash-loop concern — many bursts
// fired across a real test session with no crash and no DMA-pool circuit-breaker trip. But every
// single burst's `esp_wifi_80211_tx()` call was rejected by the WiFi driver itself, which logs
// "wifi: unsupport frame type: 0c0" once per rejected frame (0x00C0 == kFrameControlDeauth) —
// confirmed via serial log, and matches ESP-IDF's own documented/community-reported behavior:
// esp_wifi_80211_tx()'s frame-type allowlist (beacon/probe request/probe response/action/non-QoS
// data) hard-codes out deauth specifically, at the driver level, regardless of what this class or
// WifiSniffer's WIFI_MODE_STA sequencing does. See framesRejectedByDriver() — if it equals
// framesTransmitted(), which is the observed norm rather than an occasional glitch, nothing has
// actually reached the air despite bursts/frames counting up normally.
//
// The only known way past this (used by some community deauther projects) is replacing ESP-IDF's
// precompiled libnet80211.a with a reverse-engineered patched build that removes the frame-type
// check — a binary patch to the SDK itself, not an application-level fix, and out of scope here
// unless a future session takes that on deliberately. Until then, this class still runs (bursts
// still get attempted, counted, and logged) but should be treated as a no-op for actually forcing
// a handshake — [Raw handshake capture](#raw-handshake-capture-crackable-pcap-export)'s passive
// path, optionally with `Settings → WiFi channel scope` locked to a known target's channel, is the
// only capture path confirmed to actually work on this firmware.
class DeauthEngine {
 public:
  bool begin();

  // Master on/off switch (mirrors WifiSniffer::setRawCaptureEnabled's pattern). Cheap to flip at
  // runtime — just gates onObservation(); no radio reconfiguration happens here, since the
  // WIFI_MODE_STA switch only ever brackets an individual burst (see sendDeauthBurst).
  void setEnabled(bool value);
  bool isEnabled() const { return enabled; }

  // Feed every WiFi observation through here — same stream WifiSniffer's ObservationCallback
  // already produces. Decides whether this BSSID is worth a deauth burst right now (allowed by
  // TargetList, past its cooldown, no handshake captured for it yet this session) and, if so,
  // sends one.
  void onObservation(const WifiObservation& obs);

  uint32_t burstsSent() const { return totalBursts; }
  uint32_t framesTransmitted() const { return totalFrames; }

  // How many of framesTransmitted()'s attempts esp_wifi_80211_tx() itself rejected outright,
  // logging "wifi: unsupport frame type: 0c0" — confirmed via real-hardware testing to be every
  // single one. See this class's own comment above: stock
  // ESP-IDF's raw-TX path only allows beacon/probe request/probe response/action/non-QoS-data
  // frames through; deauth (a different management subtype, frame control 0x00C0) isn't on that
  // allowlist and gets rejected at the driver level before it ever reaches the air, regardless of
  // what this class does. If this equals framesTransmitted(), nothing has actually transmitted.
  uint32_t framesRejectedByDriver() const { return totalFramesRejected; }

 private:
  // Minimum gap between deauth bursts aimed at the same BSSID — keeps this from hammering one
  // network continuously while channel-hopping repeatedly passes over it, which would be both
  // needless disruption and a waste of the tiny airtime budget a 300ms/channel dwell already has.
  static constexpr uint32_t kCooldownMs = 30000;
  // Frames per burst: enough that a client reliably notices and re-associates even with the
  // occasional dropped frame, without turning this into a sustained flood.
  static constexpr uint8_t kFramesPerBurst = 6;
  // Field-tested: 16 was nowhere near enough in a real dense RF environment (a dozen-plus
  // beaconing BSSIDs is normal even in a single apartment building). Once full, trackerFor()
  // evicts the least-recently-bursted entry — with too small a table, that constantly evicted
  // whatever BSSID's cooldown was closest to expiring, wiping its memory and letting it burst
  // again within milliseconds instead of respecting kCooldownMs. 64 gives real headroom; each
  // entry is small (~13 bytes), so the RAM cost is trivial.
  static constexpr size_t kTrackCapacity = 64;

  struct BssidTrack {
    uint32_t bssidHash = 0;
    unsigned long lastBurstMs = 0;
    bool handshakeSeen = false;
    bool inUse = false;
  };

  BssidTrack& trackerFor(uint32_t bssidHash);
  bool sendDeauthBurst(const MacAddress& bssid, uint8_t channel);

  bool enabled = false;
  uint32_t totalBursts = 0;
  uint32_t totalFrames = 0;
  uint32_t totalFramesRejected = 0;
  BssidTrack trackers[kTrackCapacity] = {};
};

extern DeauthEngine deauthEngine;  // singleton, defined in DeauthEngine.cpp
