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

namespace {
constexpr int kCardPadX = 8;
constexpr int kCardPadY = 6;
constexpr int kCardGap = 8;

// Renders one record as a bordered, three-line card — type+MAC, then time/RSSI/channel-or-
// address-kind, then the label (SSID/BLE name) plus the EAPOL message number for handshake
// records. Returns the y for the next card. Broken into three clearly-separated lines (rather
// than the old two packed, abbreviated lines) specifically because "hard to read" was the
// complaint; showing the channel/address-kind and EAPOL number is the "more data" half of it.
int drawRecordCard(const GfxRenderer& renderer, int y, const LogRecordPlaintext& rec) {
  const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
  const int cardX = Chrome::contentLeft() - 2;
  const int cardWidth = Chrome::contentRight(renderer) - Chrome::contentLeft() + 4;
  const int textX = Chrome::contentLeft() + kCardPadX - 2;
  int ty = y + kCardPadY;

  char macBuf[18];
  snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X", rec.mac[0], rec.mac[1], rec.mac[2], rec.mac[3],
           rec.mac[4], rec.mac[5]);
  char line1[40];
  snprintf(line1, sizeof(line1), "%-6s %s", logRecordTypeShortName(rec.type), macBuf);
  renderer.drawText(FONT_SMALL_ID, textX, ty, line1, true, EpdFontFamily::BOLD);
  ty += lineHeight + 3;

  const time_t t = static_cast<time_t>(rec.unixTime);
  struct tm tmVal;
  gmtime_r(&t, &tmVal);
  char timeBuf[10];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", tmVal.tm_hour, tmVal.tm_min, tmVal.tm_sec);

  char extraBuf[16];
  if (rec.type == LogRecordType::BleDevice) {
    snprintf(extraBuf, sizeof(extraBuf), "%s addr", rec.extra ? "random" : "public");
  } else {
    snprintf(extraBuf, sizeof(extraBuf), "ch %u", rec.extra);
  }
  char line2[48];
  snprintf(line2, sizeof(line2), "%s   %d dBm   %s", timeBuf, rec.rssi, extraBuf);
  renderer.drawText(FONT_SMALL_ID, textX, ty, line2);
  ty += lineHeight + 3;

  char labelBuf[25] = {};
  const uint8_t len = rec.labelLen > sizeof(labelBuf) - 1 ? sizeof(labelBuf) - 1 : rec.labelLen;
  memcpy(labelBuf, rec.label, len);
  labelBuf[len] = '\0';
  char line3[56];
  if (rec.type == LogRecordType::WifiHandshake && rec.eapolMsgNum > 0) {
    snprintf(line3, sizeof(line3), "%s   EAPOL msg %u/4", labelBuf[0] ? labelBuf : "(no name)", rec.eapolMsgNum);
  } else {
    snprintf(line3, sizeof(line3), "%s", labelBuf[0] ? labelBuf : "(no name)");
  }
  renderer.drawText(FONT_SMALL_ID, textX, ty, line3);
  ty += lineHeight + kCardPadY;

  renderer.drawRoundedRect(cardX, y, cardWidth, ty - y, 1, Chrome::kCardRadius, true);
  return ty + kCardGap;
}
}  // namespace

void LogViewerActivity::onEnter() {
  Activity::onEnter();
  firstRenderSinceEnter = true;
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
  const HalDisplay::RefreshMode refreshMode =
      firstRenderSinceEnter ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH;
  firstRenderSinceEnter = false;

  renderer.clearScreen();
  Chrome::drawHeader(renderer, "LOG VIEWER");

  int y = Chrome::contentTop();

  if (files.empty()) {
    renderer.drawCenteredText(FONT_UI_10_ID, renderer.getScreenHeight() / 2, "No log files on SD yet.");
    Chrome::drawFooterHints(renderer, "Home", nullptr, nullptr, nullptr);
    renderer.displayBuffer(refreshMode);
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
    // Newest record in the page first.
    for (size_t i = pageCount; i-- > 0;) {
      y = drawRecordCard(renderer, y, page[i]);
    }
  }

  Chrome::drawFooterHints(renderer, "Home", nullptr, "Prev", "Next");
  renderer.displayBuffer(refreshMode);
}
