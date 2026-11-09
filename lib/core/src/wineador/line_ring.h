#pragma once
// A first-in, first-out store of text lines in a fixed block of bytes (spec 5.4:
// the SD logging's outage buffers). Each line is kept at its real length. It holds
// at most `maxLines` lines and `bytes` bytes; pushing past either drops the OLDEST
// lines, and the drops are counted. Plain C++, no allocation after construction.

#include <stddef.h>
#include <stdint.h>

namespace wineador {

class LineRing {
 public:
  LineRing(char* storage, size_t bytes, uint16_t maxLines);

  // Adds a line (without its terminator; `len` bytes). Drops the oldest lines to
  // make room. False (nothing changed) if the line can never fit.
  bool push(const char* line, size_t len);

  uint16_t count() const { return count_; }
  size_t bytesUsed() const { return used_; }
  uint32_t dropped() const { return dropped_; }
  bool empty() const { return count_ == 0; }

  // Copies the i-th oldest line (0 = oldest) into out, NUL-terminated (cut to n-1).
  // Returns its full length, or 0 if there is no such line.
  size_t peek(uint16_t i, char* out, size_t n) const;
  // Removes the oldest n lines (they were written).
  void pop(uint16_t n);
  void clear();

 private:
  static constexpr size_t kHdr = 2;              // each line: 2-byte length, then the bytes
  uint8_t at(size_t pos) const { return (uint8_t)buf_[pos % cap_]; }
  size_t lineLen(size_t pos) const { return (size_t)at(pos) | ((size_t)at(pos + 1) << 8); }
  void dropOldest();

  char* buf_;
  size_t cap_;
  uint16_t maxLines_;
  size_t head_ = 0;      // where the oldest line starts
  size_t used_ = 0;      // bytes in use, headers included
  uint16_t count_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace wineador
