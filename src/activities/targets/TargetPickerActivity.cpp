#include "TargetPickerActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdio>

#include "ApScanCache.h"
#include "fontIds.h"
#include "ui/Chrome.h"

namespace {
constexpr unsigned long kRefreshIntervalMs = 2000;

void formatMac(const MacAddress& mac, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac.bytes[0], mac.bytes[1], mac.bytes[2], mac.bytes[3],
           mac.bytes[4], mac.bytes[5]);
}

// Builds an RSSI-descending index list over apScanCache's current entries — cheap to redo on
// every render given the cache tops out at 32 entries and this screen redraws at most every 2s.
size_t sortByRssi(uint8_t* indices, size_t maxCount) {
  const size_t total = std::min(apScanCache.count(), maxCount);
  for (size_t i = 0; i < total; i++) indices[i] = static_cast<uint8_t>(i);
  std::sort(indices, indices + total,
            [](uint8_t a, uint8_t b) { return apScanCache.at(a).rssi > apScanCache.at(b).rssi; });
  return total;
}
}  // namespace

void TargetPickerActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  lastRenderMs = 0;
  requestUpdate();
}

void TargetPickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const size_t total = apScanCache.count();
  if (total > 0) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selected = (selected == 0) ? total - 1 : selected - 1;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selected = (selected + 1) % total;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && selected < total) {
      uint8_t indices[ApScanCache::kCapacity];
      const size_t count = sortByRssi(indices, ApScanCache::kCapacity);
      if (selected < count) {
        const auto& entry = apScanCache.at(indices[selected]);
        if (!targetList.remove(kind, entry.bssid)) {
          targetList.add(kind, entry.bssid);
        }
        requestUpdate();
      }
      return;
    }
  }

  if (millis() - lastRenderMs >= kRefreshIntervalMs) {
    requestUpdate();
  }
}

void TargetPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, kind == TargetListKind::Whitelist ? "WHITELIST" : "BLACKLIST");

  const size_t total = apScanCache.count();
  int y = Chrome::contentTop() + 4;

  if (total == 0) {
    renderer.drawCenteredText(FONT_UI_10_ID, renderer.getScreenHeight() / 2, "No networks seen yet.");
  } else {
    uint8_t indices[ApScanCache::kCapacity];
    const size_t count = sortByRssi(indices, ApScanCache::kCapacity);
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);

    for (size_t i = 0; i < count; i++) {
      const auto& entry = apScanCache.at(indices[i]);
      const int entryTop = y - 3;
      if (i == selected) {
        Chrome::drawSelectionHighlight(renderer, Chrome::contentLeft() - 4, entryTop,
                                       Chrome::contentRight(renderer) - Chrome::contentLeft() + 8,
                                       2 * lineHeight + 2 + 6);
      }

      const bool listed = targetList.contains(kind, entry.bssid);
      char line1[40];
      snprintf(line1, sizeof(line1), "%s%s", entry.ssidLen > 0 ? entry.ssid : "(hidden)", listed ? "  [+]" : "");
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line1);
      y += lineHeight + 2;

      char macBuf[18];
      formatMac(entry.bssid, macBuf);
      char line2[48];
      snprintf(line2, sizeof(line2), "  %s  ch%u  %d dBm", macBuf, entry.channel, entry.rssi);
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line2);
      y += lineHeight + 6;
    }
  }

  Chrome::drawFooterHints(renderer, "Back", "Toggle", nullptr, nullptr);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  lastRenderMs = millis();
}
