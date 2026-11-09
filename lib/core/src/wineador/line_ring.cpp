#include "wineador/line_ring.h"

namespace wineador {

LineRing::LineRing(char* storage, size_t bytes, uint16_t maxLines)
    : buf_(storage), cap_(bytes), maxLines_(maxLines) {}

void LineRing::clear() {
  head_ = used_ = 0;
  count_ = 0;
}

void LineRing::dropOldest() {
  if (count_ == 0) return;
  const size_t step = kHdr + lineLen(head_);
  head_ = (head_ + step) % cap_;
  used_ -= step;
  count_--;
  dropped_++;
}

bool LineRing::push(const char* line, size_t len) {
  const size_t need = kHdr + len;
  if (need > cap_ || len > 0xFFFF || maxLines_ == 0) return false;
  while (count_ > 0 && (used_ + need > cap_ || count_ >= maxLines_)) dropOldest();
  size_t pos = (head_ + used_) % cap_;
  buf_[pos] = (char)(len & 0xFF);
  buf_[(pos + 1) % cap_] = (char)((len >> 8) & 0xFF);
  pos = (pos + kHdr) % cap_;
  for (size_t k = 0; k < len; k++) buf_[(pos + k) % cap_] = line[k];
  used_ += need;
  count_++;
  return true;
}

size_t LineRing::peek(uint16_t i, char* out, size_t n) const {
  if (i >= count_ || n == 0) return 0;
  size_t pos = head_;
  for (uint16_t k = 0; k < i; k++) pos = (pos + kHdr + lineLen(pos)) % cap_;
  const size_t len = lineLen(pos);
  const size_t copy = len < n - 1 ? len : n - 1;
  for (size_t k = 0; k < copy; k++) out[k] = buf_[(pos + kHdr + k) % cap_];
  out[copy] = 0;
  return len;
}

void LineRing::pop(uint16_t n) {
  while (n-- > 0 && count_ > 0) {
    const size_t step = kHdr + lineLen(head_);
    head_ = (head_ + step) % cap_;
    used_ -= step;
    count_--;
  }
}

}  // namespace wineador
