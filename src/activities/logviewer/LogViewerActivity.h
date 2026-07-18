#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "LogRecord.h"
#include "activities/Activity.h"

// On-device browser for the encrypted capture log. This is possible without any key-entry UI
// because the AES key that wrote these files is already resident in EncryptedLog::aesKey for as
// long as this device stays powered — see EncryptedLog::decryptRecordRange(). Left/Right switch
// between daily log files; Up/Down page through records within the current file, newest page
// first. Nothing decrypted here is written back to storage — it only ever exists in `page` for
// the duration of one render.
class LogViewerActivity final : public Activity {
 public:
  explicit LogViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LogViewer", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void loadFileList();
  void refreshRecordCountAndPage();
  void loadCurrentPage();

  // Upper bound on how many records we ever decrypt/buffer for one page. How many of those
  // actually get drawn (and how far Up/Down page) is computed at render/nav time from the panel's
  // real content area — see gridCapacity() in the .cpp — so this constant only needs to
  // comfortably cover that dense grid's capacity, not match it exactly.
  static constexpr size_t kMaxPageRecords = 32;

  std::vector<std::string> files;  // filenames only (no dir prefix), sorted oldest to newest
  size_t fileIndex = 0;

  uint32_t recordCount = 0;
  uint32_t pageStart = 0;  // index of the oldest record in the currently-loaded page

  LogRecordPlaintext page[kMaxPageRecords];
  size_t pageCount = 0;
  bool decryptFailed = false;

  // Set on onEnter(), cleared after the first render() — gives this screen one crisp FULL_REFRESH
  // per visit, then FAST_REFRESH for the paging redraws that follow (this screen redraws on every
  // Left/Right/Up/Down press, so an unconditional FULL_REFRESH here would make paging sluggish).
  bool firstRenderSinceEnter = true;
};
