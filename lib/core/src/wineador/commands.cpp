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

Command parseCommand(const char* line, bool testBuild, int* arg) {
  if (arg) *arg = -1;
  char w[16];
  size_t n = 0;
  while (*line == ' ' || *line == '\t') line++;
  while (*line && *line != ' ' && *line != '\t' && n < sizeof(w) - 1)
    w[n++] = (char)tolower((unsigned char)*line++);
  w[n] = 0;
  while (*line == ' ' || *line == '\t') line++;
  if (n == 0) return Command::None;

  // "run <n>": the only command that takes a word after it.
  if (strcmp(w, "run") == 0) {
    if (!testBuild) return Command::TestOnly;
    int v = 0, digits = 0;
    while (*line >= '0' && *line <= '9' && digits < 4) { v = v * 10 + (*line++ - '0'); digits++; }
    while (*line == ' ' || *line == '\t') line++;
    if (arg) *arg = (digits > 0 && *line == 0) ? v : -1;
    return Command::Run;
  }
  if (*line) return Command::Unknown;   // anything after the word

  if (strcmp(w, "boot") == 0) return Command::Boot;
  if (strcmp(w, "exercise") == 0) return Command::Exercise;
  if (strcmp(w, "stop") == 0) return testBuild ? Command::Stop : Command::TestOnly;
  if (strcmp(w, "help") == 0) return Command::Help;
  if (strcmp(w, "stall") == 0) return testBuild ? Command::Stall : Command::StallNotAllowed;
  return Command::Unknown;
}

}  // namespace wineador
