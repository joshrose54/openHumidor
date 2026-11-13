#pragma once
// Reading the small JSON bodies the dashboard sends (spec 5.5, docs/api.md): one flat object of
// names and plain values, e.g. {"target_temp_c": 20.5, "heater_enable": true}. Nested objects
// and arrays are refused: no write needs them. Strict JSON (RFC 8259) otherwise. Plain C++.

#include <stdint.h>
#include <stddef.h>

namespace wineador {

constexpr size_t kMaxJsonBody = 1024;   // the largest write body accepted (docs/api.md)

struct JsonField {
  enum class Kind : uint8_t { String, Number, Bool, Null };
  char key[32] = "";
  Kind kind = Kind::Null;
  double number = 0;
  bool boolean = false;
  char text[160] = "";       // a string value (UTF-8)
};

class FlatJson {
 public:
  enum class Error : uint8_t { None, Empty, TooLong, NotAnObject, Syntax, Nested, TooManyFields, KeyTooLong,
                               StringTooLong, DuplicateKey };
  // Parses `n` bytes (need not be NUL-terminated). False with error() set if it isn't one flat object.
  bool parse(const char* body, size_t n);
  Error error() const { return err_; }
  int count() const { return count_; }
  const JsonField& at(int i) const { return f_[i]; }
  const JsonField* find(const char* key) const;   // nullptr if absent

  static constexpr int kMaxFields = 8;

 private:
  bool fail(Error e) { err_ = e; return false; }
  void ws();
  bool string(char* out, size_t n, Error tooLong);
  bool value(JsonField& f);
  const char* p_ = nullptr;
  const char* end_ = nullptr;
  JsonField f_[kMaxFields];
  int count_ = 0;
  Error err_ = Error::None;
};

const char* jsonErrorText(FlatJson::Error e);

}  // namespace wineador
