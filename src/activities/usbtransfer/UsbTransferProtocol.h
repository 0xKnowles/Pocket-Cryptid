#pragma once

#include <cstddef>
#include <cstdint>

// Wire format for pulling .pclog files off the SD card over USB-CDC without removing the card.
// This is the one authoritative spec for the framing — a host-side client (e.g. RubySift) is a
// separate codebase and re-implements this from the doc below, so keep the two in lockstep by
// hand if it ever changes.
//
// Every message in both directions is one frame:
//
//   [ 4B magic "RBY1" ][ 1B opcode ][ 4B payload length, little-endian ][ payload ]
//
// No CRC: USB-CDC is already a reliable, ordered byte stream, and file content integrity is
// already covered end-to-end by the GCM auth tag baked into the .pclog format itself (see
// EncryptedLog.cpp) — a checksum here would only catch corruption CDC itself doesn't produce.
//
// This channel only exists while UsbTransferActivity is the active screen (the user physically
// navigated into it from Maintenance) — there is no background listener during normal capture, so
// nothing about the device's normal operation exposes a service. See MaintenanceActivity.h for the
// stealth rationale this trades off against.
//
// Commands (host -> device):
//   kOpPing              — no payload. Device replies kOpPong.
//   kOpList               — no payload. Device replies kOpListOk with, for each file in the log
//                            directory: [2B name length LE][name bytes][4B file size LE], repeated
//                            back-to-back until the payload is exhausted (0 files = 0-length payload).
//   kOpGet                — payload = filename only (no directory prefix), UTF-8, no NUL
//                            terminator, at most kMaxFilenameLen bytes. Device replies kOpGetOk with
//                            payload length set to the file's exact size, immediately followed by
//                            the raw file bytes (streamed straight off the SD card) — not chunked
//                            into further frames. Unknown or unreadable file -> kOpErr instead.
//   kOpKey                 — no payload. Device replies kOpKeyOk with the 64-hex-char AES-256 key as
//                            ASCII, the same value MaintenanceActivity's "Reveal log key" shows on
//                            screen. Only reachable by a host already having a live connection to
//                            this screen, i.e. the same physical-possession bar as reading it off
//                            the display.
//
// Responses (device -> host):
//   kOpPong, kOpListOk, kOpGetOk, kOpKeyOk — as above.
//   kOpErr                  — payload = short ASCII error message.
namespace UsbTransferProtocol {

inline constexpr uint8_t kMagic[4] = {'R', 'B', 'Y', '1'};
inline constexpr size_t kHeaderSize = sizeof(kMagic) + 1 /*opcode*/ + 4 /*length*/;

enum Opcode : uint8_t {
  kOpPing = 0x01,
  kOpList = 0x02,
  kOpGet = 0x03,
  kOpKey = 0x04,

  kOpPong = 0x81,
  kOpListOk = 0x82,
  kOpGetOk = 0x83,
  kOpKeyOk = 0x84,
  kOpErr = 0x8F,
};

// Longest filename UsbTransferActivity will accept in a kOpGet request. Real log filenames are
// "YYYYMMDD.pclog" (14 bytes) — this just needs enough headroom that a genuine filename never
// gets rejected, not to accommodate arbitrary length.
inline constexpr size_t kMaxFilenameLen = 127;

}  // namespace UsbTransferProtocol
