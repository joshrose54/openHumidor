#pragma once
// For the native tests: is a text one valid JSON value (RFC 8259)? And does a JsonPump write the
// same reply when sent in small pieces as when written whole? Included by the web test suites.
#include <string.h>
#include <stdlib.h>
#include <string>
#include "wineador/json_out.h"

namespace jsoncheck {

struct P {
  const char* s;
  const char* e;
  void ws() { while (s < e && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t')) s++; }
  bool lit(const char* w) {
    const size_t k = strlen(w);
    if ((size_t)(e - s) < k || strncmp(s, w, k) != 0) return false;
    s += k;
    return true;
  }
  bool str() {
    if (s >= e || *s != '"') return false;
    s++;
    while (s < e && *s != '"') {
      if ((unsigned char)*s < 0x20) return false;
      if (*s == '\\') {
        s++;
        if (s >= e) return false;
        if (*s == 'u') { if (e - s < 5) return false; s += 5; continue; }
        if (!strchr("\"\\/bfnrt", *s)) return false;
      }
      s++;
    }
    if (s >= e) return false;
    s++;
    return true;
  }
  bool num() {
    const char* b = s;
    if (s < e && *s == '-') s++;
    if (s >= e || *s < '0' || *s > '9') return false;
    if (*s == '0') s++;
    else while (s < e && *s >= '0' && *s <= '9') s++;
    if (s < e && *s == '.') { s++; if (s >= e || *s < '0' || *s > '9') return false; while (s < e && *s >= '0' && *s <= '9') s++; }
    if (s < e && (*s == 'e' || *s == 'E')) { s++; if (s < e && (*s == '+' || *s == '-')) s++; while (s < e && *s >= '0' && *s <= '9') s++; }
    return s > b;
  }
  bool value(int depth) {
    if (depth > 20) return false;
    ws();
    if (s >= e) return false;
    if (*s == '{') {
      s++; ws();
      if (s < e && *s == '}') { s++; return true; }
      for (;;) {
        ws();
        if (!str()) return false;
        ws();
        if (s >= e || *s != ':') return false;
        s++;
        if (!value(depth + 1)) return false;
        ws();
        if (s < e && *s == ',') { s++; continue; }
        if (s < e && *s == '}') { s++; return true; }
        return false;
      }
    }
    if (*s == '[') {
      s++; ws();
      if (s < e && *s == ']') { s++; return true; }
      for (;;) {
        if (!value(depth + 1)) return false;
        ws();
        if (s < e && *s == ',') { s++; continue; }
        if (s < e && *s == ']') { s++; return true; }
        return false;
      }
    }
    if (*s == '"') return str();
    if (lit("true") || lit("false") || lit("null")) return true;
    return num();
  }
};

inline bool valid(const char* text, size_t n) {
  P p{text, text + n};
  if (!p.value(0)) return false;
  p.ws();
  return p.s == p.e;
}
inline bool valid(const std::string& t) { return valid(t.data(), t.size()); }

// Runs a pump with pieces of `piece` bytes and joins them (checks every piece is within its buffer).
template <class Pump>
std::string inPieces(Pump& p, size_t piece) {
  std::string out;
  char* buf = (char*)malloc(piece);
  for (int guard = 0; guard < 100000 && !p.done(); guard++) {
    const size_t k = p.fill(buf, piece);
    if (k > piece) { out = "<overrun>"; break; }
    out.append(buf, k);
    if (k == 0 && !p.done()) { out = "<stuck>"; break; }
  }
  free(buf);
  return out;
}

// The whole reply in one buffer.
template <class Pump>
std::string whole(Pump& p) {
  static char big[1 << 20];
  const int n = wineador::pumpAll(p, big, sizeof(big));
  return n < 0 ? std::string("<too big>") : std::string(big, (size_t)n);
}

// How often `needle` appears.
inline int count(const std::string& h, const char* needle) {
  int c = 0;
  for (size_t at = h.find(needle); at != std::string::npos; at = h.find(needle, at + 1)) c++;
  return c;
}

}  // namespace jsoncheck
