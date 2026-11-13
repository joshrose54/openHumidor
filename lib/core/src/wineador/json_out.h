#pragma once
// Writing JSON for the dashboard's API (spec 5.5, docs/api.md), in pieces.
//
// The board sends a reply a few kilobytes at a time: it never builds a whole month of graph
// data in one buffer. So the writer keeps its place (which objects and arrays are open, and
// whether the next value needs a comma) apart from the buffer it is writing into: the caller
// gives it a fresh buffer for each piece with setBuffer().
//
// A reply is a JsonPump: a small state machine that writes one step at a time (a value, an
// object, a row). fill() runs steps while there is room for the largest step the pump can
// write (maxStep()), so a step is never cut in half. Plain C++: no heap, no hardware.

#include <stdint.h>
#include <stddef.h>

namespace wineador {

class JsonWriter {
 public:
  // The piece being written. Returns to the start of `buf`.
  void setBuffer(char* buf, size_t n);
  size_t length() const { return len_; }
  size_t room() const { return n_ > len_ ? n_ - len_ : 0; }
  bool overflowed() const { return overflow_; }   // something didn't fit (a pump bug: maxStep too small)

  void beginObject();                // {
  void endObject();                  // }
  void beginArray();                 // [
  void endArray();                   // ]
  void key(const char* k);           // "k":   (inside an object; the value comes next)

  void str(const char* s);           // "…" with escapes; nullptr writes null
  void num(double v, int decimals);  // a number with up to `decimals` places, trailing zeros dropped; NaN/inf = null
  void integer(int64_t v);
  void boolean(bool b);
  void null();

  // key + value in one call
  void kStr(const char* k, const char* s) { key(k); str(s); }
  void kNum(const char* k, double v, int decimals) { key(k); num(v, decimals); }
  void kInt(const char* k, int64_t v) { key(k); integer(v); }
  void kBool(const char* k, bool b) { key(k); boolean(b); }
  void kNull(const char* k) { key(k); null(); }

  int depth() const { return depth_; }

 private:
  void raw(const char* s, size_t n);
  void rawc(char c) { raw(&c, 1); }
  void beforeValue();
  void quoted(const char* s);        // "…" with escapes, no comma handling

  char* buf_ = nullptr;
  size_t n_ = 0, len_ = 0;
  bool overflow_ = false;
  static constexpr int kMaxDepth = 8;
  bool first_[kMaxDepth] = {};   // no value written yet at this level
  int depth_ = 0;
  bool afterKey_ = false;
};

// A reply written step by step. The firmware and the Mac review server call fill() until done().
class JsonPump {
 public:
  virtual ~JsonPump() {}
  // Writes as many whole steps as fit into buf (n must be larger than maxStep()). Returns the
  // bytes written; 0 once the reply is complete.
  size_t fill(char* buf, size_t n);
  bool done() const { return done_; }
  // The most a single step writes (a value, a row): fill() stops while less room is left.
  virtual size_t maxStep() const = 0;
  // Total bytes written so far.
  size_t total() const { return total_; }

 protected:
  // Writes one step. Returns false when there is nothing more to write.
  virtual bool step(JsonWriter& w) = 0;
  JsonWriter w_;

 private:
  bool done_ = false;
  size_t total_ = 0;
};

// Writes the whole reply into one buffer (tests, and small replies). Returns the length, or -1 if
// it didn't fit. Always NUL-terminated when it fits.
int pumpAll(JsonPump& p, char* out, size_t n);

}  // namespace wineador
