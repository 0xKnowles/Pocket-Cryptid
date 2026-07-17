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
// **Transmit capability: re-enabled, pending real-hardware confirmation.** The first attempt
// (WifiSniffer bringing the radio up in WIFI_MODE_STA unconditionally, for the whole session)
// crash-looped real X3 hardware: esp-aes (hardware crypto, needed for EncryptedLog's every write)
// couldn't allocate its own interrupt and aborted, every single boot. WifiSniffer stayed reverted
// to WIFI_MODE_NULL as its resting state (see its class comment) — but the actual trigger for
// that crash looks like it was *ordering*, not concurrency: STA mode came up before
// EncryptedLog had ever written a record, so the interrupt-hungry STA driver and esp-aes's very
// first interrupt request collided at the worst possible moment (boot). With WIFI_MODE_NULL as
// the resting state, esp-aes claims and holds its interrupt during ordinary logging long before
// any burst can fire, so sendDeauthBurst() now switches to WIFI_MODE_STA only for the duration of
// one burst (a handful of milliseconds), then immediately reverts to WIFI_MODE_NULL — asking the
// driver to reconfigure a radio that's already running, not to grab a fresh interrupt at boot.
// This mirrors what github.com/yattsu/biscuit's WiFi deauther does on the same hardware (STA mode
// + esp_wifi_80211_tx), with one difference: Biscuit never runs anything like EncryptedLog's
// always-on background AES logging concurrently with it, so it never had a reason to discover
// (or avoid) the ordering issue above. **Still needs confirmation on a real device** — this
// environment can't compile-test interrupt behavior or watch for the DMA-pool fragmentation that
// repeatedly toggling STA mode over a long session could plausibly cause (the existing
// heap-health circuit breaker in main.cpp is the safety net if that happens: a silent restart,
// not a hard crash).
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
  BssidTrack trackers[kTrackCapacity] = {};
};

extern DeauthEngine deauthEngine;  // singleton, defined in DeauthEngine.cpp
