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

// logSerial.write() can return fewer bytes than requested when the USB CDC TX buffer is full --
// confirmed on real hardware: a ~35MB transfer's device-side status read "Sent ... (36672720
// bytes)" (its write loop believed every chunk went out in full, since nothing checked the return
// value) while the host only ever received 36393159 -- ~280KB silently dropped near the very end,
// with no error on either side. Every write in this file goes through here now instead of a bare
// logSerial.write(), retrying (with a yield() so the USB stack gets a chance to drain) until every
// byte is actually accounted for.
void writeAll(const uint8_t* data, size_t len) {
  size_t written = 0;
  while (written < len) {
    const size_t n = logSerial.write(data + written, len - written);
    if (n == 0) {
      yield();
      continue;
    }
    written += n;
  }
}

// Blocks (up to kGetAckTimeoutMs) for the host's kOpGetAck frame after a kOpGetOk exchange.
// Confirmed necessary on real hardware even after writeAll() + flush(): a successful write/flush
// only proves the device's own USB CDC driver drained its TX buffer, not that the host actually
// received the tail before this screen moved on (e.g. into its own render(), which can block long
// enough on the e-ink bus to lose bytes still genuinely in flight over USB). Like the rest of this
// screen's parsing (see serviceProtocol()'s comment), this trusts a cooperating host client to send
// exactly one kOpGetAck frame here and nothing else, so it's safe to consume the one frame header
// that arrives without needing to hand anything back to serviceProtocol().
bool waitForGetAck(uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    if (logSerial.available() >= static_cast<int>(kHeaderSize)) {
      uint8_t header[kHeaderSize];
      if (!readExact(header, sizeof(header))) return false;
      return memcmp(header, kMagic, sizeof(kMagic)) == 0 && header[4] == kOpGetAck;
    }
    yield();
  }
  return false;
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
  writeAll(header, sizeof(header));
}

void UsbTransferActivity::sendFrame(uint8_t opcode, const uint8_t* payload, uint32_t payloadLen) {
  sendHeader(opcode, payloadLen);
  if (payload && payloadLen > 0) {
    writeAll(payload, payloadLen);
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

void UsbTransferActivity::handleGet(uint32_t payloadLen) {
  // payload = [4B offset LE][4B length LE][filename bytes] -- see UsbTransferProtocol.h for why
  // this is a bounded chunk request rather than "send me the whole file": real .pclog files can be
  // tens of MB, and streaming one as a single giant burst reliably lost a growing tail of it well
  // before EOF on real hardware, no matter how many stronger guarantees (write-retry, flush, host
  // ack) got added around that one exchange -- see CHANGELOG for that whole saga. Bounding each
  // exchange to kMaxChunkSize keeps any one write small enough that this stopped reproducing, and
  // confines a lost chunk to one retry instead of losing the last mile of a huge transfer.
  if (payloadLen <= kGetRequestPrefixSize || payloadLen - kGetRequestPrefixSize > kMaxFilenameLen) {
    sendError("bad request size");
    return;
  }

  uint8_t prefix[kGetRequestPrefixSize];
  if (!readExact(prefix, sizeof(prefix))) {
    sendError("short request");
    return;
  }
  const uint32_t offset = decodeLE32(prefix);
  const uint32_t requestedLen = decodeLE32(prefix + 4);

  const uint32_t filenameLen = payloadLen - kGetRequestPrefixSize;
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
  if (offset > fileSize) {
    f.close();
    sendError("offset past end of file");
    return;
  }
  if (!f.seek(offset)) {
    f.close();
    sendError("seek failed");
    return;
  }

  uint32_t chunkLen = requestedLen;
  if (chunkLen > kMaxChunkSize) chunkLen = kMaxChunkSize;
  if (chunkLen > fileSize - offset) chunkLen = fileSize - offset;

  sendHeader(kOpGetOk, chunkLen);

  // Deliberately not Storage.readFileToStream(), whose internal copy loop has no yield() between
  // chunks: GfxRenderer.cpp documents the same failure mode for a tight loop of blocking ~115200
  // baud serial writes (there, repeated LOG_ERR calls) -- enough of them back-to-back starves the
  // idle task long enough to trip the watchdog.
  uint8_t buf[512];
  uint32_t remaining = chunkLen;
  bool shortRead = false;
  while (remaining > 0) {
    const size_t toRead = remaining < sizeof(buf) ? remaining : sizeof(buf);
    const int n = f.read(buf, toRead);
    if (n <= 0) {
      shortRead = true;
      break;
    }
    writeAll(buf, static_cast<size_t>(n));
    remaining -= static_cast<uint32_t>(n);
    yield();
  }
  f.close();

  std::string statusPrefix;
  if (shortRead) {
    // The header already promised the host exactly chunkLen bytes for this frame's payload --
    // there's no way to signal an error mid-payload without leaving every request after this one
    // reading out of sync. Padding with zeros keeps the wire in sync: the corrupted tail just
    // fails the host's GCM auth check on whichever record it lands in instead of a truncated
    // chunk or a connection stuck desynced for good.
    const uint32_t shortAt = offset + (chunkLen - remaining);
    uint8_t zero[512] = {0};
    while (remaining > 0) {
      const size_t toWrite = remaining < sizeof(zero) ? remaining : sizeof(zero);
      writeAll(zero, toWrite);
      remaining -= static_cast<uint32_t>(toWrite);
      yield();
    }
    statusPrefix =
        "SD read failed at " + std::to_string(shortAt) + "/" + std::to_string(fileSize) + " for " + std::string(name);
  } else {
    statusPrefix = "Sent " + std::string(name) + " [" + std::to_string(offset) + "+" + std::to_string(chunkLen) +
                    "/" + std::to_string(fileSize) + "]";
  }

  // writeAll() only guarantees every byte was handed to the USB CDC driver's own buffer -- not
  // that it actually went out over the wire yet. flush() blocks until the driver's TX buffer is
  // actually drained, which writeAll() alone does not.
  logSerial.flush();

  // Even flush() only proves the device's own side is clear -- not that the host has the bytes
  // yet. Block for the host's explicit acknowledgement instead of just assuming completion once
  // the device believes it's done, so a chunk the host never actually received can be retried by
  // the host requesting the same offset again rather than silently moving on.
  const bool acked = waitForGetAck(kGetAckTimeoutMs);
  lastStatus = statusPrefix + (acked ? "" : " (no host ack)");
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
