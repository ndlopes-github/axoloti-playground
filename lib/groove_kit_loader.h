// groove_kit_loader.h: board side of the groove kit (lib/groove_drums.h). A
// thread reads /kit/NN_name.wav (NN = 01..16, the pad) from the SD card into
// SDRAM. Included only by objects/groove_kit.axo: it needs the firmware
// (see sd_files.h for the constraints).
#ifndef GROOVE_KIT_LOADER_H
#define GROOVE_KIT_LOADER_H

#include "grains_bank.h"
#include "groove_drums.h"
#include "sd_files.h"

namespace freak {

// The pad a file name gives: its first two characters as 01..16, else -1.
inline int kit_pad_of(const char *name) {
    if (name[0] < '0' || name[0] > '9' || name[1] < '0' || name[1] > '9') return -1;
    const int n = (name[0] - '0') * 10 + (name[1] - '0');
    return n >= 1 && n <= kNumPads ? n - 1 : -1;
}

inline void kit_load_all(Kit &k, uint8_t *stage) {
    k.state = BANK_LOADING;
    const systime_t t0 = chVTGetSystemTimeX();
    char names[2 * kNumPads][13];
    const int n = fs_ready ? scan_wav_dir("/kit", names, 2 * kNumPads) : -1;
    k.status = !fs_ready ? BANK_NO_CARD : (n < 0 ? BANK_NO_DIR : BANK_OK);
    k.found = n > 0 ? n : 0;
    for (int i = 0; i < n && !chThdShouldTerminateX(); i++) {
        const int pad = kit_pad_of(names[i]);
        if (pad < 0 || k.from_file[pad]) { k.failed++; continue; }  // not NN_, or a second file for the pad
        char path[24] = "/kit/";
        strncat(path, names[i], 12);
        FIL fil;
        if (f_open(&fil, path, FA_READ | FA_OPEN_EXISTING) != FR_OK) { k.failed++; continue; }
        FatReader rd = {&fil};
        SdramAlloc alloc = {0};
        const GrainSource s = load_wav(rd, f_size(&fil), stage, alloc);
        f_close(&fil);
        if (s.len > 0) {
            kit_publish(k, pad, s);
            k.from_file[pad] = true;
            for (int c = 0; c < 13; c++) k.name[pad][c] = names[i][c];
            k.loaded++;
            k.bytes += (uint32_t)s.len * 2;
        } else {
            if (alloc.last) sdram_free(alloc.last);
            k.failed++;
        }
    }
    k.ms = ST2MS(chVTGetSystemTimeX() - t0);
    k.state = BANK_DONE;
}

}  // namespace freak

#endif
