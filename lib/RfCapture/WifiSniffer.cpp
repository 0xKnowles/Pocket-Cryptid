#include "WifiSniffer.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstddef>
#include <cstring>
#include <ctime>

WifiSniffer wifiSniffer;

namespace {

// Minimal 802.11 header. addr4 (WDS, ToDS&&FromDS) is never present on the frame kinds we care
// about (beacons/probes/EAPOL from an infrastructure BSS) so it is intentionally not modeled.
struct Ieee80211Hdr {
  uint16_t frameControl;
  uint16_t durationId;
  uint8_t addr1[6];
  uint8_t addr2[6];
  uint8_t addr3[6];
  uint16_t seqCtrl;
} __attribute__((packed));

constexpr uint8_t FC_TYPE_MGMT = 0;
constexpr uint8_t FC_TYPE_DATA = 2;
constexpr uint8_t FC_SUBTYPE_BEACON = 8;
constexpr uint8_t FC_SUBTYPE_PROBE_REQ = 4;
constexpr uint8_t FC_SUBTYPE_PROBE_RESP = 5;

inline uint8_t frameType(uint16_t fc) { return (fc >> 2) & 0x3; }
inline uint8_t frameSubtype(uint16_t fc) { return (fc >> 4) & 0xF; }
inline bool frameToDS(uint16_t fc) { return (fc >> 8) & 0x1; }
inline bool frameFromDS(uint16_t fc) { return (fc >> 9) & 0x1; }

MacAddress toMac(const uint8_t* src) {
  MacAddress mac;
  memcpy(mac.bytes, src, 6);
  return mac;
}

// Reads the SSID (tag 0) out of a beacon/probe-request/probe-response information-element
// block. `len` is the remaining payload length after the fixed-size management header fields.
bool extractSsid(const uint8_t* ies, size_t len, char* out, uint8_t* outLen) {
  size_t i = 0;
  while (i + 2 <= len) {
    const uint8_t tag = ies[i];
    const uint8_t tagLen = ies[i + 1];
    if (i + 2 + tagLen > len) break;
    if (tag == 0) {  // SSID element
      const uint8_t copyLen = tagLen > 32 ? 32 : tagLen;
      memcpy(out, &ies[i + 2], copyLen);
      out[copyLen] = '\0';
      *outLen = copyLen;
      return true;
    }
    i += 2 + tagLen;
  }
  return false;
}

// Best-effort classification of a WPA/WPA2 4-way handshake message (1-4) from the EAPOL-Key
// frame's Key Information field, following the same bit heuristics as common passive capture
// tools (KeyACK / KeyMIC / Secure / Key Data Length). We only ever look at these flag bits and
// the frame's addressing — never at the key material itself.
uint8_t classifyEapolMessage(const uint8_t* eapolKeyFrame, size_t len) {
  if (len < 4) return 0;
  const uint16_t keyInfo = (eapolKeyFrame[1] << 8) | eapolKeyFrame[2];
  const bool keyAck = keyInfo & 0x0080;
  const bool keyMic = keyInfo & 0x0100;
  const bool secure = keyInfo & 0x0200;
  uint16_t keyDataLen = 0;
  if (len >= 99) {
    keyDataLen = (eapolKeyFrame[97] << 8) | eapolKeyFrame[98];
  }

  if (keyAck && !keyMic) return 1;
  if (!keyAck && keyMic && !secure) return 2;
  if (keyAck && keyMic && secure) return 3;
  if (!keyAck && keyMic && secure && keyDataLen == 0) return 4;
  return 0;
}

constexpr uint8_t kLlcSnapEapol[] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};

// EAPOL handshakes are 4 frames and a tracked BSSID's SSID beacon is captured once each — this
// only ever needs to hold a handful of entries at a time even during a busy capture session.
constexpr size_t kRawQueueCapacity = 8;

}  // namespace

bool WifiSniffer::begin() {
  if (initialized) return true;

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    LOG_ERR("RFSNIFF", "esp_netif_init failed: %d", err);
    return false;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    LOG_ERR("RFSNIFF", "esp_event_loop_create_default failed: %d", err);
    return false;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
    LOG_ERR("RFSNIFF", "esp_wifi_init failed: %d", err);
    return false;
  }
  // Never persist captured-network artifacts (channel/last-mode) to flash — this device never
  // joins anything, so there's nothing worth surviving a reboot, and it avoids flash wear from a
  // radio that's expected to run for most of the device's on-time.
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_NULL);
  err = esp_wifi_start();
  if (err != ESP_OK) {
    LOG_ERR("RFSNIFF", "esp_wifi_start failed: %d", err);
    return false;
  }

  rxQueue = xQueueCreate(48, sizeof(WifiObservation));
  if (rxQueue == nullptr) {
    LOG_ERR("RFSNIFF", "Failed to allocate capture queue");
    return false;
  }

  initialized = true;
  LOG_INF("RFSNIFF", "WiFi monitor mode initialized");
  return true;
}

void WifiSniffer::end() {
  stop();
  if (initialized) {
    esp_wifi_stop();
    esp_wifi_deinit();
    initialized = false;
  }
  if (rxQueue) {
    vQueueDelete(static_cast<QueueHandle_t>(rxQueue));
    rxQueue = nullptr;
  }
  if (rawQueue) {
    vQueueDelete(static_cast<QueueHandle_t>(rawQueue));
    rawQueue = nullptr;
  }
  rawCaptureEnabled = false;
}

void WifiSniffer::setRawCaptureEnabled(bool enabled) {
  if (enabled && !rawQueue) {
    rawQueue = xQueueCreate(kRawQueueCapacity, sizeof(RawFrameCapture));
    if (!rawQueue) {
      LOG_ERR("RFSNIFF", "Failed to allocate raw capture queue");
      enabled = false;
    } else {
      memset(eapolBssidTracks, 0, sizeof(eapolBssidTracks));
      LOG_INF("RFSNIFF", "Raw handshake capture enabled");
    }
  } else if (!enabled && rawCaptureEnabled) {
    LOG_INF("RFSNIFF", "Raw handshake capture disabled");
  }
  rawCaptureEnabled = enabled && rawQueue != nullptr;
}

bool WifiSniffer::start(const uint8_t* chans, size_t count, uint32_t dwell, bool captureRawFrames) {
  if (!initialized && !begin()) return false;
  if (running) stop();

  channels = chans;
  channelCount = count;
  channelIndex = 0;
  dwellMs = dwell;
  setRawCaptureEnabled(captureRawFrames);

  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&WifiSniffer::promiscuousRxCallback);
  esp_wifi_set_channel(channels[0], WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);

  running = true;
  lastHopAt = millis();
  LOG_INF("RFSNIFF", "Monitor mode started on channel %u", channels[0]);
  return true;
}

void WifiSniffer::stop() {
  if (!running) return;
  esp_wifi_set_promiscuous(false);
  running = false;
  LOG_INF("RFSNIFF", "Monitor mode stopped");
}

void WifiSniffer::tick() {
  if (!initialized) return;

  if (running && channelCount > 1) {
    const unsigned long now = millis();
    if (now - lastHopAt >= dwellMs) {
      channelIndex = (channelIndex + 1) % channelCount;
      esp_wifi_set_channel(channels[channelIndex], WIFI_SECOND_CHAN_NONE);
      lastHopAt = now;
    }
  }

  if (rxQueue && callback) {
    WifiObservation obs;
    while (xQueueReceive(static_cast<QueueHandle_t>(rxQueue), &obs, 0) == pdTRUE) {
      callback(obs);
    }
  }

  if (rawQueue && rawCallback) {
    // Drain everything currently queued into one local batch and hand it to the callback in a
    // single call, rather than once per frame — a handshake's 4 EAPOL captures usually all land
    // in the queue within the same tick(), so writing them with one open/write-many/close cycle
    // instead of 4 separate ones cuts the SD-card churn (and the heap allocation each
    // Storage.open() does for its file handle) by 4x during raw capture's highest-density moment.
    RawFrameCapture batch[kRawQueueCapacity];
    size_t batchCount = 0;
    while (batchCount < kRawQueueCapacity &&
           xQueueReceive(static_cast<QueueHandle_t>(rawQueue), &batch[batchCount], 0) == pdTRUE) {
      batchCount++;
    }
    if (batchCount > 0) rawCallback(batch, batchCount);
  }
}

// Runs in the WiFi driver's task context (not an ISR, but not our app task either) — keep this
// fast, do no logging/allocation, and hand off via the queue for tick() to process.
void WifiSniffer::promiscuousRxCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!wifiSniffer.rxQueue) return;

  const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = pkt->payload;
  const size_t len = pkt->rx_ctrl.sig_len;
  if (len < sizeof(Ieee80211Hdr)) return;

  wifiSniffer.totalFrames++;

  const auto* hdr = reinterpret_cast<const Ieee80211Hdr*>(payload);
  const uint8_t fType = frameType(hdr->frameControl);

  WifiObservation obs;
  obs.rssi = pkt->rx_ctrl.rssi;
  obs.channel = pkt->rx_ctrl.channel;
  obs.transmitter = toMac(hdr->addr2);
  obs.bssid = toMac(hdr->addr3);

  bool matched = false;

  if (type == WIFI_PKT_MGMT && fType == FC_TYPE_MGMT) {
    const uint8_t subtype = frameSubtype(hdr->frameControl);
    if (subtype == FC_SUBTYPE_BEACON || subtype == FC_SUBTYPE_PROBE_RESP) {
      obs.kind = subtype == FC_SUBTYPE_BEACON ? WifiFrameKind::Beacon : WifiFrameKind::ProbeResponse;
      // Beacon fixed fields: timestamp(8) + interval(2) + capabilities(2) = 12 bytes before IEs.
      // Probe response has the same fixed-field layout.
      constexpr size_t kFixedFieldsLen = 12;
      if (len > sizeof(Ieee80211Hdr) + kFixedFieldsLen) {
        const uint8_t* ies = payload + sizeof(Ieee80211Hdr) + kFixedFieldsLen;
        const size_t iesLen = len - sizeof(Ieee80211Hdr) - kFixedFieldsLen;
        extractSsid(ies, iesLen, obs.ssid, &obs.ssidLen);
      }
      matched = true;
    } else if (subtype == FC_SUBTYPE_PROBE_REQ) {
      obs.kind = WifiFrameKind::ProbeRequest;
      if (len > sizeof(Ieee80211Hdr)) {
        const uint8_t* ies = payload + sizeof(Ieee80211Hdr);
        const size_t iesLen = len - sizeof(Ieee80211Hdr);
        extractSsid(ies, iesLen, obs.ssid, &obs.ssidLen);
      }
      matched = true;
    }
  } else if (type == WIFI_PKT_DATA && fType == FC_TYPE_DATA) {
    // QoS data frames insert a 2-byte QoS Control field between the 802.11 header and the LLC
    // payload; the subtype's low bit (0x08) marks QoS.
    const uint8_t subtype = frameSubtype(hdr->frameControl);
    size_t llcOffset = sizeof(Ieee80211Hdr);
    if (subtype & 0x08) llcOffset += 2;
    if (len >= llcOffset + sizeof(kLlcSnapEapol) &&
        memcmp(payload + llcOffset, kLlcSnapEapol, sizeof(kLlcSnapEapol)) == 0) {
      const uint8_t* eapol = payload + llcOffset + sizeof(kLlcSnapEapol);
      const size_t eapolLen = len - llcOffset - sizeof(kLlcSnapEapol);
      // EAPOL header is 4 bytes (version, type, length); the Key frame body follows.
      if (eapolLen > 4 && eapol[1] == 3 /* EAPOL-Key */) {
        obs.eapolMessageNum = classifyEapolMessage(eapol + 4, eapolLen - 4);
        if (obs.eapolMessageNum != 0) {
          obs.kind = WifiFrameKind::EapolHandshake;
          // Infrastructure BSS data frame addressing: BSSID sits at addr1 (STA->AP, ToDS) or
          // addr2 (AP->STA, FromDS) — not addr3, which is what the default assignment above
          // guessed. EAPOL M1/M3 come from the AP (FromDS); M2/M4 come from the STA (ToDS).
          if (frameToDS(hdr->frameControl) && !frameFromDS(hdr->frameControl)) {
            obs.bssid = toMac(hdr->addr1);
          } else if (frameFromDS(hdr->frameControl) && !frameToDS(hdr->frameControl)) {
            obs.bssid = toMac(hdr->addr2);
          }
          matched = true;
        }
      }
    }
  }

  if (!matched) return;

  // Raw capture: only ever runs when the owner has explicitly opted in (see class comment /
  // setRawCaptureEnabled). Captures every EAPOL handshake frame verbatim, plus the first
  // SSID-bearing beacon/probe-response seen for each BSSID that has one, so the resulting .pcap
  // carries enough context for hcxpcapngtool/hashcat to identify the network.
  if (wifiSniffer.rawCaptureEnabled && wifiSniffer.rawQueue) {
    const uint32_t bssidHash = obs.bssid.fnv1a();
    bool captureThisFrame = false;

    if (obs.kind == WifiFrameKind::EapolHandshake) {
      int matchSlot = -1;
      int freeSlot = -1;
      for (size_t i = 0; i < kEapolBssidTrackCapacity; i++) {
        EapolBssidTrack& track = wifiSniffer.eapolBssidTracks[i];
        if (track.active && track.bssidHash == bssidHash) {
          matchSlot = static_cast<int>(i);
          break;
        }
        if (freeSlot < 0 && !track.active) freeSlot = static_cast<int>(i);
      }
      if (matchSlot < 0) {
        // Not tracked yet — claim a free slot, or evict slot 0 if all are in use.
        const int slot = (freeSlot >= 0) ? freeSlot : 0;
        wifiSniffer.eapolBssidTracks[slot] = EapolBssidTrack{bssidHash, true, false};
      }
      captureThisFrame = true;
    } else if (obs.kind == WifiFrameKind::Beacon || obs.kind == WifiFrameKind::ProbeResponse) {
      for (size_t i = 0; i < kEapolBssidTrackCapacity; i++) {
        EapolBssidTrack& track = wifiSniffer.eapolBssidTracks[i];
        if (track.active && track.bssidHash == bssidHash && !track.ssidCaptured) {
          track.ssidCaptured = true;
          captureThisFrame = true;
          break;
        }
      }
    }

    if (captureThisFrame) {
      RawFrameCapture frame;
      frame.len = static_cast<uint16_t>(len > kRawFrameMaxLen ? kRawFrameMaxLen : len);
      memcpy(frame.bytes, payload, frame.len);
      frame.unixTime = static_cast<uint32_t>(time(nullptr));
      if (xQueueSend(static_cast<QueueHandle_t>(wifiSniffer.rawQueue), &frame, 0) != pdTRUE) {
        wifiSniffer.droppedFrames++;  // reuses the existing drop counter; overflow here is rare
      }
    }
  }

  BaseType_t queued = xQueueSend(static_cast<QueueHandle_t>(wifiSniffer.rxQueue), &obs, 0);
  if (queued != pdTRUE) {
    wifiSniffer.droppedFrames++;
  }
}
