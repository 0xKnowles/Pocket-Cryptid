#include "LogViewerActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "EncryptedLog.h"
#include "fontIds.h"
#include "ui/Chrome.h"

void LogViewerActivity::onEnter() {
  Activity::onEnter();
  loadFileList();
  refreshRecordCountAndPage();
  requestUpdate();
}

void LogViewerActivity::loadFileList() {
  files.clear();
  HalFile dir = Storage.open(EncryptedLog::logDirectory());
  if (dir) {
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      if (!entry.isDirectory()) {
        char name[64] = {};
        entry.getName(name, sizeof(name));
        files.emplace_back(name);
      }
      entry.close();
    }
    dir.close();
  }
  std::sort(files.begin(), files.end());
  fileIndex = files.empty() ? 0 : files.size() - 1;  // start on the most recent date
}

void LogViewerActivity::refreshRecordCountAndPage() {
  recordCount = 0;
  if (!files.empty()) {
    const std::string path = std::string(EncryptedLog::logDirectory()) + "/" + files[fileIndex];
    HalFile file = Storage.open(path.c_str());
    if (file) {
      recordCount = EncryptedLog::recordCountForFileSize(file.fileSize64());
      file.close();
    }
  }
  pageStart = recordCount > kPageSize ? recordCount - kPageSize : 0;
  loadCurrentPage();
}

void LogViewerActivity::loadCurrentPage() {
  pageCount = 0;
  decryptFailed = false;
  if (files.empty() || recordCount == 0) return;

  const std::string path = std::string(EncryptedLog::logDirectory()) + "/" + files[fileIndex];
  pageCount = encryptedLog.decryptRecordRange(path.c_str(), pageStart, page, kPageSize);
  decryptFailed = (pageCount == 0);
}

void LogViewerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  if (files.empty()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    fileIndex = (fileIndex == 0) ? files.size() - 1 : fileIndex - 1;
    refreshRecordCountAndPage();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    fileIndex = (fileIndex + 1) % files.size();
    refreshRecordCountAndPage();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    pageStart = (pageStart >= kPageSize) ? pageStart - kPageSize : 0;
    loadCurrentPage();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    const uint32_t maxStart = recordCount > kPageSize ? recordCount - kPageSize : 0;
    pageStart = std::min(pageStart + static_cast<uint32_t>(kPageSize), maxStart);
    loadCurrentPage();
    requestUpdate();
    return;
  }
}

void LogViewerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, "LOG VIEWER");

  int y = Chrome::contentTop();

  if (files.empty()) {
    renderer.drawCenteredText(FONT_UI_10_ID, renderer.getScreenHeight() / 2, "No log files on SD yet.");
    Chrome::drawFooterHints(renderer, "Home", nullptr, nullptr, nullptr);
    renderer.displayBuffer();
    return;
  }

  renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, files[fileIndex].c_str(), true, EpdFontFamily::BOLD);

  char counterBuf[32];
  if (recordCount == 0) {
    snprintf(counterBuf, sizeof(counterBuf), "empty");
  } else {
    snprintf(counterBuf, sizeof(counterBuf), "%lu-%lu of %lu", static_cast<unsigned long>(pageStart + 1),
             static_cast<unsigned long>(pageStart + pageCount), static_cast<unsigned long>(recordCount));
  }
  const int counterW = renderer.getTextWidth(FONT_SMALL_ID, counterBuf);
  renderer.drawText(FONT_SMALL_ID, Chrome::contentRight(renderer) - counterW, y + 2, counterBuf);
  y += 20;
  renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, "Left/Right: switch file   Up/Down: page");
  y += 18;
  Chrome::drawDivider(renderer, y);
  y += 12;

  if (recordCount == 0) {
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, "This file has no records yet.");
  } else if (decryptFailed) {
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, "Could not decrypt this page.");
  } else {
    const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
    // Newest record in the page first.
    for (size_t i = pageCount; i-- > 0;) {
      const LogRecordPlaintext& rec = page[i];
      char macBuf[18];
      snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X", rec.mac[0], rec.mac[1], rec.mac[2],
               rec.mac[3], rec.mac[4], rec.mac[5]);

      const time_t t = static_cast<time_t>(rec.unixTime);
      struct tm tmVal;
      gmtime_r(&t, &tmVal);
      char timeBuf[10];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", tmVal.tm_hour, tmVal.tm_min, tmVal.tm_sec);

      char line1[56];
      snprintf(line1, sizeof(line1), "%-6s %s  %s  %d dBm", logRecordTypeShortName(rec.type), macBuf, timeBuf,
               rec.rssi);
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line1);
      y += lineHeight + 2;

      char labelBuf[25] = {};
      const uint8_t len = rec.labelLen > sizeof(labelBuf) - 1 ? sizeof(labelBuf) - 1 : rec.labelLen;
      memcpy(labelBuf, rec.label, len);
      labelBuf[len] = '\0';
      char line2[40];
      snprintf(line2, sizeof(line2), "  %s", labelBuf[0] ? labelBuf : "(no name)");
      renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line2);
      y += lineHeight + 6;
    }
  }

  Chrome::drawFooterHints(renderer, "Home", nullptr, "Prev", "Next");
  renderer.displayBuffer();
}
