// grains_loader.h: board side of the grains bank (lib/grains_bank.h). A
// thread reads /grains/*.wav from the SD card with FatFs into SDRAM.
// Included only by objects/grains_bank.axo: it needs the firmware (FatFs,
// ChibiOS 3, sdram_malloc).
//
// Constraints, from the firmware:
//  - sdram_malloc is a bump allocator that is not thread-safe, and when it
//    runs out it returns the SDRAM base instead of null. The thread waits
//    for the first DSP tick (bank.go), after every object's init has done
//    its allocations, and checks sdram_get_free() before allocating.
//  - FatFs is built with _FS_TINY (a FIL has no buffer) and reentrant, so a
//    FIL on this thread's stack is fine and the patcher may use the card at
//    the same time.
#ifndef GRAINS_LOADER_H
#define GRAINS_LOADER_H

#include "grains_bank.h"

namespace freak {

const int32_t kSdramReserve = 16 * 1024;  // left free after loading

// Short names compared without case. With long names enabled, FatFs returns
// a short name in lower case when its directory entry says so (the NT case
// flags, set for a name like "01.wav"), so "01.wav" and "01.WAV" both occur.
inline char upper_ascii(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
inline int name_cmp(const char *a, const char *b) {
    while (*a && upper_ascii(*a) == upper_ascii(*b)) { a++; b++; }
    return (int)(unsigned char)upper_ascii(*a) - (int)(unsigned char)upper_ascii(*b);
}

struct FatReader {
    FIL *fp;
    uint32_t read(uint32_t off, uint8_t *buf, uint32_t n) {
        if (chThdShouldTerminateX()) return 0;  // patch stopping: end the load
        if (f_lseek(fp, off) != FR_OK) return 0;
        UINT got = 0;
        if (f_read(fp, buf, n, &got) != FR_OK) return 0;
        return got;
    }
};

struct SdramAlloc {
    void *last;
    void *operator()(uint32_t n) {
        last = 0;
        if (sdram_get_free() < (int32_t)n + kSdramReserve) return 0;
        last = sdram_malloc(n);
        return last;
    }
};

// The WAV files in /grains, by short name, at most kMaxFiles (the first in
// name order). Names starting with '_' are skipped: the "._name" files
// macOS writes next to every file show up as "_NAME~1.WAV". Returns -1 if
// there is no /grains directory.
inline int scan_grains_dir(char names[kMaxFiles][13]) {
    DIR dir;
    FILINFO fno;
    fno.lfname = 0;  // short names only (LFN is on in the firmware)
    fno.lfsize = 0;
    if (f_opendir(&dir, "/grains") != FR_OK) return -1;
    int n = 0;
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) continue;
        if (fno.fname[0] == '_' || fno.fname[0] == '.') continue;
        const char *dot = strrchr(fno.fname, '.');
        if (!dot || name_cmp(dot, ".WAV") != 0) continue;
        // Insertion into the sorted list, keeping the kMaxFiles smallest.
        int i = n < kMaxFiles ? n : kMaxFiles - 1;
        if (n == kMaxFiles && name_cmp(fno.fname, names[i]) >= 0) continue;
        while (i > 0 && name_cmp(fno.fname, names[i - 1]) < 0) {
            memcpy(names[i], names[i - 1], 13);
            i--;
        }
        strncpy(names[i], fno.fname, 12);
        names[i][12] = 0;
        if (n < kMaxFiles) n++;
    }
    f_closedir(&dir);
    return n;
}

// Load every file, publishing each source as soon as it is ready.
inline void bank_load_all(Bank &b, uint8_t *stage) {
    b.state = BANK_LOADING;
    const systime_t t0 = chVTGetSystemTimeX();
    char names[kMaxFiles][13];
    const int n = fs_ready ? scan_grains_dir(names) : -1;
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
