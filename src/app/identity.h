#pragma once
// The image's own identity block (src/app/identity.cpp).
#include "wineador/image_identity.h"

namespace firmware {
const wineador::ImageIdentityBlock& identity();
bool hasIdentity();
}  // namespace firmware
