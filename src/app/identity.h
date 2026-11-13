#pragma once
// The image's own identity block (Rev Q; src/app/identity.cpp). Rev N: none.
#include "wineador/image_identity.h"

namespace firmware {
const wineador::ImageIdentityBlock& identity();
bool hasIdentity();
}  // namespace firmware
