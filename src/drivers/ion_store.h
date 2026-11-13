#pragma once
// The ion generator's cap record in NVS (spec 4.6c, Rev Q): the runs of the last 24 hours, so the
// 10-minute cap is kept across reboots. Written at each run's start and end (a few ms each, at most
// about 8 a day on the real schedule); read once at boot. One copy per kind of build, as the user
// settings ("ion_cab", "ion_bench", "ion_test"). Rev N: nothing (no ion generator).

#include <stdint.h>
#include <stddef.h>

namespace ionstore {

size_t load(uint8_t* out, size_t n);   // bytes read (0: none saved, or unreadable)
bool save(const uint8_t* blob, size_t n);
const char* nvsNamespace();

}  // namespace ionstore
