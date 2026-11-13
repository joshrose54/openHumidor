#include "wineador/image_identity.h"
#include <string.h>
#include <stdio.h>

namespace wineador {

// A fixed-size text field into a NUL-terminated string (the field may fill all its bytes).
static void field(char* out, size_t outSize, const uint8_t* p, size_t len) {
  size_t k = 0;
  while (k < len && k + 1 < outSize && p[k] != 0) { out[k] = (char)p[k]; k++; }
  out[k] = 0;
}

ImageFacts readImageFacts(const uint8_t* b, size_t n) {
  ImageFacts f;
  f.bytes = n;
  if (n < 16) return f;
  f.isImage = b[0] == kImageMagic;
  f.chip = (uint16_t)(b[12] | (b[13] << 8));
  if (n < kAppDescOffset + 4) return f;
  const uint32_t m = (uint32_t)b[kAppDescOffset] | ((uint32_t)b[kAppDescOffset + 1] << 8) |
                     ((uint32_t)b[kAppDescOffset + 2] << 16) | ((uint32_t)b[kAppDescOffset + 3] << 24);
  f.hasAppDesc = m == kAppDescMagic;
  if (n < kImageCheckBytes) return f;
  const uint8_t* id = b + kIdentityOffset;
  const char* magic = WINEADOR_IDENTITY_MAGIC;
  f.hasIdentity = memcmp(id, magic, strlen(magic) + 1) == 0;
  if (!f.hasIdentity) return f;
  field(f.board, sizeof(f.board), id + 16, 8);
  field(f.profile, sizeof(f.profile), id + 24, 8);
  f.testBuild = id[32] != 0;
  f.layout = id[33];
  f.api = (uint16_t)(id[34] | (id[35] << 8));
  field(f.version, sizeof(f.version), id + 36, 28);
  return f;
}

ImageVerdict judgeImage(const ImageFacts& f) {
  if (f.bytes < kImageCheckBytes) return ImageVerdict::TooShort;
  if (!f.isImage) return ImageVerdict::NotFirmware;
  if (f.chip == kChipEsp32) return ImageVerdict::RevNImage;
  if (f.chip != kChipEsp32S3) return ImageVerdict::WrongChip;
  if (!f.hasAppDesc) return ImageVerdict::NoAppDesc;
  if (!f.hasIdentity) return ImageVerdict::NoIdentity;
  if (f.layout != kIdentityLayout) return ImageVerdict::UnknownLayout;
  if (strcmp(f.board, "rev-q") != 0) return ImageVerdict::WrongBoard;
  if (strcmp(f.profile, "real") != 0) return ImageVerdict::BenchBuild;
  if (f.testBuild) return ImageVerdict::TestBuild;
  return ImageVerdict::Accept;
}

const char* imageVerdictCode(ImageVerdict v) {
  switch (v) {
    case ImageVerdict::Accept:        return "accept";
    case ImageVerdict::TooShort:      return "too_short";
    case ImageVerdict::NotFirmware:   return "not_firmware";
    case ImageVerdict::RevNImage:     return "rev_n_image";
    case ImageVerdict::WrongChip:     return "wrong_chip";
    case ImageVerdict::NoAppDesc:     return "no_app_description";
    case ImageVerdict::NoIdentity:    return "no_identity";
    case ImageVerdict::UnknownLayout: return "unknown_identity_layout";
    case ImageVerdict::WrongBoard:    return "wrong_board";
    case ImageVerdict::BenchBuild:    return "bench_build";
    case ImageVerdict::TestBuild:     return "test_build";
  }
  return "?";
}

const char* imageVerdictText(ImageVerdict v) {
  switch (v) {
    case ImageVerdict::Accept:        return "A Rev Q cabinet build.";
    case ImageVerdict::TooShort:      return "The file is too short to be a firmware image.";
    case ImageVerdict::NotFirmware:   return "That file isn't a firmware image (use firmware.bin from the revq build).";
    case ImageVerdict::RevNImage:     return "That is a Rev N image (ESP32): it can't run on this board.";
    case ImageVerdict::WrongChip:     return "That image is for another kind of chip.";
    case ImageVerdict::NoAppDesc:     return "That image isn't firmware built for this project.";
    case ImageVerdict::NoIdentity:    return "That image carries no openHumidor identity: a BP2 test sketch, or firmware from before stage Q5.";
    case ImageVerdict::UnknownLayout: return "That image's identity is in a newer layout this firmware can't read.";
    case ImageVerdict::WrongBoard:    return "That image is built for another board.";
    case ImageVerdict::BenchBuild:    return "That is a bench build (short timers): never for the cabinet.";
    case ImageVerdict::TestBuild:     return "That is a test build (scripted readings): never for the cabinet.";
  }
  return "?";
}

void makeIdentityBlock(ImageIdentityBlock* b, const char* board, bool bench, bool testBuild, const char* version,
                       uint16_t api) {
  memset(b, 0, sizeof(*b));
  memcpy(b->magic, WINEADOR_IDENTITY_MAGIC, sizeof(WINEADOR_IDENTITY_MAGIC));
  strncpy(b->board, board, sizeof(b->board) - 1);
  strncpy(b->profile, bench ? "bench" : "real", sizeof(b->profile) - 1);
  b->testBuild = testBuild ? 1 : 0;
  b->layout = kIdentityLayout;
  b->apiLo = (uint8_t)(api & 0xFF);
  b->apiHi = (uint8_t)(api >> 8);
  strncpy(b->version, version, sizeof(b->version) - 1);
}

}  // namespace wineador
