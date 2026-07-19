#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"

// USB pull for .pclog files without removing the SD card. Reachable from Maintenance (Down).
// Speaks the framed protocol in UsbTransferProtocol.h over the same USB-CDC port normal debug
// logging already uses — see onEnter()/onExit() for how it keeps that logging from corrupting the
// wire while this screen owns it, and for how it pauses WiFi/BLE capture (same mechanism as
// Dashboard's own Pause) so the log file isn't growing mid-transfer and nothing else is competing
// for the SD card or CPU with it.
//
// The device never initiates anything here; it only ever responds to a host that already sent a
// well-formed request, and the whole channel only exists while a user has physically navigated
// into this screen. See MaintenanceActivity.h for the stealth tradeoff this makes against the
// project's normal "nothing but the SD card leaves this device" posture.
class UsbTransferActivity final : public Activity {
 public:
  explicit UsbTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UsbTransfer", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Services at most one incoming frame per call — loop() calls this every iteration, same as
  // every other Activity's non-blocking loop(). A kOpGet's file body is streamed synchronously
  // once the header is parsed (see .cpp), so that single call can take longer than the others, but
  // still bounded by file size rather than blocking on network conditions.
  void serviceProtocol();

  void handlePing();
  void handleList();
  void handleGet(uint32_t filenameLen);
  void handleKey();

  void sendHeader(uint8_t opcode, uint32_t payloadLen);
  void sendFrame(uint8_t opcode, const uint8_t* payload, uint32_t payloadLen);
  void sendError(const char* message);

  std::string lastStatus = "Waiting for host...";
  uint32_t framesServed = 0;

  // Whether onEnter() is the one that paused capture (vs. it already being paused when this
  // screen was opened) — onExit() only resumes it in the former case, so a capture the owner had
  // already manually paused before coming here stays paused afterward too, same as
  // toggleCapturePause()'s own resume behavior respecting prior settings rather than forcing state.
  bool pausedCaptureOnEnter = false;
};
