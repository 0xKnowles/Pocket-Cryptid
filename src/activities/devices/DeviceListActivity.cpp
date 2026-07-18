#include "DeviceListActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

#include "LogRecord.h"
#include "RecentSightings.h"
#include "VendorOui.h"
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
  firstRenderSinceEnter = true;
  requestUpdate();
}

void DeviceListActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  if (millis() - lastRenderMs >= kRefreshIntervalMs) {
    requestUpdate();
  }
}

void DeviceListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, "DEVICE LOG");

  const size_t total = recentSightings.count();
  const int top = Chrome::contentTop();

  if (total == 0) {
    renderer.drawCenteredText(FONT_UI_10_ID, renderer.getScreenHeight() / 2, "Nothing heard yet.");
  } else {
    // Same tight grid as Dashboard's RECENT DEVICES card (see DashboardActivity.cpp) — this
    // screen is the expanded view of that same feed, so it should show every one of the ring
    // buffer's 16 entries at once (per this file's own header comment), not run most of them off
    // the bottom of the screen the way a single wide-spaced column did. A third line adds the
    // label back in, since that's the one extra bit of detail this "expanded" view has room for
    // that Dashboard's narrower card doesn't.
    constexpr int kLineHeight = 13;
    constexpr int kLineGap = 2;
    constexpr int kEntryGap = 6;
    constexpr int kEntryHeight = kLineHeight * 3 + kLineGap * 2 + kEntryGap;
    constexpr int kEntryColWidth = 224;
    constexpr int kColumnGap = 14;
    const int left = Chrome::contentLeft();
    const int rowsPerColumn = std::max(1, (Chrome::contentBottom(renderer) - top) / kEntryHeight);
    const int columnCount =
        std::max(1, (Chrome::contentRight(renderer) - left) / (kEntryColWidth + kColumnGap));
    const size_t maxVisible = std::min(total, static_cast<size_t>(rowsPerColumn * columnCount));

    for (size_t i = 0; i < maxVisible; i++) {
      const size_t col = i / static_cast<size_t>(rowsPerColumn);
      const size_t row = i % static_cast<size_t>(rowsPerColumn);
      const int entryX = left + static_cast<int>(col) * (kEntryColWidth + kColumnGap);
      const int entryY = top + static_cast<int>(row) * kEntryHeight;

      const auto& entry = recentSightings.at(i);
      char macBuf[18];
      formatMac(entry.mac, macBuf);
      char agoBuf[16];
      formatAgo(entry.seenAtMs, agoBuf, sizeof(agoBuf));

      char line1[32];
      snprintf(line1, sizeof(line1), "%s %s", logRecordTypeCompactName(entry.type), macBuf);
      renderer.drawText(FONT_SMALL_ID, entryX, entryY, line1);

      const int line2Y = entryY + kLineHeight + kLineGap;
      Chrome::drawSignalBars(renderer, entryX, line2Y, entry.rssi);
      char line2[32];
      snprintf(line2, sizeof(line2), "%d dBm  %s", entry.rssi, agoBuf);
      renderer.drawText(FONT_SMALL_ID, entryX + Chrome::kSignalBarsWidth + 4, line2Y, line2);

      // Fall back to a vendor-name lookup (see VendorOui.h — opt-in, needs /.ruby/oui.txt on the
      // SD card) when there's no advertised label at all, rather than just "(no name)".
      const char* label = entry.label;
      char vendorBuf[32];
      if (!label[0] && lookupVendorOui(entry.mac, vendorBuf, sizeof(vendorBuf))) {
        label = vendorBuf;
      }
      const std::string line3 =
          renderer.truncatedText(FONT_SMALL_ID, label[0] ? label : "(no name)", kEntryColWidth);
      renderer.drawText(FONT_SMALL_ID, entryX, entryY + (kLineHeight + kLineGap) * 2, line3.c_str());
    }
  }

  Chrome::drawFooterHints(renderer, "Home", nullptr, nullptr, nullptr);
  renderer.displayBuffer(firstRenderSinceEnter ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  firstRenderSinceEnter = false;
  lastRenderMs = millis();
}
