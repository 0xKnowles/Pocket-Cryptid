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

  // Deliberately small: each record renders as a multi-line bordered card (type, MAC, time,
  // RSSI, channel/address-kind, label, EAPOL message number) rather than one packed line, so
  // fewer fit per screen than the old dense 8-line layout did.
  static constexpr size_t kPageSize = 5;

  std::vector<std::string> files;  // filenames only (no dir prefix), sorted oldest to newest
  size_t fileIndex = 0;

  uint32_t recordCount = 0;
  uint32_t pageStart = 0;  // index of the oldest record in the currently-loaded page

  LogRecordPlaintext page[kPageSize];
  size_t pageCount = 0;
  bool decryptFailed = false;
};
