// grains_loader.h: board side of the grains bank (lib/grains_bank.h). A
// thread reads /grains/*.wav from the SD card with FatFs into SDRAM.
// Included only by objects/grains_bank.axo: it needs the firmware (see
// sd_files.h for the constraints).
#ifndef GRAINS_LOADER_H
#define GRAINS_LOADER_H

#include "grains_bank.h"
#include "sd_files.h"

namespace freak {

// Load every file, publishing each source as soon as it is ready.
inline void bank_load_all(Bank &b, uint8_t *stage) {
    b.state = BANK_LOADING;
    const systime_t t0 = chVTGetSystemTimeX();
    char names[kMaxFiles][13];
    const int n = fs_ready ? scan_wav_dir("/grains", names, kMaxFiles) : -1;
    b.status = !fs_ready ? BANK_NO_CARD : (n < 0 ? BANK_NO_DIR : BANK_OK);
    b.found = n > 0 ? n : 0;
    for (int i = 0; i < n && !chThdShouldTerminateX(); i++) {
        char path[24] = "/grains/";
        strncat(path, names[i], 12);
        FIL fil;
        if (f_open(&fil, path, FA_READ | FA_OPEN_EXISTING) != FR_OK) {
            b.failed++;
            continue;
        }
        FatReader rd = {&fil};
        SdramAlloc alloc = {0};
        const GrainSource s = load_wav(rd, f_size(&fil), stage, alloc);
        f_close(&fil);
        if (s.len > 0) {
            bank_publish(b, s, names[i]);
            b.bytes += (uint32_t)s.len * 2;
        } else {
            if (alloc.last) sdram_free(alloc.last);  // still the last block: nothing else allocates now
            b.failed++;
        }
    }
    b.ms = ST2MS(chVTGetSystemTimeX() - t0);
    b.state = BANK_DONE;
}

}  // namespace freak

#endif
