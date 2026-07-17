#include "DeviceListActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <cstddef>
#include <cstdio>

#include "RecentSightings.h"
#include "TargetList.h"
#include "fontIds.h"
#include "ui/Chrome.h"

namespace {
constexpr unsigned long kRefreshIntervalMs = 2000;

void formatMac(const MacAddress& mac, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac.bytes[0], mac.bytes[1], mac.bytes[2], mac.bytes[3],
           mac.bytes[4], mac.bytes[5]);
}

void formatAgo(unsigned long seenAtMs, char* out, size_t outSize) {
  const unsigned long ageSec = (millis() - seenAtMs) / 1000;
  if (ageSec < 60) {
    snprintf(out, outSize, "%lus ago", ageSec);
  } else {
    snprintf(out, outSize, "%lum ago", ageSec / 60);
  }
}
}  // namespace

void DeviceListActivity::onEnter() {
  Activity::onEnter();
  lastRenderMs = 0;
  selected = 0;
  requestUpdate();
}

void DeviceListActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const size_t total = recentSightings.count();
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
      const auto& entry = recentSightings.at(selected);
      if (entry.type == LogRecordType::WifiAp) {
        if (!targetList.remove(entry.mac)) {
          targetList.add(entry.mac);
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

void DeviceListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, "DEVICE LOG");

  const size_t total = recentSightings.count();
  int y = Chrome::contentTop() + 4;

  if (total == 0) {
    renderer.drawCenteredText(FONT_UI_10_ID, renderer.getScreenHeight() / 2, "Nothing heard yet.");
  } else {
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (size_t i = 0; i < total; i++) {
      const auto& entry = recentSightings.at(i);
      const int entryTop = y - 3;
      if (i == selected) {
        Chrome::drawSelectionHighlight(renderer, Chrome::contentLeft() - 4, entryTop,
                                       Chrome::contentRight(renderer) - Chrome::contentLeft() + 8,
                                       2 * lineHeight + 2 + 6);
      }

      char macBuf[18];
      formatMac(entry.mac, macBuf);
      char agoBuf[16];
      formatAgo(entry.seenAtMs, agoBuf, sizeof(agoBuf));
      const bool isTarget = entry.type == LogRecordType::WifiAp && targetList.contains(entry.mac);

      char line1[52];
      snprintf(line1, sizeof(line1), "%-6s %s  %d dBm%s", logRecordTypeShortName(entry.type), macBuf, entry.rssi,
                isTarget ? "  [T]" : "");
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line1);
      y += lineHeight + 2;

      char line2[56];
      snprintf(line2, sizeof(line2), "  %s - %s", entry.label[0] ? entry.label : "(no name)", agoBuf);
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line2);
      y += lineHeight + 6;
    }
  }

  Chrome::drawFooterHints(renderer, "Home", total > 0 ? "Target" : nullptr, nullptr, nullptr);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  lastRenderMs = millis();
}
