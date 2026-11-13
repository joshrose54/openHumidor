#pragma once
// Free memory, measured as spec 5.2 says: the 30 KB floor is INTERNAL RAM only. On Rev Q the
// framework's ordinary calls (esp_get_free_heap_size, esp_get_minimum_free_heap_size) include
// the 2 MB of PSRAM, so they would hide a low internal heap. PSRAM is measured separately.
//
// On Rev N (no PSRAM) these give exactly the figures logged through B3 and B4:
//   free()     = ESP.getFreeHeap()                  (internal RAM)
//   lowest()   = esp_get_minimum_free_heap_size()   (data RAM; with no PSRAM it is all internal)

#include <stdint.h>
#include "esp_heap_caps.h"

namespace heap {

// Free internal RAM now.
inline uint32_t freeNow() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
// The lowest free internal (data) RAM since boot.
inline uint32_t lowest() { return (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
// Free internal data RAM now (what the SD task logs as its own figure).
inline uint32_t freeData() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
// The largest block of internal RAM that could be had now.
inline uint32_t largestBlock() { return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }   // = ESP.getMaxAllocHeap()

// PSRAM: total 0 when none was found at boot (Rev N, or a Rev Q fault).
inline uint32_t psramTotal() { return (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM); }
inline uint32_t psramFree() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
inline uint32_t psramLowest() { return (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM); }

}  // namespace heap
