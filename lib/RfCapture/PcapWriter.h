#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "RfTypes.h"

// Writes standard libpcap (.pcap) files of raw 802.11 frames — link-layer type 105
// (DLT_IEEE802_11), no radiotap header. This is the one capture path in Ruby that writes
// plaintext to SD: a WPA handshake is only crackable by tools like hashcat/hcxpcapngtool if the
// frame bytes are exactly what was sent over the air, so it can't be routed through
// EncryptedLog's AES-256-GCM envelope the way every other observation is. Feeding is gated
// behind RubySettings::rawHandshakeCaptureEnabled (off by default) — see WifiSniffer for what
// gets captured and SettingsActivity for the on-screen warning.
class PcapWriter {
 public:
  bool begin();

  // Appends `count` raw frames as pcap records to today's file in a single open/write/close —
  // batched rather than one file-handle cycle per frame, since a real handshake's rapid 4-frame
  // burst was a meaningful contributor to the SD-churn-driven heap fragmentation that caused
  // real crashes during extended unattended capture (see CHANGELOG). Frames with len == 0 are
  // skipped.
  bool writeFrames(const RawFrameCapture* frames, size_t count);

  // Call periodically from the main loop: rotates to a new day's file when the date changes.
  void tick();

  // Directory capture files live in, e.g. for MaintenanceActivity to summarize.
  static const char* captureDirectory();

 private:
  bool openTodaysFile();

  std::string currentFilePath;
  int32_t currentFileDay = -1;  // day-of-epoch the open file was rotated for; -1 = none open
  bool ready = false;
};

extern PcapWriter pcapWriter;  // singleton, defined in PcapWriter.cpp
