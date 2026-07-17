#pragma once

#include "activities/Activity.h"

// Home screen. Portrait (480x800 logical) — that's the orientation the X3/X4's physical
// Back/Confirm/Left/Right/Up/Down buttons are actually laid out for; a rotated landscape
// dashboard would need the orientation-aware button remapping layer this port doesn't carry
// (see main.cpp's setupDisplayAndFonts()). Confirm/Left/Right open Settings/Lore/Maintenance;
// Up/Down open DeviceListActivity (live recent sightings) and LogViewerActivity (on-device
// decrypt of the encrypted capture log) respectively.
//
// Two update cadences share one framebuffer:
//   - A slow (~5s) full redraw of the header + specimen card + RF stat rows + footer, using
//     FAST_REFRESH.
//   - A faster (~1.2s) redraw of *only* the cryptid's fixed box, also FAST_REFRESH.
// Because the box-only tick never calls clearScreen() or touches any pixel outside its box, the
// panel's differential refresh only actually changes that box on screen — everything else stays
// visually static between the slower full redraws even though both paths share the same refresh
// mode. See CryptidSpriteRenderer.h for more on why this is the "partial refresh" here.
class DashboardActivity final : public Activity {
 public:
  explicit DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Dashboard", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void renderFull();
  void renderCryptidBoxOnly();
  void drawCryptidPanel(bool withNoise);

  enum class RenderKind { Full, CryptidOnly };
  RenderKind pendingRenderKind = RenderKind::Full;

  unsigned long lastFullRenderMs = 0;
  unsigned long lastPetRenderMs = 0;
  unsigned long lastPeriodicFullRefreshMs = 0;
  uint8_t lastAnimFrameRendered = 0xFF;
  bool evolutionBannerActive = false;
  unsigned long evolutionBannerUntilMs = 0;

  int cryptidBoxX = 0;
  int cryptidBoxY = 0;
  int cryptidBoxSize = 0;
};
