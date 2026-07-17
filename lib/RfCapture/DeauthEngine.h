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
// TargetList, whose own default mode is Whitelist — attack *nothing* — specifically so enabling
// this feature can never itself cause every network in range to be attacked; the owner has to
// explicitly add a BSSID first, either by editing /.ruby/targets.txt or from the on-device
// device list / settings screen.
//
// Frame transmission uses esp_wifi_80211_tx(), the same raw-TX primitive essentially every
// community ESP32 deauther project uses. ESP-IDF's own doxygen comment for that function lists
// "beacon/probe request/probe response/action and non-QoS data frame" as supported — deauth
// (a different management subtype) is not on that list.
//
// **Currently non-functional on real hardware.** esp_wifi_80211_tx(WIFI_IF_STA, ...) needs the
// radio in WIFI_MODE_STA, but real X3 hardware testing showed that mode starves the interrupt
// matrix badly enough that esp-aes (hardware crypto, needed for EncryptedLog's every write)
// can't allocate its own interrupt and aborts on every boot — a crash loop, not a targeted
// failure, since WifiSniffer brought the radio up in STA mode unconditionally rather than only
// while this was armed. Reverted WifiSniffer back to WIFI_MODE_NULL (see its class comment), so
// sendDeauthBurst() below now calls esp_wifi_80211_tx() with no STA interface up — it fails and
// logs an error instead of transmitting, which is safe but means this class currently does
// nothing even when enabled. Left in place, not deleted, pending a way to get TX capability on
// this hardware without breaking crypto — a transient, carefully-scoped mode switch only for the
// duration of a burst is the most likely path, but that needs real hardware iteration this
// environment can't do.
class DeauthEngine {
 public:
  bool begin();

  // Master on/off switch (mirrors WifiSniffer::setRawCaptureEnabled's pattern). Cheap to flip at
  // runtime — just gates onObservation(), no radio reconfiguration needed since WifiSniffer
  // already brings the interface up in a TX-capable mode (see WifiSniffer::begin).
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
  static constexpr size_t kTrackCapacity = 16;

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
