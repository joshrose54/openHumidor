// Rev Q: the image's openHumidor identity block (spec 5.5; lib/core image_identity.h). Plain
// constant data in ESP-IDF's ".rodata_custom_desc" section, which the framework's linker script
// puts straight after the 256-byte app description: byte 0x120 of the image. The OTA check (the
// board's, and the Update page's own early copy, web/image_check.js) reads it from there.
//
// tools/check_identity.py (run after every board build) fails the build unless it is there and
// says the right thing; firmware::identity() reads it at boot, which also keeps the linker from
// dropping it (the section isn't marked "keep").
#include "identity.h"

#if defined(WINEADOR_BOARD_REVQ)
#include "board.h"
#include "wineador/build_info.h"
#include "wineador/web_api.h"

namespace {
constexpr wineador::ImageIdentityBlock kBlock =
    wineador::identityBlockFor(board::kIdentityBoard, wineador::kBuildProfile == wineador::Profile::Bench,
                               wineador::kTestBuild, wineador::kRollbackTest, board::kFirmwareVersion,
                               (uint16_t)wineador::kApiVersion);
}  // namespace

extern "C" {
__attribute__((section(".rodata_custom_desc"), used))
const wineador::ImageIdentityBlock wineador_image_identity = kBlock;
}

namespace firmware {
const wineador::ImageIdentityBlock& identity() {
  // read through a volatile pointer: the block is really read from flash, so it stays in the image
  const volatile wineador::ImageIdentityBlock* p = &wineador_image_identity;
  return *const_cast<const wineador::ImageIdentityBlock*>(p);
}
bool hasIdentity() { return true; }
}  // namespace firmware

#else   // Rev N: no identity block (spec 5.5: Rev Q only; a Rev N image is refused by its chip ID)

namespace firmware {
const wineador::ImageIdentityBlock& identity() {
  static const wineador::ImageIdentityBlock none{};
  return none;
}
bool hasIdentity() { return false; }
}  // namespace firmware

#endif
