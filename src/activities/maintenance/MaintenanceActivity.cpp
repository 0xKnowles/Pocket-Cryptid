#include "MaintenanceActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <cstddef>
#include <cstdio>

#include "EncryptedLog.h"
#include "PcapWriter.h"
#include "fontIds.h"
#include "ui/Chrome.h"

namespace {
constexpr char kLogDir[] = "/.ruby/log";

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
  }
}

void MaintenanceActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, "EXPORT / MAINTENANCE");

  const int contentWidth = Chrome::contentRight(renderer) - Chrome::contentLeft();
  int y = Chrome::contentTop();

  const LogDirSummary summary = summarizeDir(kLogDir);
  char valueBuf[24];

  snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(summary.fileCount));
  y = Chrome::drawStatRow(renderer, y, "Log files on SD", valueBuf);
  char sizeBuf[24];
  formatBytes(summary.totalBytes, sizeBuf, sizeof(sizeBuf));
  y = Chrome::drawStatRow(renderer, y, "Total log size", sizeBuf);
  snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(encryptedLog.recordsWrittenThisBoot()));
  Chrome::drawStatRow(renderer, y, "Records this session", valueBuf);

  y += 40;
  Chrome::drawDivider(renderer, y);
  y += 16;

  const auto logLines = renderer.wrappedText(
      FONT_UI_10_ID,
      "To export: power the device off, remove the SD card, and copy the files under "
      "/.ruby/log/ to a computer. Each file is AES-256-GCM encrypted — decrypt with "
      "scripts/decrypt_log.py and the key shown in Settings > Reveal log key.",
      contentWidth, 8);
  const int lineHeight = renderer.getLineHeight(FONT_UI_10_ID);
  for (const auto& line : logLines) {
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, line.c_str());
    y += lineHeight;
  }

  const LogDirSummary pcapSummary = summarizeDir(PcapWriter::captureDirectory());
  if (pcapSummary.fileCount > 0) {
    y += 24;
    Chrome::drawDivider(renderer, y);
    y += 16;

    snprintf(valueBuf, sizeof(valueBuf), "%lu", static_cast<unsigned long>(pcapSummary.fileCount));
    y = Chrome::drawStatRow(renderer, y, "Raw capture files", valueBuf);
    formatBytes(pcapSummary.totalBytes, sizeBuf, sizeof(sizeBuf));
    y = Chrome::drawStatRow(renderer, y, "Raw capture size", sizeBuf);
    y += 8;

    const auto pcapLines = renderer.wrappedText(
        FONT_UI_10_ID,
        "Files under /.ruby/pcap/ are plaintext .pcap — NOT encrypted like the log above. Load "
        "directly into hashcat/hcxpcapngtool. Turn off in Settings > Raw handshake capture when "
        "not actively auditing.",
        contentWidth, 8);
    for (const auto& line : pcapLines) {
      renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, line.c_str());
      y += lineHeight;
    }
  }

  y += 16;
  renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y,
                    "Firmware updates: pio run -e default -t upload over USB.");

  Chrome::drawFooterHints(renderer, "Home", "Home", nullptr, nullptr);
  renderer.displayBuffer();
}
