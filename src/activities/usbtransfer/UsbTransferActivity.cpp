#include "UsbTransferActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

#include "CaptureControl.h"
#include "EncryptedLog.h"
#include "UsbTransferProtocol.h"
#include "fontIds.h"
#include "ui/Chrome.h"

using namespace UsbTransferProtocol;

namespace {
// Reads exactly `len` bytes with a short overall timeout, matching Arduino Stream::readBytes'
// existing default timeout (1000ms) — plenty for a host on the other end of a live USB-CDC link;
// a genuinely gone host just leaves this screen sitting at "Waiting for host..." next loop().
bool readExact(uint8_t* buf, size_t len) { return logSerial.readBytes(buf, len) == len; }

uint32_t decodeLE32(const uint8_t* b) {
  return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
         (static_cast<uint32_t>(b[3]) << 24);
}

void encodeLE32(uint32_t v, uint8_t* out) {
  out[0] = static_cast<uint8_t>(v);
  out[1] = static_cast<uint8_t>(v >> 8);
  out[2] = static_cast<uint8_t>(v >> 16);
  out[3] = static_cast<uint8_t>(v >> 24);
}

void encodeLE16(uint16_t v, uint8_t* out) {
  out[0] = static_cast<uint8_t>(v);
  out[1] = static_cast<uint8_t>(v >> 8);
}
}  // namespace

void UsbTransferActivity::onEnter() {
  Activity::onEnter();
  // Muted for the entire lifetime of this screen, not just around individual frame reads — every
  // other subsystem's tick() (WifiSniffer, EncryptedLog, ...) keeps running in the background and
  // can log at any point in between our own reads/writes.
  setSerialLogMuted(true);

  // Pausing capture (same mechanism as Dashboard's own Pause) stops all WiFi/BLE scanning and SD
  // writes for as long as this screen is open: one less thing competing for the SD card's shared
  // SPI bus and CPU time with the file transfer itself, and it keeps the log file this screen is
  // reading from static instead of growing mid-read. Only resumed on exit if this screen is the
  // one that paused it — see the header comment on pausedCaptureOnEnter.
  pausedCaptureOnEnter = !captureIsPaused();
  if (pausedCaptureOnEnter) {
    toggleCapturePause();
  }

  lastStatus = "Waiting for host...";
  framesServed = 0;
  requestUpdate();
}

void UsbTransferActivity::onExit() {
  if (pausedCaptureOnEnter) {
    toggleCapturePause();
  }
  setSerialLogMuted(false);
  Activity::onExit();
}

void UsbTransferActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  serviceProtocol();
}

void UsbTransferActivity::serviceProtocol() {
  if (logSerial.available() <= 0) return;

  uint8_t header[kHeaderSize];
  if (!readExact(header, sizeof(header))) {
    lastStatus = "Short read (dropped)";
    requestUpdate();
    return;
  }
  if (memcmp(header, kMagic, sizeof(kMagic)) != 0) {
    // Not a resync-capable parser — a malformed frame just gets dropped here rather than hunted
    // for the next magic sequence. Acceptable because logging is muted for this screen's whole
    // lifetime (see onEnter()), so the only bytes that ever arrive on this wire are ones a
    // cooperating host client sent on purpose.
    lastStatus = "Bad magic (dropped)";
    requestUpdate();
    return;
  }

  const uint8_t opcode = header[4];
  const uint32_t length = decodeLE32(header + 5);

  switch (opcode) {
    case kOpPing:
      handlePing();
      break;
    case kOpList:
      handleList();
      break;
    case kOpGet:
      handleGet(length);
      break;
    case kOpKey:
      handleKey();
      break;
    default:
      sendError("unknown opcode");
      break;
  }
  framesServed++;
  requestUpdate();
}

void UsbTransferActivity::sendHeader(uint8_t opcode, uint32_t payloadLen) {
  uint8_t header[kHeaderSize];
  memcpy(header, kMagic, sizeof(kMagic));
  header[4] = opcode;
  encodeLE32(payloadLen, header + 5);
  logSerial.write(header, sizeof(header));
}

void UsbTransferActivity::sendFrame(uint8_t opcode, const uint8_t* payload, uint32_t payloadLen) {
  sendHeader(opcode, payloadLen);
  if (payload && payloadLen > 0) {
    logSerial.write(payload, payloadLen);
  }
}

void UsbTransferActivity::sendError(const char* message) {
  const size_t len = strnlen(message, 200);
  sendFrame(kOpErr, reinterpret_cast<const uint8_t*>(message), static_cast<uint32_t>(len));
  lastStatus = std::string("Error: ") + message;
}

void UsbTransferActivity::handlePing() {
  sendFrame(kOpPong, nullptr, 0);
  lastStatus = "Ping";
}

void UsbTransferActivity::handleList() {
  const char* dir = EncryptedLog::logDirectory();
  HalFile root = Storage.open(dir);
  if (!root || !root.isDirectory()) {
    sendFrame(kOpListOk, nullptr, 0);
    lastStatus = "Listed 0 files";
    return;
  }

  std::string payload;
  char name[128];
  uint32_t fileCount = 0;
  for (HalFile f = root.openNextFile(); f; f = root.openNextFile()) {
    if (!f.isDirectory()) {
      const size_t nameLen = f.getName(name, sizeof(name));
      const uint16_t clampedLen = static_cast<uint16_t>(nameLen > 255 ? 255 : nameLen);
      const uint32_t fileSize = static_cast<uint32_t>(f.fileSize64());

      uint8_t lenBuf[2];
      encodeLE16(clampedLen, lenBuf);
      payload.append(reinterpret_cast<const char*>(lenBuf), sizeof(lenBuf));
      payload.append(name, clampedLen);

      uint8_t sizeBuf[4];
      encodeLE32(fileSize, sizeBuf);
      payload.append(reinterpret_cast<const char*>(sizeBuf), sizeof(sizeBuf));
      fileCount++;
    }
    f.close();
  }
  root.close();

  sendFrame(kOpListOk, reinterpret_cast<const uint8_t*>(payload.data()), static_cast<uint32_t>(payload.size()));
  lastStatus = "Listed " + std::to_string(fileCount) + " file(s)";
}

void UsbTransferActivity::handleGet(uint32_t filenameLen) {
  if (filenameLen == 0 || filenameLen > kMaxFilenameLen) {
    sendError("bad filename length");
    return;
  }

  char name[kMaxFilenameLen + 1];
  if (!readExact(reinterpret_cast<uint8_t*>(name), filenameLen)) {
    sendError("short filename");
    return;
  }
  name[filenameLen] = '\0';

  const std::string fullPath = std::string(EncryptedLog::logDirectory()) + "/" + name;
  HalFile f = Storage.open(fullPath.c_str());
  if (!f) {
    sendError("file not found");
    return;
  }
  const uint32_t fileSize = static_cast<uint32_t>(f.fileSize64());

  sendHeader(kOpGetOk, fileSize);

  // Deliberately not Storage.readFileToStream(), whose internal copy loop has no yield() between
  // chunks: GfxRenderer.cpp documents the same failure mode for a tight loop of blocking ~115200
  // baud serial writes (there, repeated LOG_ERR calls) -- enough of them back-to-back starves the
  // idle task long enough to trip the watchdog. A multi-megabyte .pclog streamed 512 bytes at a
  // time from here made that a certainty rather than an edge case: real-hardware testing hit a
  // mid-transfer reboot every time (visible on the host as garbage where a protocol frame should
  // be -- either leftover ciphertext from the abandoned transfer, or literal log text once the
  // device had already rebooted back to normal logging).
  uint8_t buf[512];
  uint32_t remaining = fileSize;
  bool shortRead = false;
  while (remaining > 0) {
    const size_t toRead = remaining < sizeof(buf) ? remaining : sizeof(buf);
    const int n = f.read(buf, toRead);
    if (n <= 0) {
      shortRead = true;
      break;
    }
    logSerial.write(buf, static_cast<size_t>(n));
    remaining -= static_cast<uint32_t>(n);
    yield();
  }
  f.close();

  if (shortRead) {
    // The header already promised the host exactly fileSize bytes for this frame's payload --
    // there's no way to signal an error mid-payload without leaving every request after this one
    // reading out of sync (the host has no framing cue to know a short payload means "abort" vs.
    // "here is the whole file"). Padding with zeros keeps the wire in sync: the corrupted tail
    // just fails the host's GCM auth check on that record instead of a truncated file or a
    // connection stuck desynced for good. Real-hardware testing hit this on a ~35MB file: a
    // Storage.readFileToStream()-free chunked copy loop (see above) got as far as 93% before a
    // read stopped returning data, with nothing on this screen or in the (muted) logs to say why.
    const uint32_t shortAt = fileSize - remaining;
    uint8_t zero[512] = {0};
    while (remaining > 0) {
      const size_t toWrite = remaining < sizeof(zero) ? remaining : sizeof(zero);
      logSerial.write(zero, toWrite);
      remaining -= static_cast<uint32_t>(toWrite);
      yield();
    }
    lastStatus =
        "SD read failed at " + std::to_string(shortAt) + "/" + std::to_string(fileSize) + " for " + std::string(name);
  } else {
    lastStatus = "Sent " + std::string(name) + " (" + std::to_string(fileSize) + " bytes)";
  }
}

void UsbTransferActivity::handleKey() {
  char hex[65];
  if (!encryptedLog.revealDecryptionKeyHex(hex, sizeof(hex))) {
    sendError("key unavailable");
    return;
  }
  const size_t len = strnlen(hex, sizeof(hex));
  sendFrame(kOpKeyOk, reinterpret_cast<const uint8_t*>(hex), static_cast<uint32_t>(len));
  lastStatus = "Key revealed to host";
}

void UsbTransferActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, "USB TRANSFER");

  const int top = Chrome::contentTop();
  const int lineHeight = renderer.getLineHeight(FONT_SMALL_ID);
  int y = top;

  renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, "Connect over USB and pull .pclog files with RubySift.");
  y += lineHeight + 8;

  if (pausedCaptureOnEnter) {
    renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, "Capture paused for the duration of this screen.");
    y += lineHeight + 8;
  }

  renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, lastStatus.c_str());
  y += lineHeight + 8;

  const std::string countLine = "Requests served this session: " + std::to_string(framesServed);
  renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, countLine.c_str());

  Chrome::drawFooterHints(renderer, "Exit", nullptr, nullptr, nullptr);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
