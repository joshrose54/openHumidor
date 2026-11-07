#include "wineador/commands.h"
#include <string.h>
#include <ctype.h>

namespace wineador {

bool LineReader::feed(char c) {
  if (c == '\r' || c == '\n') {
    if (len_ == 0 && !overflow_) return false;   // blank line, or the \n of a \r\n
    buf_[len_] = 0;
    if (overflow_) line_[0] = '?', line_[1] = 0;  // too long: reported as unknown
    else memcpy(line_, buf_, len_ + 1);
    len_ = 0;
    overflow_ = false;
    return true;
  }
  if (len_ < kMax) buf_[len_++] = c;
  else overflow_ = true;
  return false;
}

Command parseCommand(const char* line, bool testBuild) {
  char w[16];
  size_t n = 0;
  while (*line == ' ' || *line == '\t') line++;
  while (*line && *line != ' ' && *line != '\t' && n < sizeof(w) - 1)
    w[n++] = (char)tolower((unsigned char)*line++);
  w[n] = 0;
  while (*line == ' ' || *line == '\t') line++;
  if (n == 0) return Command::None;
  if (*line) return Command::Unknown;   // anything after the word

  if (strcmp(w, "boot") == 0) return Command::Boot;
  if (strcmp(w, "help") == 0) return Command::Help;
  if (strcmp(w, "stall") == 0) return testBuild ? Command::Stall : Command::StallNotAllowed;
  return Command::Unknown;
}

}  // namespace wineador
