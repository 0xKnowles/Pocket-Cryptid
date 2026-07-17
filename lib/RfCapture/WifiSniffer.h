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
//   - Active deauth (see DeauthEngine, RubySettings::activeDeauthEnabled) is meant to transmit
//     real 802.11 deauthentication frames to force a handshake instead of waiting for one, gated
//     by TargetList in addition to its own setting. **Currently non-functional**: it needs the
//     radio in WIFI_MODE_STA to get a TX-capable interface, but real X3 hardware testing showed
//     that mode starves the interrupt matrix badly enough to crash esp-aes (hardware crypto) on
//     every boot — see WifiSniffer::begin() and CHANGELOG. Reverted to WIFI_MODE_NULL; the code
//     path is left in place (it fails harmlessly with no interface up) pending a safe fix.
// Both exist for auditing the strength of networks the device's owner controls or is
// explicitly authorized to test.
//
// The radio hardware can only listen to one channel at a time, so start() hops across the
// configured channel list on a timer (tick() must be called regularly from the main loop to
// drive the hop and to drain captured frames into the observation callback).
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

 private:
  static void promiscuousRxCallback(void* buf, wifi_promiscuous_pkt_type_t type);

  static constexpr size_t kEapolBssidTrackCapacity = 8;
  struct EapolBssidTrack {
    uint32_t bssidHash = 0;
    bool active = false;
    bool ssidCaptured = false;
  };

  bool running = false;
  bool initialized = false;
  const uint8_t* channels = kAllChannels;
  size_t channelCount = kAllChannelsCount;
  size_t channelIndex = 0;
  uint32_t dwellMs = 300;
  unsigned long lastHopAt = 0;
  uint32_t totalFrames = 0;
  uint32_t droppedFrames = 0;

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
