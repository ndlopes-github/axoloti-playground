// grains_bank.h: the sources the grains voice plays.
//
// Index 0 is a built-in source, generated at init, so the patch plays before
// (or without) any file on the SD card. Indices 1.. are the WAV files of
// /grains on the card, in short-name order, loaded by a background thread
// (objects/grains_bank.axo, lib/grains_loader.h) into SDRAM as mono int16.
//
// Publication: the thread fills entry count, then increments count, with a
// compiler barrier between. The board has one core, which sees its own
// stores in program order, so a reader that sees count > i sees all of
// entry i. Readers only use entries below count.
//
// Header-only, C++11, every type a POD.
#ifndef GRAINS_BANK_H
#define GRAINS_BANK_H

#include <stdint.h>
#include <string.h>

#include "grains.h"
#include "wav.h"

namespace freak {

const int kMaxFiles = 16;                 // files loaded from /grains
const int kMaxSources = kMaxFiles + 1;    // plus the built-in source
const uint32_t kMaxFrames = 1u << 22;     // 87 s at 48 kHz: 8 MB, all of SDRAM
const uint32_t kStage = 4096;             // read block, a multiple of the 512-byte sector
const uint32_t kMaxBlockAlign = 64;       // bytes per frame (16 channels of 32 bit)
const float kFallbackSeconds = 1.5f;

enum { BANK_IDLE, BANK_LOADING, BANK_DONE };
enum { BANK_OK, BANK_NO_CARD, BANK_NO_DIR };

struct Bank {
    GrainSource src[kMaxSources];
    char name[kMaxSources][13];  // 8.3 short names
    volatile int32_t count;      // ready entries
    volatile int32_t state;      // BANK_*
    volatile bool go;            // set by the first DSP tick: every init is done
    int32_t status;              // BANK_OK, BANK_NO_CARD, BANK_NO_DIR
    int32_t found, failed;       // WAV files seen in /grains; files that did not load
    uint32_t bytes, ms;          // sample memory used; load time
};

static Bank g_bank;
inline Bank &bank() { return g_bank; }

inline void bank_publish(Bank &b, const GrainSource &s, const char *name) {
    const int i = b.count;
    if (i >= kMaxSources) return;
    b.src[i] = s;
    int k = 0;
    for (; k < 12 && name[k]; k++) b.name[i][k] = name[k];
    b.name[i][k] = 0;
    __asm__ volatile("" ::: "memory");  // entry i is written before count
    b.count = i + 1;
}

// The source a voice plays for selection sel: sel if it is ready, else the
// built-in one (entry 0, ready once bank_init has run).
inline const GrainSource &bank_source(const Bank &b, int sel) {
    static const GrainSource none = {0, 0, 1.0f};
    if (b.count <= 0) return none;
    return (sel >= 0 && sel < b.count) ? b.src[sel] : b.src[0];
}

// Built-in source: a chord of PolyBLEP saws (C3 C4 G4, so MIDI note 60
// plays it at its own pitch) through a low-pass that opens from 200 Hz to
// 8 kHz along the source, so POSITION sweeps the brightness. Peak below
// 0.75 full scale: three saws of 0.25, and the filter at Q 1.2 adds less
// than its 1.6 dB of peak gain to a saw's spectrum.
inline void make_fallback(int16_t *d, int32_t n) {
    static const float hz[3] = {130.81f, 261.63f, 392.00f};
    float ph[3] = {0.0f, 0.31f, 0.67f};
    Svf svf;
    svf.init();
    for (int32_t i = 0; i < n; i++) {
        if ((i & 15) == 0) svf.set(200.0f * fast_exp2(5.32192809f * (float)i / (float)n), 1.2f);  // 200 x 40^t
        float x = 0.0f;
        for (int k = 0; k < 3; k++) {
            const float dt = hz[k] / kSampleRate, p = ph[k];
            float y = 2.0f * p - 1.0f;
            if (p < dt) { const float t = p / dt; y -= t + t - t * t - 1.0f; }
            else if (p > 1.0f - dt) { const float t = (p - 1.0f) / dt; y -= t * t + t + t + 1.0f; }
            x += 0.25f * y;
            ph[k] = p + dt >= 1.0f ? p + dt - 1.0f : p + dt;
        }
        d[i] = (int16_t)(clampf(svf.process(x, 0), -1.0f, 1.0f) * 32767.0f);
    }
}

// Reset the bank and add the built-in source in mem (n samples, 4-byte
// aligned), or nothing if mem is null.
inline void bank_init(Bank &b, int16_t *mem, int32_t n) {
    b.count = 0;
    b.state = BANK_IDLE;
    b.go = false;
    b.status = BANK_OK;
    b.found = b.failed = 0;
    b.bytes = 0;
    b.ms = 0;
    if (mem && n >= kMinSource) {
        make_fallback(mem, n);
        const GrainSource s = {mem, n, 1.0f};
        bank_publish(b, s, "built-in");
    }
}

// Load one WAV file into memory from alloc (bytes -> 4-byte-aligned pointer
// or null; called at most once, so the caller can free a failed load).
// Reads go through rd.read(offset, buf, n) and, for every read of a sector
// or more, offset is a multiple of 512 and buf is 4-byte aligned:
// the SD driver DMAs whole sectors straight into buf and supports nothing
// else (STM32_SDC_SDIO_UNALIGNED_SUPPORT is FALSE in the firmware). stage
// holds kMaxBlockAlign + kStage bytes, 4-byte aligned.
//
// The data are read in aligned blocks of kStage. A frame that straddles two
// blocks is carried: its first bytes move just before the next block's
// bytes (the kMaxBlockAlign bytes in front of them), so wav_convert always
// sees whole frames. Returns a source with len 0 on failure; a short file
// keeps the frames that arrived.
template <typename Reader, typename Alloc>
GrainSource load_wav(Reader &rd, uint32_t file_size, uint8_t *stage, Alloc &alloc) {
    GrainSource s = {0, 0, 1.0f};
    WavFormat f;
    if (!wav_parse(rd, file_size, f) || f.block_align > kMaxBlockAlign) return s;
    uint32_t frames = f.frames < kMaxFrames ? f.frames : kMaxFrames;
    if (frames < (uint32_t)kMinSource) return s;
    int16_t *out = (int16_t *)alloc(frames * sizeof(int16_t));
    if (!out) return s;
    uint8_t *const blk = stage + kMaxBlockAlign;
    uint32_t pos = f.data_offset & ~511u;     // file offset of the next block
    uint32_t skip = f.data_offset - pos;      // header bytes in the first block
    uint32_t carry = 0;                       // partial frame bytes, just before blk
    uint32_t done = 0;
    const uint32_t ba = f.block_align;
    while (done < frames) {
        const uint32_t got = rd.read(pos, blk, kStage);
        if (got == 0) break;
        pos += got;
        if (got <= skip) { skip -= got; continue; }
        uint8_t *p = blk + skip - carry;      // carry > 0 only after the first block (skip = 0)
        const uint32_t avail = got - skip + carry;
        skip = 0;
        uint32_t k = avail / ba;
        if (k > frames - done) k = frames - done;
        wav_convert(f, p, k, out + done);
        done += k;
        carry = avail - k * ba;               // < ba <= kMaxBlockAlign
        if (done < frames) memmove(blk - carry, p + k * ba, carry);
        if (got < kStage) break;              // end of file
    }
    if (done < (uint32_t)kMinSource) return s;
    s.data = out;
    s.len = (int32_t)done;
    s.ratio = (float)f.rate / kSampleRate;
    return s;
}

}  // namespace freak

#endif
