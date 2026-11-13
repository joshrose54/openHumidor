#include "wineador/json_out.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace wineador {

void JsonWriter::setBuffer(char* buf, size_t n) {
  buf_ = buf;
  n_ = n;
  len_ = 0;
}

void JsonWriter::raw(const char* s, size_t n) {
  if (len_ + n > n_) { overflow_ = true; return; }
  memcpy(buf_ + len_, s, n);
  len_ += n;
}

void JsonWriter::beforeValue() {
  if (afterKey_) { afterKey_ = false; return; }
  if (depth_ > 0) {
    if (!first_[depth_ - 1]) rawc(',');
    first_[depth_ - 1] = false;
  }
}

void JsonWriter::beginObject() {
  beforeValue();
  rawc('{');
  if (depth_ < kMaxDepth) first_[depth_] = true;
  depth_++;
}

void JsonWriter::endObject() {
  rawc('}');
  if (depth_ > 0) depth_--;
}

void JsonWriter::beginArray() {
  beforeValue();
  rawc('[');
  if (depth_ < kMaxDepth) first_[depth_] = true;
  depth_++;
}

void JsonWriter::endArray() {
  rawc(']');
  if (depth_ > 0) depth_--;
}

void JsonWriter::key(const char* k) {
  beforeValue();
  quoted(k ? k : "");
  rawc(':');
  afterKey_ = true;
}

void JsonWriter::str(const char* s) {
  if (!s) { null(); return; }
  beforeValue();
  quoted(s);
}

void JsonWriter::quoted(const char* s) {
  rawc('"');
  for (const unsigned char* c = (const unsigned char*)s; *c; c++) {
    switch (*c) {
      case '"':  raw("\\\"", 2); break;
      case '\\': raw("\\\\", 2); break;
      case '\n': raw("\\n", 2); break;
      case '\r': raw("\\r", 2); break;
      case '\t': raw("\\t", 2); break;
      default:
        if (*c < 0x20) {
          char e[8];
          snprintf(e, sizeof(e), "\\u%04x", (unsigned)*c);
          raw(e, 6);
        } else {
          rawc((char)*c);   // UTF-8 passes through unchanged
        }
    }
  }
  rawc('"');
}

void JsonWriter::num(double v, int decimals) {
  if (isnan(v) || isinf(v)) { null(); return; }
  beforeValue();
  char t[40];
  if (decimals < 0) decimals = 0;
  if (decimals > 6) decimals = 6;
  int k = snprintf(t, sizeof(t), "%.*f", decimals, v);
  if (k < 0 || k >= (int)sizeof(t)) { raw("null", 4); return; }
  if (decimals > 0) {                       // drop trailing zeros: 20.50 -> 20.5, 20.00 -> 20
    while (k > 0 && t[k - 1] == '0') k--;
    if (k > 0 && t[k - 1] == '.') k--;
  }
  if (k == 2 && t[0] == '-' && t[1] == '0') { t[0] = '0'; k = 1; }   // -0 -> 0
  raw(t, (size_t)k);
}

void JsonWriter::integer(int64_t v) {
  beforeValue();
  char t[24];
  const int k = snprintf(t, sizeof(t), "%lld", (long long)v);
  raw(t, (size_t)k);
}

void JsonWriter::boolean(bool b) {
  beforeValue();
  if (b) raw("true", 4);
  else   raw("false", 5);
}

void JsonWriter::null() {
  beforeValue();
  raw("null", 4);
}

size_t JsonPump::fill(char* buf, size_t n) {
  if (done_ || n <= maxStep()) return 0;
  w_.setBuffer(buf, n);
  while (w_.room() > maxStep()) {
    if (!step(w_)) { done_ = true; break; }
  }
  total_ += w_.length();
  return w_.length();
}

int pumpAll(JsonPump& p, char* out, size_t n) {
  size_t len = 0;
  while (!p.done()) {
    if (n - len <= p.maxStep() + 1) return -1;
    const size_t k = p.fill(out + len, n - len - 1);
    len += k;
    if (k == 0 && !p.done()) return -1;
  }
  out[len] = 0;
  return (int)len;
}

}  // namespace wineador
