#include "ApHistoryActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "ApHistory.h"
#include "fontIds.h"
#include "ui/Chrome.h"

namespace {
void formatMac(const MacAddress& mac, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac.bytes[0], mac.bytes[1], mac.bytes[2], mac.bytes[3],
           mac.bytes[4], mac.bytes[5]);
}

void formatAgoFromUnix(uint32_t thenUnix, char* out, size_t outSize) {
  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  if (thenUnix == 0 || now < thenUnix) {
    snprintf(out, outSize, "unknown");
    return;
  }
  const uint32_t ageSec = now - thenUnix;
  if (ageSec < 60) {
    snprintf(out, outSize, "%lus ago", static_cast<unsigned long>(ageSec));
  } else if (ageSec < 3600) {
    snprintf(out, outSize, "%lum ago", static_cast<unsigned long>(ageSec / 60));
  } else if (ageSec < 86400) {
    snprintf(out, outSize, "%luh ago", static_cast<unsigned long>(ageSec / 3600));
  } else {
    snprintf(out, outSize, "%lud ago", static_cast<unsigned long>(ageSec / 86400));
  }
}

// Builds a lastSeen-descending index list over ApHistory's current entries — cheap enough to
// redo on every render given this screen refreshes at most every few seconds.
size_t sortByRecency(uint8_t* indices, size_t maxCount) {
  const size_t total = std::min(apHistory.count(), maxCount);
  for (size_t i = 0; i < total; i++) indices[i] = static_cast<uint8_t>(i);
  std::sort(indices, indices + total,
            [](uint8_t a, uint8_t b) { return apHistory.at(a).lastSeenUnix > apHistory.at(b).lastSeenUnix; });
  return total;
}
}  // namespace

void ApHistoryActivity::onEnter() {
  Activity::onEnter();
  pageStart = 0;
  lastRenderMs = 0;
  requestUpdate();
}

void ApHistoryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const size_t total = apHistory.count();
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (pageStart >= kPageSize) {
      pageStart -= kPageSize;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (pageStart + kPageSize < total) {
      pageStart += kPageSize;
      requestUpdate();
    }
    return;
  }

  constexpr unsigned long kRefreshIntervalMs = 5000;
  if (millis() - lastRenderMs >= kRefreshIntervalMs) {
    requestUpdate();
  }
}

void ApHistoryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, "AP HISTORY");

  uint8_t indices[ApHistory::kCapacity];
  const size_t total = sortByRecency(indices, ApHistory::kCapacity);

  int y = Chrome::contentTop() + 4;

  if (total == 0) {
    renderer.drawCenteredText(FONT_UI_10_ID, renderer.getScreenHeight() / 2, "No history yet.");
  } else {
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    const size_t pageEnd = std::min(pageStart + kPageSize, total);
    for (size_t i = pageStart; i < pageEnd; i++) {
      const auto& entry = apHistory.at(indices[i]);
      char macBuf[18];
      formatMac(entry.bssid, macBuf);
      char agoBuf[16];
      formatAgoFromUnix(entry.lastSeenUnix, agoBuf, sizeof(agoBuf));

      char line1[48];
      snprintf(line1, sizeof(line1), "%s", entry.ssid[0] ? entry.ssid : "(hidden)");
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line1, true, EpdFontFamily::BOLD);
      y += lineHeight;

      char line2[64];
      snprintf(line2, sizeof(line2), "  %s  seen %lux - last %s", macBuf, static_cast<unsigned long>(entry.sightings),
               agoBuf);
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line2);
      y += lineHeight + 6;
    }

    char pageBuf[32];
    snprintf(pageBuf, sizeof(pageBuf), "%u-%u of %u", static_cast<unsigned>(pageStart + 1),
             static_cast<unsigned>(pageEnd), static_cast<unsigned>(total));
    renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), Chrome::contentBottom(renderer) - lineHeight, pageBuf);
  }

  Chrome::drawFooterHints(renderer, "Back", nullptr, nullptr, nullptr);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  lastRenderMs = millis();
}
