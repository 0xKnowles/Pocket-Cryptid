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
#include "LogRecord.h"
#include "fontIds.h"
#include "ui/Chrome.h"

namespace {
// Same tight grid geometry as Dashboard's RECENT DEVICES card and DeviceListActivity (see
// DeviceListActivity.cpp) — this screen's old bordered, generously-padded card format only fit 5
// records per page (~101px each) versus the ~488px of usable content height available, nowhere
// near as dense as the rest of the app's "live feed" screens.
constexpr int kLineHeight = 13;
constexpr int kLineGap = 2;
constexpr int kEntryGap = 6;
constexpr int kEntryHeight = kLineHeight * 3 + kLineGap * 2 + kEntryGap;
constexpr int kEntryColWidth = 224;
constexpr int kColumnGap = 14;

// y where the record grid actually starts, below the filename/counter row and the "Left/Right:
// switch file..." hint row. Spaced off each font's real getLineHeight() rather than guessed pixel
// counts — a fixed 20px/18px guess here used to run past FONT_UI_10_ID's real line height (31px),
// so the hint row started drawing before the filename row's descenders had finished, the same
// category of bug as the header/divider overlap fixed in Chrome.h. Shared by gridCapacity() and
// render() so the two can never disagree about where the grid begins.
int gridTop(const GfxRenderer& renderer) {
  int y = Chrome::contentTop();
  y += renderer.getLineHeight(FONT_UI_10_ID);  // filename (left) + page counter (right) row
  y += renderer.getLineHeight(FONT_SMALL_ID);  // "Left/Right: switch file..." hint row
  return y + 12;                               // gap below the divider drawn at that y
}

// How many records actually fit on screen at once in the grid below — computed from the real
// content area rather than hardcoded, so it stays correct if Chrome's header/footer geometry ever
// changes. Shared by render() (how many of the buffered page's records to draw and how to lay
// them out) and by the page-navigation logic below (so Up/Down page by exactly what's on screen,
// not some unrelated fixed stride).
size_t gridCapacity(const GfxRenderer& renderer) {
  const int rowsPerColumn = std::max(1, (Chrome::contentBottom(renderer) - gridTop(renderer)) / kEntryHeight);
  const int columnCount =
      std::max(1, (Chrome::contentRight(renderer) - Chrome::contentLeft()) / (kEntryColWidth + kColumnGap));
  return static_cast<size_t>(rowsPerColumn) * static_cast<size_t>(columnCount);
}

// Renders one record as a compact, borderless three-line entry — type+MAC, then
// time/RSSI/channel-or-address-kind, then the label (SSID/BLE name) plus the EAPOL message number
// for handshake records — at a fixed x/y rather than flowing down a single column, so records lay
// out in the same multi-column grid the rest of the app's dense feeds use.
void drawRecordEntry(const GfxRenderer& renderer, int x, int y, const LogRecordPlaintext& rec) {
  char macBuf[18];
  snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X", rec.mac[0], rec.mac[1], rec.mac[2], rec.mac[3],
           rec.mac[4], rec.mac[5]);
  char line1[32];
  snprintf(line1, sizeof(line1), "%s %s", logRecordTypeCompactName(rec.type), macBuf);
  renderer.drawText(FONT_SMALL_ID, x, y, line1, true, EpdFontFamily::BOLD);

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
  const int line2Y = y + kLineHeight + kLineGap;
  Chrome::drawSignalBars(renderer, x, line2Y, rec.rssi);
  char line2[40];
  snprintf(line2, sizeof(line2), "%s  %d dBm  %s", timeBuf, rec.rssi, extraBuf);
  renderer.drawText(FONT_SMALL_ID, x + Chrome::kSignalBarsWidth + 4, line2Y, line2);

  char labelBuf[25] = {};
  const uint8_t len = rec.labelLen > sizeof(labelBuf) - 1 ? sizeof(labelBuf) - 1 : rec.labelLen;
  memcpy(labelBuf, rec.label, len);
  labelBuf[len] = '\0';
  char line3[56];
  if (rec.type == LogRecordType::WifiHandshake && rec.eapolMsgNum > 0) {
    snprintf(line3, sizeof(line3), "%s EAPOL %u/4", labelBuf[0] ? labelBuf : "(no name)", rec.eapolMsgNum);
  } else {
    snprintf(line3, sizeof(line3), "%s", labelBuf[0] ? labelBuf : "(no name)");
  }
  // Column width is fixed, but a label can be up to 24 bytes plus an EAPOL suffix — truncate
  // rather than let a long one bleed into the next column.
  const std::string truncated = renderer.truncatedText(FONT_SMALL_ID, line3, kEntryColWidth);
  renderer.drawText(FONT_SMALL_ID, x, y + (kLineHeight + kLineGap) * 2, truncated.c_str());
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
  const size_t pageSize = std::min(gridCapacity(renderer), kMaxPageRecords);
  pageStart = recordCount > pageSize ? recordCount - static_cast<uint32_t>(pageSize) : 0;
  loadCurrentPage();
}

void LogViewerActivity::loadCurrentPage() {
  pageCount = 0;
  decryptFailed = false;
  if (files.empty() || recordCount == 0) return;

  const std::string path = std::string(EncryptedLog::logDirectory()) + "/" + files[fileIndex];
  const size_t pageSize = std::min(gridCapacity(renderer), kMaxPageRecords);
  pageCount = encryptedLog.decryptRecordRange(path.c_str(), pageStart, page, pageSize);
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
    const uint32_t pageSize = static_cast<uint32_t>(std::min(gridCapacity(renderer), kMaxPageRecords));
    pageStart = (pageStart >= pageSize) ? pageStart - pageSize : 0;
    loadCurrentPage();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    const uint32_t pageSize = static_cast<uint32_t>(std::min(gridCapacity(renderer), kMaxPageRecords));
    const uint32_t maxStart = recordCount > pageSize ? recordCount - pageSize : 0;
    pageStart = std::min(pageStart + pageSize, maxStart);
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
  // Real getLineHeight() per font, not guessed pixel counts — see gridTop()'s comment, which uses
  // this exact same formula so paging math never disagrees with what's actually drawn here.
  y += renderer.getLineHeight(FONT_UI_10_ID);
  renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, "Left/Right: switch file   Up/Down: page");
  y += renderer.getLineHeight(FONT_SMALL_ID);
  Chrome::drawDivider(renderer, y);
  y += 12;

  if (recordCount == 0) {
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, "This file has no records yet.");
  } else if (decryptFailed) {
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, "Could not decrypt this page.");
  } else {
    const int left = Chrome::contentLeft();
    const int rowsPerColumn = std::max(1, (Chrome::contentBottom(renderer) - y) / kEntryHeight);
    const int columnCount =
        std::max(1, (Chrome::contentRight(renderer) - left) / (kEntryColWidth + kColumnGap));
    const size_t maxVisible = std::min(pageCount, static_cast<size_t>(rowsPerColumn * columnCount));

    // Newest record in the page first, same reading order the old single-column layout used —
    // just laid out column-major (top-to-bottom, then next column) like Dashboard/DeviceListActivity's
    // grids, rather than flowing down one long column.
    for (size_t i = 0; i < maxVisible; i++) {
      const size_t idx = pageCount - 1 - i;
      const size_t col = i / static_cast<size_t>(rowsPerColumn);
      const size_t row = i % static_cast<size_t>(rowsPerColumn);
      const int entryX = left + static_cast<int>(col) * (kEntryColWidth + kColumnGap);
      const int entryY = y + static_cast<int>(row) * kEntryHeight;
      drawRecordEntry(renderer, entryX, entryY, page[idx]);
    }
  }

  Chrome::drawFooterHints(renderer, "Home", nullptr, "Prev", "Next");
  renderer.displayBuffer(refreshMode);
}
