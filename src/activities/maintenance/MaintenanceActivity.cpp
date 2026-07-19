#include "MaintenanceActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>

#include "DeauthEngine.h"
#include "EncryptedLog.h"
#include "PcapWriter.h"
#include "WifiSniffer.h"
#include "fontIds.h"
#include "ui/Chrome.h"

namespace {
constexpr char kLogDir[] = "/.ruby/log";

// Small local duplicate of DashboardActivity's card-drawing pattern (title + rule, rounded
// border once row count is known) rather than promoting it to Chrome — this screen is the only
// other user, and the two callers' row counts/column widths differ enough that a shared helper
// would need as many parameters as just keeping two small copies.
constexpr int kCardOutsetX = 6;
constexpr int kCardTopPad = 6;
constexpr int kCardTitleGap = 6;
constexpr int kCardBottomPad = 8;
constexpr int kCardGap = 10;
constexpr int kColumnGap = 14;

int beginCard(const GfxRenderer& renderer, int x, int width, int y, const char* title) {
  renderer.drawText(FONT_SMALL_ID, x, y + kCardTopPad, title, true, EpdFontFamily::BOLD);
  const int ruleY = y + kCardTopPad + renderer.getLineHeight(FONT_SMALL_ID) + 2;
  renderer.drawLine(x, ruleY, x + width, ruleY, true);
  return ruleY + kCardTitleGap;
}

void endCard(const GfxRenderer& renderer, int x, int width, int cardTop, int rowsEndY) {
  const int rectX = x - kCardOutsetX;
  const int rectWidth = width + kCardOutsetX * 2;
  const int height = (rowsEndY + kCardBottomPad) - cardTop;
  renderer.drawRoundedRect(rectX, cardTop, rectWidth, height, 1, Chrome::kCardRadius, true);
}

struct LogDirSummary {
  uint32_t fileCount = 0;
  uint64_t totalBytes = 0;
};

LogDirSummary summarizeDir(const char* path) {
  LogDirSummary summary;
  HalFile dir = Storage.open(path);
  if (!dir) return summary;
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (!entry.isDirectory()) {
      summary.fileCount++;
      summary.totalBytes += entry.fileSize64();
    }
    entry.close();
  }
  dir.close();
  return summary;
}

void formatBytes(uint64_t bytes, char* out, size_t outSize) {
  if (bytes >= 1024 * 1024) {
    snprintf(out, outSize, "%.1f MB", bytes / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(out, outSize, "%.1f KB", bytes / 1024.0);
  } else {
    snprintf(out, outSize, "%llu B", static_cast<unsigned long long>(bytes));
  }
}
}  // namespace

void MaintenanceActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void MaintenanceActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onGoHome();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    activityManager.goToUsbTransfer();
  }
}

void MaintenanceActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // Short title — the previous "EXPORT / MAINTENANCE" combined with a wide landscape screen
  // that's otherwise crammed with content below (see the 2-column layout here) was reported as
  // reading badly near the top-right corner.
  Chrome::drawHeader(renderer, "EXPORT");

  const int top = Chrome::contentTop();
  // Reserve a line at the very bottom for the firmware-update note, so no card's content can
  // ever grow into it — the previous single-column layout had no such reservation and, with
  // raw-capture and active-deauth stats both present (a real combination — this is exactly what
  // shipped), ran past contentBottom() and off the visible screen entirely.
  const int firmwareLineY = Chrome::contentBottom(renderer) - renderer.getLineHeight(FONT_SMALL_ID);
  const int columnBottom = firmwareLineY - 8;
  const int colWidth = (Chrome::contentRight(renderer) - Chrome::contentLeft() - kColumnGap) / 2;
  const int leftColX = Chrome::contentLeft();
  const int rightColX = leftColX + colWidth + kColumnGap;

  // LEFT: encrypted log stats + a short export blurb.
  const LogDirSummary summary = summarizeDir(kLogDir);
  char valueBuf[24];
  char sizeBuf[24];
  int leftY = beginCard(renderer, leftColX, colWidth, top, "ENCRYPTED LOG");
  snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(summary.fileCount));
  leftY = Chrome::drawStatRow(renderer, leftY, "Log files", valueBuf, false, leftColX + colWidth, leftColX);
  formatBytes(summary.totalBytes, sizeBuf, sizeof(sizeBuf));
  leftY = Chrome::drawStatRow(renderer, leftY, "Total size", sizeBuf, false, leftColX + colWidth, leftColX);
  snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(encryptedLog.recordsWrittenThisBoot()));
  leftY = Chrome::drawStatRow(renderer, leftY, "Records this session", valueBuf, false, leftColX + colWidth, leftColX);
  leftY += 8;
  const auto logLines = renderer.wrappedText(
      FONT_SMALL_ID,
      "Export: power off, pull the SD card, copy /.ruby/log/*. AES-256-GCM encrypted — decrypt "
      "with scripts/decrypt_log.py using the key from Settings > Reveal log key. Or press Down "
      "for USB Transfer to pull files over USB without removing the card.",
      colWidth, 8);
  const int smallLineHeight = renderer.getLineHeight(FONT_SMALL_ID);
  for (const auto& line : logLines) {
    if (leftY + smallLineHeight > columnBottom) break;  // hard stop — never draw past the card's floor
    renderer.drawText(FONT_SMALL_ID, leftColX, leftY, line.c_str());
    leftY += smallLineHeight;
  }
  endCard(renderer, leftColX, colWidth, top, std::min(leftY, columnBottom));

  // RIGHT: raw-capture card and/or active-deauth card, stacked, only when there's something to
  // show — both are opt-in features most owners never turn on.
  const LogDirSummary pcapSummary = summarizeDir(PcapWriter::captureDirectory());
  const bool hasPcap = pcapSummary.fileCount > 0;
  const bool hasDeauth = deauthEngine.burstsSent() > 0;
  int rightY = top;

  if (hasPcap) {
    rightY = beginCard(renderer, rightColX, colWidth, rightY, "RAW CAPTURE");
    snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(pcapSummary.fileCount));
    rightY = Chrome::drawStatRow(renderer, rightY, "Files", valueBuf, false, rightColX + colWidth, rightColX);
    formatBytes(pcapSummary.totalBytes, sizeBuf, sizeof(sizeBuf));
    rightY = Chrome::drawStatRow(renderer, rightY, "Size", sizeBuf, false, rightColX + colWidth, rightColX);
    snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(wifiSniffer.pmkidCapableFrames()));
    rightY = Chrome::drawStatRow(renderer, rightY, "PMKID-capable", valueBuf, false, rightColX + colWidth, rightColX);
    rightY += 4;
    const auto pcapLines = renderer.wrappedText(
        FONT_SMALL_ID, "Plaintext .pcap under /.ruby/pcap/ — load into hashcat/hcxpcapngtool.", colWidth, 4);
    for (const auto& line : pcapLines) {
      if (rightY + smallLineHeight > columnBottom) break;  // hard stop — never draw past the card's floor
      renderer.drawText(FONT_SMALL_ID, rightColX, rightY, line.c_str());
      rightY += smallLineHeight;
    }
    const int cardTop = top;
    endCard(renderer, rightColX, colWidth, cardTop, std::min(rightY, columnBottom));
    rightY += kCardGap;
  }

  // Guard against the (currently implausible, but not structurally impossible) case where the
  // raw-capture card above already used the whole column — starting a second card past
  // columnBottom would draw it entirely off-screen instead of just being tight on room.
  if (hasDeauth && rightY < columnBottom) {
    const int cardTop = rightY;
    rightY = beginCard(renderer, rightColX, colWidth, rightY, "ACTIVE DEAUTH");
    snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(deauthEngine.burstsSent()));
    rightY = Chrome::drawStatRow(renderer, rightY, "Bursts attempted", valueBuf, false, rightColX + colWidth, rightColX);
    snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(deauthEngine.framesTransmitted()));
    rightY = Chrome::drawStatRow(renderer, rightY, "Frames sent", valueBuf, false, rightColX + colWidth, rightColX);
    // Confirmed via real-hardware testing: stock ESP-IDF's esp_wifi_80211_tx() rejects every
    // deauth-frame-type TX attempt outright (see DeauthEngine::framesRejectedByDriver()'s
    // comment) — if this equals "Frames sent" above, nothing has actually reached the air despite
    // every burst/frame counter above incrementing normally.
    snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(deauthEngine.framesRejectedByDriver()));
    rightY = Chrome::drawStatRow(renderer, rightY, "Rejected by driver", valueBuf, false, rightColX + colWidth, rightColX);
    endCard(renderer, rightColX, colWidth, cardTop, std::min(rightY, columnBottom));
  }

  if (!hasPcap && !hasDeauth) {
    renderer.drawText(FONT_SMALL_ID, rightColX, top + kCardTopPad,
                      "Raw capture and active deauth are both off — nothing else to report.");
  }

  renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), firmwareLineY,
                    "Firmware updates: pio run -e default -t upload over USB.");

  Chrome::drawFooterHints(renderer, "Home", "Home", nullptr, nullptr);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}
