#include "wineador/build_info.h"
#include <stdio.h>

namespace wineador {

const char* profileName(Profile p) { return p == Profile::Bench ? "BENCH" : "REAL"; }

const char* makeVersionString(char* out, size_t n, uint8_t stage, Profile p, bool testBuild) {
  snprintf(out, n, "0.1.0-stage%u%s%s", (unsigned)stage,
           p == Profile::Bench ? "-BENCH" : "", testBuild ? "-TEST" : "");
  return out;
}

const char* busResultName(BusResult r) {
  switch (r) {
    case BusResult::NotRead:   return "not read";
    case BusResult::Ok:        return "ok";
    case BusResult::MuxFail:   return "MUX FAIL";
    case BusResult::NoAck:     return "NO ACK";
    case BusResult::ShortRead: return "SHORT READ";
    case BusResult::CrcFail:   return "CRC FAIL";
    case BusResult::BusStuck:  return "BUS STUCK";
  }
  return "?";
}

}  // namespace wineador
