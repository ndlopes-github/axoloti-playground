// braids_init.h: initialise a Braids DigitalOscillator on Ksoloti firmware.
//
// Ksoloti's braids/digital_oscillator.h moved the oscillator's delay lines to
// SDRAM, and its Init() allocates 1025 * 8 = 8200 bytes for them. The union
// is 16384 bytes, though: comb is int16_t[8192]. So the comb shape wrote past
// the block, into the next allocation; AddressSanitizer reports it on the
// host. sdram_malloc is a bump allocator (firmware/axoloti_memory.c):
// sdram_malloc(0) returns the current pointer without advancing it, and
// blocks follow each other. Reserving the missing bytes right after Init()
// therefore makes the block whole. The returned flag says whether the two
// pieces really are contiguous; if not, callers must avoid the comb shape.
#ifndef BRAIDS_INIT_H
#define BRAIDS_INIT_H

#include "braids/digital_oscillator.h"

namespace freak {

const size_t kKsolotiDelayAlloc = 1025 * 8;  // what Init() allocates
static_assert(sizeof(braids::delay_lines_t) >= kKsolotiDelayAlloc, "Braids delay lines shrank?");
const int kShapeComb = 2;  // the only shape that uses more than 8200 bytes

inline bool init_braids(braids::DigitalOscillator *osc) {
    char *before = (char *)sdram_malloc(0);
    osc->Init();
    const size_t extra = sizeof(braids::delay_lines_t) - kKsolotiDelayAlloc;
    char *rest = (char *)sdram_malloc(extra);
    return before != 0 && rest == before + kKsolotiDelayAlloc;
}

}  // namespace freak

#endif
