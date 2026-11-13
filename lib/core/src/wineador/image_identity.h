#pragma once
// Which build a firmware image is, read from its first bytes before anything is written
// (spec 5.2, 5.5): OTA accepts only a Rev Q cabinet build (real profile, not a test build).
//
// An ESP32 app image starts with a 24-byte image header (byte 0 = 0xE9; bytes 12-13 = the chip:
// 0 = ESP32, as on Rev N; 9 = ESP32-S3, as on Rev Q), then the first segment's 8-byte header,
// then the framework's 256-byte app description (magic 0xABCD5432). Its project name and version
// are the same in every one of our builds (the prebuilt framework's), so they can't tell a bench
// build from a cabinet build. ESP-IDF lets an app put its own block straight after the app
// description (the ".rodata_custom_desc" section): that is where every build from stage Q5 on
// carries this 64-byte identity block, at byte 0x120 of the image. The firmware places it in
// Q5d; this file defines its layout and reads it. Only a positive match is accepted: an image
// with no block (a BP2 sketch, or firmware from before Q5) is refused.

#include <stdint.h>
#include <stddef.h>

namespace wineador {

constexpr uint8_t  kImageMagic = 0xE9;
constexpr uint16_t kChipEsp32 = 0;              // Rev N
constexpr uint16_t kChipEsp32S3 = 9;            // Rev Q
constexpr size_t   kAppDescOffset = 0x20;       // 24 (image header) + 8 (segment header)
constexpr uint32_t kAppDescMagic = 0xABCD5432;
constexpr size_t   kIdentityOffset = 0x120;     // kAppDescOffset + 256 (the app description)
constexpr size_t   kIdentityBytes = 64;
constexpr size_t   kImageCheckBytes = kIdentityOffset + kIdentityBytes;   // 352: what the check needs
constexpr uint8_t  kIdentityLayout = 1;
#define WINEADOR_IDENTITY_MAGIC "openHumidor-ID"  // 14 characters + NUL, in a 16-byte field

// The block as it sits in the image. Every field is bytes, so the layout is the same on the
// Mac and the board; offsets: magic 0, board 16, profile 24, test 32, layout 33, api 34 (two
// bytes, little-endian), version 36.
struct ImageIdentityBlock {
  char magic[16];      // WINEADOR_IDENTITY_MAGIC
  char board[8];       // "rev-q" / "rev-n"
  char profile[8];     // "real" / "bench"
  uint8_t testBuild;   // 1 = a test build
  uint8_t layout;      // kIdentityLayout
  uint8_t apiLo, apiHi;   // the API version
  char version[28];    // "2.0.0", "2.0.0-BENCH-TEST"
};
static_assert(sizeof(ImageIdentityBlock) == kIdentityBytes, "the identity block is 64 bytes");

// What an image is, as read from its first bytes.
struct ImageFacts {
  size_t bytes = 0;              // how many bytes were looked at
  bool isImage = false;          // byte 0 is 0xE9
  uint16_t chip = 0xFFFF;
  bool hasAppDesc = false;
  bool hasIdentity = false;
  char board[8] = "";
  char profile[8] = "";
  bool testBuild = false;
  uint8_t layout = 0;
  uint16_t api = 0;
  char version[28] = "";
};

enum class ImageVerdict : uint8_t {
  Accept,          // a Rev Q cabinet build
  TooShort,        // fewer than kImageCheckBytes
  NotFirmware,     // not an ESP32 app image at all
  RevNImage,       // chip ESP32: a Rev N build
  WrongChip,       // another chip
  NoAppDesc,       // an image, but not an app built by the framework
  NoIdentity,      // no identity block: a BP2 sketch, or firmware from before Q5
  UnknownLayout,   // a block from a newer firmware this one can't read
  WrongBoard,      // a block naming another board
  BenchBuild,      // the bench profile (short timers): never for the cabinet
  TestBuild,       // a test build (scripted readings)
};

// Reads the first `n` bytes of an image.
ImageFacts readImageFacts(const uint8_t* first, size_t n);
ImageVerdict judgeImage(const ImageFacts& f);
inline ImageVerdict checkImage(const uint8_t* first, size_t n) { return judgeImage(readImageFacts(first, n)); }
const char* imageVerdictCode(ImageVerdict v);   // "bench_build"
const char* imageVerdictText(ImageVerdict v);   // "That is a bench build (short timers): never for the cabinet."

// Fills a block (the firmware's own, Q5d; and the tests' made-up images).
void makeIdentityBlock(ImageIdentityBlock* b, const char* board, bool bench, bool testBuild, const char* version,
                       uint16_t api);

}  // namespace wineador
