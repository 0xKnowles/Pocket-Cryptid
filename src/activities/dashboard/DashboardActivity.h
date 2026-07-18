#pragma once

#include "activities/Activity.h"

// Home screen. Landscape (792x528 logical, the panel's native orientation — see main.cpp's
// setupDisplayAndFonts()). Button *behavior* doesn't depend on screen orientation at all —
// MappedInputManager maps straight to hardware regardless of what's on screen — so Confirm opens
// Settings, Right opens Maintenance, Left toggles CaptureControl's pause (same button
// pauses/resumes, no screen change), and Up/Down still open DeviceListActivity (live recent
// sightings) and LogViewerActivity (on-device decrypt of the encrypted capture log) respectively;
// only the layout below is orientation-specific.
//
// Two update cadences share one framebuffer:
//   - A slow (~5s) full redraw of the header + specimen card + RF stat rows + footer, using
//     FAST_REFRESH.
//   - A faster (~1.2s) redraw of *only* Ruby's fixed box, also FAST_REFRESH.
// Because the box-only tick never calls clearScreen() or touches any pixel outside its box, the
// panel's differential refresh only actually changes that box on screen — everything else stays
// visually static between the slower full redraws even though both paths share the same refresh
// mode. See RubySpriteRenderer.h for more on why this is the "partial refresh" here.
class DashboardActivity final : public Activity {
 public:
  explicit DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Dashboard", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void renderFull();
  void renderRubyBoxOnly();
  void drawRubyPanel(bool withNoise);
  void armSpeechBubble(const char* text);

  enum class RenderKind { Full, RubyOnly };
  RenderKind pendingRenderKind = RenderKind::Full;

  unsigned long lastFullRenderMs = 0;
  unsigned long lastPetRenderMs = 0;
  unsigned long lastPeriodicFullRefreshMs = 0;
  uint8_t lastAnimFrameRendered = 0xFF;
  bool handshakeBannerActive = false;
  unsigned long handshakeBannerUntilMs = 0;
  bool levelUpBannerActive = false;
  unsigned long levelUpBannerUntilMs = 0;
  uint8_t levelUpBannerLevel = 0;  // level just reached, for the banner's "LEVEL UP -> Lv.N" text
  // DeauthDetector::alertActive() is itself already time-windowed, so no separate until-timestamp
  // is needed here — just the last-known state, to notice the on/off transition and redraw.
  bool lastDeauthAlertState = false;

  // Ruby's one-shot speech-bubble reaction (see RubySpriteRenderer::draw's reactionText param) —
  // same timed on/off shape as the banners above, but a fixed string literal from RubyThoughts
  // rather than a formatted buffer, so no backing char array is needed here. Falls back to the
  // ambient mood-driven thought bubble whenever this isn't active.
  bool speechBubbleActive = false;
  unsigned long speechBubbleUntilMs = 0;
  const char* speechBubbleText = nullptr;
  // Last-seen SignalCatalog unique counts, so a fresh sighting (this tick's count > last tick's)
  // can fire a "new device" reaction exactly once per increase rather than every redraw it's still
  // true. Seeded from the live counts in onEnter() so pre-existing sightings from before this
  // screen was entered don't spuriously fire a reaction on the very first render.
  uint32_t lastUniqueWifiAPs = 0;
  uint32_t lastUniqueWifiClients = 0;
  uint32_t lastUniqueBleDevices = 0;

  // Dedicated, session-only ring of handshake-capture timestamps (millis() at capture) — separate
  // from RecentSightings' shared 16-slot feed, which mixes in every AP/client/BLE sighting too and
  // so pushes a handshake out of view again within moments in any normal RF environment. This ring
  // is handshake-only, so it stays genuinely historical across the whole session instead of just
  // the last few seconds — see drawHandshakeHistoryChart() in DashboardActivity.cpp.
  static constexpr size_t kHandshakeHistoryCapacity = 24;
  unsigned long handshakeHistoryTimes[kHandshakeHistoryCapacity] = {};
  size_t handshakeHistoryCount = 0;
  size_t handshakeHistoryNext = 0;

  int rubyBoxX = 0;
  int rubyBoxY = 0;
  int rubyBoxSize = 0;
};
