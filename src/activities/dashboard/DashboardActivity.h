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

  enum class RenderKind { Full, RubyOnly };
  RenderKind pendingRenderKind = RenderKind::Full;

  unsigned long lastFullRenderMs = 0;
  unsigned long lastPetRenderMs = 0;
  unsigned long lastPeriodicFullRefreshMs = 0;
  uint8_t lastAnimFrameRendered = 0xFF;
  bool handshakeBannerActive = false;
  unsigned long handshakeBannerUntilMs = 0;
  // DeauthDetector::alertActive() is itself already time-windowed, so no separate until-timestamp
  // is needed here — just the last-known state, to notice the on/off transition and redraw.
  bool lastDeauthAlertState = false;

  int rubyBoxX = 0;
  int rubyBoxY = 0;
  int rubyBoxSize = 0;
};
