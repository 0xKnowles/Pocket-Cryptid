#pragma once

#include <cstdint>

#include "RfTypes.h"

// Passive counterpart to DeauthEngine: watches the deauth/disassoc management frames WifiSniffer
// already classifies (see its class comment) and flags when their rate spikes — evidence that
// *something* is actively attacking a nearby network, not necessarily this device. This never
// transmits anything and never changes what WifiSniffer captures; it only counts.
//
// Self-detection isn't a concern here: a radio can't receive on the same antenna it's actively
// transmitting from, so DeauthEngine's own bursts (see its class comment on the transient
// WIFI_MODE_STA switch) are physically invisible to this device's own promiscuous RX path. Doubly
// moot in practice right now — DeauthEngine's bursts are confirmed rejected by the WiFi driver
// before they ever reach the air (see its own class comment), so there's nothing to self-filter.
class DeauthDetector {
 public:
  void onObservation(const WifiObservation& obs);

  uint32_t totalDeauthFrames() const { return totalDeauth; }
  uint32_t totalDisassocFrames() const { return totalDisassoc; }

  // True for kAlertDurationMs after a spike (kSpikeThreshold+ deauth/disassoc frames within
  // kSpikeWindowMs) — callers (DashboardActivity) use this for a one-shot banner rather than a
  // permanently-lit indicator that would never turn off again after the first attack.
  bool alertActive() const;

 private:
  static constexpr uint32_t kSpikeWindowMs = 5000;
  static constexpr uint8_t kSpikeThreshold = 5;
  static constexpr uint32_t kAlertDurationMs = 15000;

  uint32_t totalDeauth = 0;
  uint32_t totalDisassoc = 0;

  unsigned long windowStartMs = 0;
  uint8_t windowCount = 0;
  unsigned long alertUntilMs = 0;
};

extern DeauthDetector deauthDetector;  // singleton, defined in DeauthDetector.cpp
