// sd_files.h: reading WAV files from the SD card in a patch, shared by the
// grains bank (grains_loader.h) and the groove kit (groove_kit_loader.h).
// Board only: it needs the firmware (FatFs, ChibiOS 3, sdram_malloc).
//
// Constraints, from the firmware:
//  - sdram_malloc is a bump allocator that is not thread-safe, and when it
//    runs out it returns the SDRAM base instead of null. A loader thread
//    waits for the first DSP tick, after every object's init has done its
//    allocations, and SdramAlloc checks sdram_get_free() first.
//  - FatFs is built with _FS_TINY (a FIL has no buffer) and reentrant, so a
//    FIL on the thread's stack is fine and the patcher may use the card at
//    the same time.
//  - Reads of a sector or more must start at a 512-byte file offset into a
//    4-byte-aligned buffer (load_wav in grains_bank.h does that).
#ifndef SD_FILES_H
#define SD_FILES_H

#include <stdint.h>
#include <string.h>

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

// The WAV files in dir, by short name, at most max (the first in name
// order). Names starting with '_' are skipped: the "._name" files macOS
// writes next to every file show up as "_NAME~1.WAV". Returns -1 if there is
// no such directory.
inline int scan_wav_dir(const char *dir, char (*names)[13], int max) {
    DIR d;
    FILINFO fno;
    fno.lfname = 0;  // short names only (LFN is on in the firmware)
    fno.lfsize = 0;
    if (f_opendir(&d, dir) != FR_OK) return -1;
    int n = 0;
    while (f_readdir(&d, &fno) == FR_OK && fno.fname[0]) {
        if (fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) continue;
        if (fno.fname[0] == '_' || fno.fname[0] == '.') continue;
        const char *dot = strrchr(fno.fname, '.');
        if (!dot || name_cmp(dot, ".WAV") != 0) continue;
        // Insertion into the sorted list, keeping the max smallest.
        int i = n < max ? n : max - 1;
        if (n == max && name_cmp(fno.fname, names[i]) >= 0) continue;
        while (i > 0 && name_cmp(fno.fname, names[i - 1]) < 0) {
            memcpy(names[i], names[i - 1], 13);
            i--;
        }
        strncpy(names[i], fno.fname, 12);
        names[i][12] = 0;
        if (n < max) n++;
    }
    f_closedir(&d);
    return n;
}

}  // namespace freak

#endif
