#include "wineador/commands.h"
#include "wineador/sensor_table.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

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

namespace {

constexpr int kWords = 5;
constexpr size_t kWordLen = 16;

// Splits into lower-case words. Returns the count (a word too long makes it kWords + 1).
int split(const char* line, char w[kWords][kWordLen]) {
  int n = 0;
  while (*line) {
    while (*line == ' ' || *line == '\t') line++;
    if (!*line) break;
    if (n == kWords) return kWords + 1;
    size_t k = 0;
    while (*line && *line != ' ' && *line != '\t') {
      if (k == kWordLen - 1) return kWords + 1;
      w[n][k++] = (char)tolower((unsigned char)*line++);
    }
    w[n][k] = 0;
    n++;
  }
  return n;
}

// Whole word is an integer 0..9999.
bool toInt(const char* s, int* v) {
  if (!*s || strlen(s) > 4) return false;
  int x = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') return false;
    x = x * 10 + (*s - '0');
  }
  *v = x;
  return true;
}

// Whole word is a plain decimal number ("19", "19.5", "-3").
bool toFloat(const char* s, float* v) {
  if (!*s) return false;
  char* end = nullptr;
  const float x = strtof(s, &end);
  if (!end || *end) return false;
  *v = x;
  return true;
}

int onOff(const char* s) {
  if (strcmp(s, "on") == 0) return 1;
  if (strcmp(s, "off") == 0) return 0;
  return -2;
}

// Case-insensitive whole-word match (the words are already lower case; the table's labels aren't).
bool sameWord(const char* word, const char* name) {
  for (; *word && *name; word++, name++)
    if (tolower((unsigned char)*word) != tolower((unsigned char)*name)) return false;
  return *word == 0 && *name == 0;
}

}  // namespace

// Spec 5.5, serial console: a sensor channel is the number 0-5 ("3" or "ch3"), the
// position name ("mid_rear") or the probe label ("a1c5", any case). -2 otherwise.
int channelWord(const char* s) {
  for (int i = 0; i < kNumSensors; i++)
    if (sameWord(s, kSensors[i].position) || sameWord(s, kSensors[i].label)) return i;
  if (s[0] == 'c' && s[1] == 'h') s += 2;
  int v;
  if (!toInt(s, &v) || v < 0 || v > 5) return -2;
  return v;
}

int formatChannelEcho(char* out, size_t n, int channel, const char* what) {
  if (channel < 0 || channel >= kNumSensors) return snprintf(out, n, "no such channel: %s", what);
  return snprintf(out, n, "ch%d = %s (probe %s): %s", channel, kSensors[channel].position, kSensors[channel].label, what);
}

namespace {

}  // namespace

Command parseCommand(const char* line, bool testBuild, CommandArgs& a) {
  a = CommandArgs{};
  char w[kWords][kWordLen];
  const int n = split(line, w);
  if (n == 0) return Command::None;
  if (n > kWords) return Command::Unknown;
  const char* c = w[0];

  // ---- test builds only
  if (strcmp(c, "run") == 0) {
    if (!testBuild) return Command::TestOnly;
    int v;
    if (n == 2 && strcmp(w[1], "soak") == 0) a.arg = kSoakScenario;
    else a.arg = (n == 2 && toInt(w[1], &v)) ? v : -1;
    return Command::Run;
  }
  if (strcmp(c, "resets") == 0) {
    if (!testBuild) return Command::TestOnly;
    if (n == 1) return Command::Resets;
    return (n == 2 && strcmp(w[1], "clear") == 0) ? Command::ResetsClear : Command::Unknown;
  }
  if (strcmp(c, "fan") == 0) {
    if (!testBuild) return Command::TestOnly;
    int v;
    a.arg = -2;
    if (n == 2 && strcmp(w[1], "auto") == 0) a.arg = -1;
    else if (n == 2 && toInt(w[1], &v)) a.arg = v;
    return Command::Fan;
  }
  if (strcmp(c, "humidcool") == 0) {
    if (!testBuild) return Command::TestOnly;
    a.arg = (n == 2) ? onOff(w[1]) : -2;
    return Command::HumidCool;
  }
  if (strcmp(c, "crc") == 0 || strcmp(c, "timeout") == 0) {
    if (!testBuild) return Command::TestOnly;
    a.arg = (n == 2) ? channelWord(w[1]) : -2;
    return c[0] == 'c' ? Command::InjectCrc : Command::InjectTimeout;
  }
  if (strcmp(c, "sdstress") == 0) {
    if (!testBuild) return Command::TestOnly;
    if (n == 2 && strcmp(w[1], "stop") == 0) { a.arg = -1; return Command::SdStress; }
    int khz, writes;
    const int retry = (n == 4) ? (strcmp(w[2], "retry") == 0 ? 1 : strcmp(w[2], "noretry") == 0 ? 0 : -1) : -1;
    if (n == 4 && toInt(w[1], &khz) && khz >= 400 && khz <= 9999 && retry >= 0 && toInt(w[3], &writes) && writes > 0) {
      a.arg = khz;
      a.arg2 = retry;
      a.arg3 = writes;
    } else {
      a.arg = -2;
    }
    return Command::SdStress;
  }
  if (strcmp(c, "stop") == 0 && n == 1) return testBuild ? Command::Stop : Command::TestOnly;
  if (strcmp(c, "ntpsync") == 0 && n == 1) return testBuild ? Command::NtpSync : Command::TestOnly;
  if (strcmp(c, "stall") == 0 && n == 1) return testBuild ? Command::Stall : Command::StallNotAllowed;

  // ---- every build
  if (strcmp(c, "events") == 0) {
    int v;
    if (n == 1) return Command::Events;
    a.arg = (n == 2 && toInt(w[1], &v)) ? v : -2;
    return n == 2 ? Command::Events : Command::Unknown;
  }
  if (strcmp(c, "set") == 0) {
    if (n == 2 && strcmp(w[1], "defaults") == 0) return Command::SetDefaults;
    if (n != 3) return Command::Unknown;
    if (strcmp(w[1], "temp") == 0 || strcmp(w[1], "rh") == 0) {
      if (!toFloat(w[2], &a.f)) a.arg = -2;
      return w[1][0] == 't' ? Command::SetTemp : Command::SetRh;
    }
    if (strcmp(w[1], "heater") == 0) { a.arg = onOff(w[2]); return Command::SetHeater; }
    if (strcmp(w[1], "humid") == 0)  { a.arg = onOff(w[2]); return Command::SetHumid; }
    return Command::Unknown;
  }
  if (strcmp(c, "exclude") == 0) {
    if (n != 3) { a.arg = -2; return Command::Exclude; }
    a.arg = channelWord(w[1]);
    a.arg2 = onOff(w[2]);
    if (a.arg2 < 0) a.arg = -2;
    return Command::Exclude;
  }
  if (n != 1) return Command::Unknown;   // the rest take no words after them
  if (strcmp(c, "boot") == 0) return Command::Boot;
  if (strcmp(c, "help") == 0) return Command::Help;
  if (strcmp(c, "exercise") == 0) return Command::Exercise;
  if (strcmp(c, "status") == 0) return Command::Status;
  if (strcmp(c, "sensors") == 0) return Command::Sensors;
  if (strcmp(c, "log") == 0) return Command::Log;
  if (strcmp(c, "settings") == 0) return Command::Settings;
  return Command::Unknown;
}

Command parseCommand(const char* line, bool testBuild, int* arg) {
  CommandArgs a;
  const Command c = parseCommand(line, testBuild, a);
  if (arg) *arg = a.arg;
  return c;
}

}  // namespace wineador
