#include "wineador/json_in.h"
#include <string.h>
#include <stdlib.h>

namespace wineador {

const char* jsonErrorText(FlatJson::Error e) {
  switch (e) {
    case FlatJson::Error::None:          return "ok";
    case FlatJson::Error::Empty:         return "the body is empty";
    case FlatJson::Error::TooLong:       return "the body is too long";
    case FlatJson::Error::NotAnObject:   return "the body is not a JSON object";
    case FlatJson::Error::Syntax:        return "the body is not valid JSON";
    case FlatJson::Error::Nested:        return "objects and arrays are not accepted as values";
    case FlatJson::Error::TooManyFields: return "too many fields";
    case FlatJson::Error::KeyTooLong:    return "a field name is too long";
    case FlatJson::Error::StringTooLong: return "a text value is too long";
    case FlatJson::Error::DuplicateKey:  return "a field is given twice";
  }
  return "?";
}

void FlatJson::ws() {
  while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) p_++;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// A JSON string at p_ (on the opening quote), into out as UTF-8.
bool FlatJson::string(char* out, size_t n, Error tooLong) {
  if (p_ >= end_ || *p_ != '"') return fail(Error::Syntax);
  p_++;
  size_t len = 0;
  auto put = [&](char c) -> bool {
    if (len + 1 >= n) return false;
    out[len++] = c;
    return true;
  };
  while (p_ < end_ && *p_ != '"') {
    unsigned char c = (unsigned char)*p_++;
    if (c < 0x20) return fail(Error::Syntax);
    if (c != '\\') {
      if (!put((char)c)) return fail(tooLong);
      continue;
    }
    if (p_ >= end_) return fail(Error::Syntax);
    const char e = *p_++;
    char simple = 0;
    switch (e) {
      case '"': simple = '"'; break;
      case '\\': simple = '\\'; break;
      case '/': simple = '/'; break;
      case 'b': simple = '\b'; break;
      case 'f': simple = '\f'; break;
      case 'n': simple = '\n'; break;
      case 'r': simple = '\r'; break;
      case 't': simple = '\t'; break;
      case 'u': {
        if (end_ - p_ < 4) return fail(Error::Syntax);
        uint32_t cp = 0;
        for (int i = 0; i < 4; i++) {
          const int h = hexVal(p_[i]);
          if (h < 0) return fail(Error::Syntax);
          cp = cp * 16 + (uint32_t)h;
        }
        p_ += 4;
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';   // surrogate pairs: not needed here
        bool ok;
        if (cp < 0x80) ok = put((char)cp);
        else if (cp < 0x800) ok = put((char)(0xC0 | (cp >> 6))) && put((char)(0x80 | (cp & 0x3F)));
        else ok = put((char)(0xE0 | (cp >> 12))) && put((char)(0x80 | ((cp >> 6) & 0x3F))) &&
                  put((char)(0x80 | (cp & 0x3F)));
        if (!ok) return fail(tooLong);
        continue;
      }
      default: return fail(Error::Syntax);
    }
    if (!put(simple)) return fail(tooLong);
  }
  if (p_ >= end_) return fail(Error::Syntax);
  p_++;   // closing quote
  out[len] = 0;
  return true;
}

bool FlatJson::value(JsonField& f) {
  if (p_ >= end_) return fail(Error::Syntax);
  const char c = *p_;
  if (c == '"') {
    f.kind = JsonField::Kind::String;
    return string(f.text, sizeof(f.text), Error::StringTooLong);
  }
  if (c == '{' || c == '[') return fail(Error::Nested);
  auto word = [&](const char* w) {
    const size_t k = strlen(w);
    if ((size_t)(end_ - p_) < k || strncmp(p_, w, k) != 0) return false;
    p_ += k;
    return true;
  };
  if (word("true"))  { f.kind = JsonField::Kind::Bool; f.boolean = true; return true; }
  if (word("false")) { f.kind = JsonField::Kind::Bool; f.boolean = false; return true; }
  if (word("null"))  { f.kind = JsonField::Kind::Null; return true; }
  // A number, strictly: -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?
  const char* s = p_;
  const char* q = p_;
  if (q < end_ && *q == '-') q++;
  if (q >= end_) return fail(Error::Syntax);
  if (*q == '0') q++;
  else if (*q >= '1' && *q <= '9') { while (q < end_ && *q >= '0' && *q <= '9') q++; }
  else return fail(Error::Syntax);
  if (q < end_ && *q == '.') {
    q++;
    if (q >= end_ || *q < '0' || *q > '9') return fail(Error::Syntax);
    while (q < end_ && *q >= '0' && *q <= '9') q++;
  }
  if (q < end_ && (*q == 'e' || *q == 'E')) {
    q++;
    if (q < end_ && (*q == '+' || *q == '-')) q++;
    if (q >= end_ || *q < '0' || *q > '9') return fail(Error::Syntax);
    while (q < end_ && *q >= '0' && *q <= '9') q++;
  }
  char t[40];
  const size_t k = (size_t)(q - s);
  if (k >= sizeof(t)) return fail(Error::Syntax);
  memcpy(t, s, k);
  t[k] = 0;
  f.kind = JsonField::Kind::Number;
  f.number = strtod(t, nullptr);
  p_ = q;
  return true;
}

bool FlatJson::parse(const char* body, size_t n) {
  count_ = 0;
  err_ = Error::None;
  if (n > kMaxJsonBody) return fail(Error::TooLong);
  p_ = body;
  end_ = body + n;
  ws();
  if (p_ >= end_) return fail(Error::Empty);
  if (*p_ != '{') return fail(Error::NotAnObject);
  p_++;
  ws();
  if (p_ < end_ && *p_ == '}') {
    p_++;
  } else {
    for (;;) {
      ws();
      if (count_ >= kMaxFields) return fail(Error::TooManyFields);
      JsonField& f = f_[count_];
      f = JsonField{};
      if (!string(f.key, sizeof(f.key), Error::KeyTooLong)) return false;
      for (int i = 0; i < count_; i++)
        if (strcmp(f_[i].key, f.key) == 0) return fail(Error::DuplicateKey);
      ws();
      if (p_ >= end_ || *p_ != ':') return fail(Error::Syntax);
      p_++;
      ws();
      if (!value(f)) return false;
      count_++;
      ws();
      if (p_ < end_ && *p_ == ',') { p_++; continue; }
      if (p_ < end_ && *p_ == '}') { p_++; break; }
      return fail(Error::Syntax);
    }
  }
  ws();
  if (p_ != end_) return fail(Error::Syntax);   // nothing after the object
  return true;
}

const JsonField* FlatJson::find(const char* key) const {
  for (int i = 0; i < count_; i++)
    if (strcmp(f_[i].key, key) == 0) return &f_[i];
  return nullptr;
}

}  // namespace wineador
