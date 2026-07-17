#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// Byte-stream abstraction `Bitmap` reads through — just the handful of HalFile operations BMP
// parsing/row-reading actually needs (sequential + absolute seeks, single-byte and buffered
// reads). Letting `Bitmap` work against this instead of `HalFile` directly means the exact same
// BMP-parsing/dithering/scaling pipeline can read art embedded in flash (see `MemorySource`)
// as well as art on the SD card (see `HalFileSource`), with no duplicated logic.
class BitmapSource {
 public:
  virtual ~BitmapSource() = default;
  virtual int read(void* buf, size_t count) = 0;
  virtual int read() = 0;
  virtual bool seek(size_t pos) = 0;
  virtual bool seekCur(int64_t offset) = 0;
  virtual explicit operator bool() const = 0;
};

// Adapts an already-open SD-card HalFile to BitmapSource. Thin delegation, no ownership.
class HalFileSource final : public BitmapSource {
 public:
  explicit HalFileSource(HalFile& file) : file(file) {}
  int read(void* buf, size_t count) override { return file.read(buf, count); }
  int read() override { return file.read(); }
  bool seek(size_t pos) override { return file.seek(pos); }
  bool seekCur(int64_t offset) override { return file.seekCur(offset); }
  explicit operator bool() const override { return static_cast<bool>(file); }

 private:
  HalFile& file;
};

// Reads a BMP already resident in flash/RAM (e.g. a `static const uint8_t[]` baked into the
// firmware image) instead of the SD card — same byte-stream shape, no filesystem involved.
class MemorySource final : public BitmapSource {
 public:
  MemorySource(const uint8_t* data, size_t size) : data(data), size(size) {}

  int read(void* buf, size_t count) override {
    if (!data || pos >= size) return 0;
    const size_t n = count < (size - pos) ? count : (size - pos);
    memcpy(buf, data + pos, n);
    pos += n;
    return static_cast<int>(n);
  }

  int read() override {
    if (!data || pos >= size) return -1;
    return data[pos++];
  }

  bool seek(size_t newPos) override {
    if (newPos > size) return false;
    pos = newPos;
    return true;
  }

  bool seekCur(int64_t offset) override {
    const int64_t target = static_cast<int64_t>(pos) + offset;
    if (target < 0 || static_cast<size_t>(target) > size) return false;
    pos = static_cast<size_t>(target);
    return true;
  }

  explicit operator bool() const override { return data != nullptr; }

 private:
  const uint8_t* data;
  size_t size;
  size_t pos = 0;
};
