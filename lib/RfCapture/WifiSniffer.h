#pragma once

#include <esp_wifi_types.h>

#include <cstddef>
#include <cstdint>
#include <functional>

#include "RfTypes.h"

// 802.11 monitor-mode capture, passive by default.
//
// WifiSniffer puts the ESP32-C3's radio into promiscuous (monitor) mode via the ESP-IDF
// esp_wifi_* API — it never calls esp_wifi_connect()/associate, and never starts an AP. By
// default it only extracts metadata that is broadcast in the clear by design (beacon/probe
// SSIDs, MAC addresses, the *presence* of an EAPOL handshake and which message number it is)
// — it does not crack, decrypt, or store handshake key material or payloads, and transmits
// nothing.
//
// Two explicit, off-by-default opt-ins layer on top of that baseline:
//   - Raw frame capture (see setRawCaptureEnabled/setRawFrameCallback) preserves verbatim
//     EAPOL handshake bytes (ANonce/SNonce/MIC included) plus the SSID-bearing beacon for each
//     BSSID, for export as a standard .pcap that offline tools like hashcat/hcxpcapngtool can
//     attempt to crack. See PcapWriter and SettingsActivity.
//   - Active deauth (see DeauthEngine, RubySettings::activeDeauthEnabled) transmits real 802.11
//     deauthentication frames to force a handshake instead of waiting for one, gated by
//     TargetList in addition to its own setting. Needs the radio in WIFI_MODE_STA to get a
//     TX-capable interface — real X3 hardware testing showed bringing STA mode up for the whole
//     session crashed esp-aes (hardware crypto) via interrupt starvation on every boot (see
//     CHANGELOG), so WifiSniffer itself stays in WIFI_MODE_NULL as its resting state; DeauthEngine
//     now switches to STA transiently, only for the duration of one deauth burst, then reverts —
//     see DeauthEngine.h's class comment for the full reasoning and its still-pending
//     real-hardware confirmation.
// Both exist for auditing the strength of networks the device's owner controls or is
// explicitly authorized to test.
//
// The radio hardware can only listen to one channel at a time, so start() hops across the
// configured channel list on a timer (tick() must be called regularly from the main loop to
// drive the hop and to drain captured frames into the observation callback). That hop is briefly
// suspended whenever an EAPOL frame is seen, so an in-progress handshake isn't orphaned mid-hop —
// see kHandshakeChannelLockMs.
class WifiSniffer {
 public:
  using ObservationCallback = std::function<void(const WifiObservation&)>;
  // Batch, not single-frame: tick() hands over everything drained from the raw queue in one call
  // rather than one call per frame, so a handshake's rapid burst of captures can be written to SD
  // in a single open/write-many/close cycle instead of one cycle per frame — see PcapWriter.
  using RawFrameCallback = std::function<void(const RawFrameCapture* frames, size_t count)>;

  // Default 2.4 GHz channel plan (1/6/11 are the non-overlapping US/EU channels; the rest catch
  // networks that ignore that convention).
  static constexpr uint8_t kAllChannels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  static constexpr size_t kAllChannelsCount = sizeof(kAllChannels);

  // Translates RubySettings::wifiChannelScope (0 = hop all 13 channels; 1-13 = lock onto just
  // that one, skipping the hop entirely — see tick()'s `channelCount > 1` guard) into a
  // channels/count pair start() can take directly. Locking to a single known channel raises its
  // duty cycle from ~1/13 to 100%, which matters a lot for catching a handshake that completes in
  // well under a second — see README's capture-rate discussion. The single-channel case needs
  // storage that outlives the call, since start() only ever stores the pointer it's given rather
  // than copying; this owns that storage (a function-local static, safe here since only one
  // channel plan is ever in effect at a time).
  static void channelPlanFor(uint8_t scope, const uint8_t*& outChannels, size_t& outCount);

  bool begin();
  void end();

  // Starts monitor mode hopping across `channels` (kAllChannelsCount entries by default),
  // spending `dwellMs` on each before moving to the next. `captureRawFrames` mirrors
  // RubySettings::rawHandshakeCaptureEnabled — see setRawCaptureEnabled.
  bool start(const uint8_t* channels = kAllChannels, size_t channelCount = kAllChannelsCount,
             uint32_t dwellMs = 300, bool captureRawFrames = false);
  void stop();
  bool isRunning() const { return running; }

  // Drains captured frames and drives channel hopping. Call from the main loop; cheap/no-op
  // when nothing is queued and no hop is due.
  void tick();

  void setObservationCallback(ObservationCallback cb) { callback = std::move(cb); }
  void setRawFrameCallback(RawFrameCallback cb) { rawCallback = std::move(cb); }

  // Turns raw-frame capture on/off at runtime (called both from start() and live from
  // SettingsActivity when the owner flips the toggle). The backing queue is allocated lazily on
  // first enable and, once allocated, kept for the rest of the boot — deleting a FreeRTOS queue
  // out from under the promiscuous callback (a different task) is a use-after-free hazard, and
  // this queue is small (a handful of frames' worth) so there's little to gain from reclaiming
  // it on disable.
  void setRawCaptureEnabled(bool enabled);
  bool isRawCaptureEnabled() const { return rawCaptureEnabled; }

  uint8_t currentChannel() const { return channels[channelIndex]; }
  uint32_t framesSeen() const { return totalFrames; }
  uint32_t framesDropped() const { return droppedFrames; }  // queue overflow — capture rate exceeded processing rate

  // Count of raw-captured EAPOL message-1 frames whose Key Data field carries the vendor-specific
  // PMKID KDE (OUI 00:0F:AC, type 4) — hashcat's -m 22000 PMKID mode can recover a PSK straight
  // from one of these, without ever needing the rest of the 4-way handshake to complete. Only
  // incremented for frames actually written to raw .pcap (see setRawCaptureEnabled) — purely
  // informational, doesn't change what's captured either way.
  uint32_t pmkidCapableFrames() const { return totalPmkidCapableFrames; }

 private:
  static void promiscuousRxCallback(void* buf, wifi_promiscuous_pkt_type_t type);

  static constexpr size_t kEapolBssidTrackCapacity = 8;
  struct EapolBssidTrack {
    uint32_t bssidHash = 0;
    bool active = false;
    bool ssidCaptured = false;
  };

  // How long to hold the current channel once an EAPOL frame is seen, instead of hopping on the
  // normal dwellMs cadence. A full 4-way handshake completes in tens of milliseconds, but with a
  // 300ms dwell it's easy for the hop timer to fire between the AP's M1 and the client's M2 —
  // this held the channel just long enough for the AP to give up and retransmit M1 into a channel
  // Ruby had already left, over and over, without ever seeing a reply. 3s comfortably covers a
  // handshake plus a retry or two. Set/extended from promiscuousRxCallback each time any EAPOL
  // frame arrives (any BSSID — the radio can only be on one channel regardless), so a slow
  // multi-retry handshake keeps the lock alive rather than only the first message extending it.
  static constexpr uint32_t kHandshakeChannelLockMs = 3000;

  bool running = false;
  bool initialized = false;
  const uint8_t* channels = kAllChannels;
  size_t channelCount = kAllChannelsCount;
  size_t channelIndex = 0;
  uint32_t dwellMs = 300;
  unsigned long lastHopAt = 0;
  // Written from promiscuousRxCallback (WiFi driver task), read from tick() (main loop) — a plain
  // unsigned long is naturally aligned and single-writer here, so this doesn't need a mutex/queue
  // the way actual frame data does.
  volatile unsigned long channelLockUntilMs = 0;
  uint32_t totalFrames = 0;
  uint32_t droppedFrames = 0;
  uint32_t totalPmkidCapableFrames = 0;

  ObservationCallback callback;
  void* rxQueue = nullptr;  // QueueHandle_t, opaque here to keep FreeRTOS headers out of this .h

  RawFrameCallback rawCallback;
  void* rawQueue = nullptr;  // QueueHandle_t of RawFrameCapture; null until first enabled
  bool rawCaptureEnabled = false;
  // Which BSSIDs currently have an in-progress/completed handshake worth capturing a raw EAPOL
  // frame for, and whether we've already grabbed their SSID-bearing beacon. Tiny, fixed-size,
  // and only meaningfully populated while raw capture is on — see the class comment.
  EapolBssidTrack eapolBssidTracks[kEapolBssidTrackCapacity] = {};
};

extern WifiSniffer wifiSniffer;  // singleton, defined in WifiSniffer.cpp
