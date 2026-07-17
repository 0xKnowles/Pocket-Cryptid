#include "DeviceListActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <cstddef>
#include <cstdio>

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
  int y = Chrome::contentTop() + 4;

  if (total == 0) {
    renderer.drawCenteredText(FONT_UI_10_ID, renderer.getScreenHeight() / 2, "Nothing heard yet.");
  } else {
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    for (size_t i = 0; i < total; i++) {
      const auto& entry = recentSightings.at(i);
      char macBuf[18];
      formatMac(entry.mac, macBuf);
      char agoBuf[16];
      formatAgo(entry.seenAtMs, agoBuf, sizeof(agoBuf));

      char line1[48];
      snprintf(line1, sizeof(line1), "%-6s %s  %d dBm", logRecordTypeShortName(entry.type), macBuf, entry.rssi);
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line1);
      y += lineHeight + 2;

      // Fall back to a vendor-name lookup (see VendorOui.h — opt-in, needs /.ruby/oui.txt on the
      // SD card) when there's no advertised label at all, rather than just "(no name)".
      const char* label = entry.label;
      char vendorBuf[32];
      if (!label[0] && lookupVendorOui(entry.mac, vendorBuf, sizeof(vendorBuf))) {
        label = vendorBuf;
      }
      char line2[64];
      snprintf(line2, sizeof(line2), "  %s - %s", label[0] ? label : "(no name)", agoBuf);
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line2);
      y += lineHeight + 6;
    }
  }

  Chrome::drawFooterHints(renderer, "Home", nullptr, nullptr, nullptr);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  lastRenderMs = millis();
}
