#include "Screenshot.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
constexpr char kScreenshotDir[] = "/.ruby/screenshots";

// Plain, uncompressed 1bpp BMP — matches the framebuffer's own format exactly (see below), so
// this is a straight memory dump plus a header, not a real encoder.
struct BmpFileHeader {
  uint16_t bfType;
  uint32_t bfSize;
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  uint32_t bfOffBits;
} __attribute__((packed));

struct BmpInfoHeader {
  uint32_t biSize;
  int32_t biWidth;
  int32_t biHeight;
  uint16_t biPlanes;
  uint16_t biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
} __attribute__((packed));

// BMP rows pad to a 4-byte boundary; the framebuffer's own row stride (getDisplayWidthBytes())
// doesn't necessarily land on one (e.g. the X3 panel's 792px width is 99 bytes/row). 128 covers
// every panel this firmware targets (800px wide is the largest, 100 bytes/row) with room to
// spare — checked at runtime below rather than assumed, since a future wider panel would
// otherwise silently truncate rows instead of failing loudly.
constexpr size_t kMaxRowBytes = 128;
}  // namespace

bool saveScreenshot(const GfxRenderer& renderer) {
  const uint8_t* src = renderer.getFrameBuffer();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int srcStride = renderer.getDisplayWidthBytes();
  if (!src || width <= 0 || height <= 0 || srcStride <= 0) {
    LOG_ERR("SCREENSHOT", "Renderer not ready");
    return false;
  }

  const int dstStride = (srcStride + 3) & ~3;
  if (static_cast<size_t>(dstStride) > kMaxRowBytes) {
    LOG_ERR("SCREENSHOT", "Row stride %d exceeds kMaxRowBytes %zu", dstStride, kMaxRowBytes);
    return false;
  }

  if (!Storage.ensureDirectoryExists(kScreenshotDir)) {
    LOG_ERR("SCREENSHOT", "Failed to create %s", kScreenshotDir);
    return false;
  }

  // Named by unix time for a human-readable sort order; a numeric suffix handles the rare case of
  // two screenshots inside the same second (clock not set yet, so time(nullptr) is stuck at 0, or
  // just two button presses close together).
  char path[64];
  const long now = static_cast<long>(time(nullptr));
  snprintf(path, sizeof(path), "%s/%ld.bmp", kScreenshotDir, now);
  for (int suffix = 2; Storage.exists(path); suffix++) {
    snprintf(path, sizeof(path), "%s/%ld_%d.bmp", kScreenshotDir, now, suffix);
  }

  const uint32_t pixelDataSize = static_cast<uint32_t>(dstStride) * static_cast<uint32_t>(height);
  constexpr uint32_t kPaletteBytes = 2 * sizeof(uint32_t);

  BmpFileHeader fileHeader{};
  fileHeader.bfType = 0x4D42;  // 'BM'
  fileHeader.bfOffBits = sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + kPaletteBytes;
  fileHeader.bfSize = fileHeader.bfOffBits + pixelDataSize;

  BmpInfoHeader infoHeader{};
  infoHeader.biSize = sizeof(BmpInfoHeader);
  infoHeader.biWidth = width;
  infoHeader.biHeight = height;  // positive => bottom-up row order, matches the write loop below
  infoHeader.biPlanes = 1;
  infoHeader.biBitCount = 1;
  infoHeader.biCompression = 0;  // BI_RGB
  infoHeader.biSizeImage = pixelDataSize;
  infoHeader.biClrUsed = 2;
  infoHeader.biClrImportant = 2;

  // GfxRenderer::drawPixel clears a bit to draw ink (0 = black) and leaves it set otherwise
  // (1 = white) — palette index 0 -> black, index 1 -> white mirrors that directly, no bit
  // inversion needed.
  const uint32_t palette[2] = {0x00000000u, 0x00FFFFFFu};

  HalFile file = Storage.open(path, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("SCREENSHOT", "Failed to open %s for writing", path);
    return false;
  }

  file.write(reinterpret_cast<const uint8_t*>(&fileHeader), sizeof(fileHeader));
  file.write(reinterpret_cast<const uint8_t*>(&infoHeader), sizeof(infoHeader));
  file.write(reinterpret_cast<const uint8_t*>(palette), sizeof(palette));

  // BMP stores rows bottom-up; the framebuffer is top-down (row 0 = the physical top of the
  // screen, and this orientation's logical/physical coordinates are identical — see
  // GfxRenderer::rotateCoordinates' LandscapeCounterClockwise case) — so write rows in reverse.
  uint8_t rowBuf[kMaxRowBytes] = {};
  for (int y = height - 1; y >= 0; y--) {
    const uint8_t* srcRow = src + static_cast<size_t>(y) * static_cast<size_t>(srcStride);
    memcpy(rowBuf, srcRow, static_cast<size_t>(srcStride));
    if (dstStride > srcStride) memset(rowBuf + srcStride, 0, static_cast<size_t>(dstStride - srcStride));
    file.write(rowBuf, static_cast<size_t>(dstStride));
  }
  file.close();

  LOG_INF("SCREENSHOT", "Saved %s (%dx%d)", path, width, height);
  return true;
}
