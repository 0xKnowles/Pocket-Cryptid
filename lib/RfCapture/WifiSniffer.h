#pragma once

#include <esp_wifi_types.h>

#include <cstddef>
#include <cstdint>
#include <functional>

#include "RfTypes.h"

// Passive 802.11 monitor-mode capture.
//
// WifiSniffer puts the ESP32-C3's radio into promiscuous (monitor) mode via the ESP-IDF
// esp_wifi_* API — it never calls esp_wifi_connect()/associate, never transmits a frame, and
// never starts an AP. It only listens, and only extracts metadata that is broadcast in the
// clear by design (beacon/probe SSIDs, MAC addresses, the *presence* of an EAPOL handshake and
// which message number it is) — it does not attempt to crack, decrypt, or store handshake key
// material or payloads.
//
// The radio hardware can only listen to one channel at a time, so start() hops across the
// configured channel list on a timer (tick() must be called regularly from the main loop to
// drive the hop and to drain captured frames into the observation callback).
class WifiSniffer {
 public:
  using ObservationCallback = std::function<void(const WifiObservation&)>;

  // Default 2.4 GHz channel plan (1/6/11 are the non-overlapping US/EU channels; the rest catch
  // networks that ignore that convention).
  static constexpr uint8_t kAllChannels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  static constexpr size_t kAllChannelsCount = sizeof(kAllChannels);

  bool begin();
  void end();

  // Starts monitor mode hopping across `channels` (kAllChannelsCount entries by default),
  // spending `dwellMs` on each before moving to the next.
  bool start(const uint8_t* channels = kAllChannels, size_t channelCount = kAllChannelsCount,
             uint32_t dwellMs = 300);
  void stop();
  bool isRunning() const { return running; }

  // Drains captured frames and drives channel hopping. Call from the main loop; cheap/no-op
  // when nothing is queued and no hop is due.
  void tick();

  void setObservationCallback(ObservationCallback cb) { callback = std::move(cb); }

  uint8_t currentChannel() const { return channels[channelIndex]; }
  uint32_t framesSeen() const { return totalFrames; }
  uint32_t framesDropped() const { return droppedFrames; }  // queue overflow — capture rate exceeded processing rate

 private:
  static void promiscuousRxCallback(void* buf, wifi_promiscuous_pkt_type_t type);

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
};

extern WifiSniffer wifiSniffer;  // singleton, defined in WifiSniffer.cpp
