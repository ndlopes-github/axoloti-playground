// wav.h: WAV parsing and conversion to mono int16, for the grains bank.
//
// The parser walks the RIFF chunks through a reader (read n bytes at an
// offset), so the same code runs on the host (tests, a memory reader) and on
// the board (a FatFs file), and never needs the whole file in memory: the
// bank then allocates exactly frames x 2 bytes and converts the audio chunk
// by chunk.
//
// Accepted: PCM 16, 24 or 32 bit, or IEEE float 32 bit (format 1 or 3, or
// WAVE_FORMAT_EXTENSIBLE with one of those as subformat), any number of
// channels (the first two are mixed to mono), any sample rate (the engine
// resamples by rate / 48 kHz, within its 1/8..2 range). 8-bit files are
// rejected.
//
// Header-only, C++11, no allocation, no libm.
#ifndef WAV_H
#define WAV_H

#include <stdint.h>
#include <string.h>

namespace freak {

struct WavFormat {
    uint16_t format;       // 1 PCM, 3 float (resolved from EXTENSIBLE)
    uint16_t channels;
    uint16_t bits;
    uint16_t block_align;  // bytes per frame
    uint32_t rate;
    uint32_t data_offset;  // file offset of the first frame
    uint32_t frames;       // whole frames in the data chunk (clamped to the file)
};

inline uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

// Reader: uint32_t read(uint32_t offset, uint8_t *buf, uint32_t n), the
// number of bytes read (fewer at the end of the file). file_size: the size.
// Returns false for anything that is not a WAV file this code can play.
template <typename Reader>
bool wav_parse(Reader &rd, uint32_t file_size, WavFormat &f) {
    uint8_t h[40];
    if (file_size < 12 || rd.read(0, h, 12) != 12) return false;
    if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return false;
    bool have_fmt = false;
    uint32_t off = 12;
    // Each step moves off forward by at least 8, so the loop ends; the
    // bound on steps guards against a reader that misbehaves.
    for (int steps = 0; steps < 256 && off + 8 <= file_size; steps++) {
        if (rd.read(off, h, 8) != 8) return false;
        const uint32_t size = le32(h + 4);
        const uint32_t body = off + 8;
        if (memcmp(h, "fmt ", 4) == 0) {
            if (size < 16 || body + 16 > file_size) return false;
            const uint32_t n = size < 40 ? size : 40;
            if (rd.read(body, h, n) != n) return false;
            f.format = le16(h);
            f.channels = le16(h + 2);
            f.rate = le32(h + 4);
            f.block_align = le16(h + 12);
            f.bits = le16(h + 14);
            if (f.format == 0xFFFE) {  // EXTENSIBLE: subformat GUID at byte 24
                if (n < 26) return false;
                f.format = le16(h + 24);
            }
            have_fmt = true;
        } else if (memcmp(h, "data", 4) == 0) {
            if (!have_fmt) return false;
            const bool pcm = f.format == 1 && (f.bits == 16 || f.bits == 24 || f.bits == 32);
            const bool flt = f.format == 3 && f.bits == 32;
            if (!(pcm || flt) || f.channels == 0 || f.rate == 0) return false;
            if (f.block_align != f.channels * (f.bits / 8)) return false;
            // Some writers leave size 0 or 0xFFFFFFFF (streams): use what is there.
            uint32_t bytes = size;
            if (bytes == 0 || bytes > file_size - body) bytes = file_size - body;
            f.data_offset = body;
            f.frames = bytes / f.block_align;
            return f.frames > 0;
        }
        // Chunks are padded to an even size. Stop before off can wrap around.
        const uint32_t step = 8 + size + (size & 1);
        if (step < 8 || step > file_size - off) break;  // off <= file_size here
        off += step;
    }
    return false;
}

// One sample of channel c in a frame, scaled to the int16 range, as int32.
inline int32_t wav_sample(const WavFormat &f, const uint8_t *frame, int c) {
    const uint8_t *p = frame + c * (f.bits / 8);
    if (f.format == 3) {
        uint32_t u = le32(p);
        float x;
        memcpy(&x, &u, 4);
        if (!(x > -1.0f)) x = -1.0f;  // also NaN
        if (x > 1.0f) x = 1.0f;
        return (int32_t)(x * 32767.0f);
    }
    if (f.bits == 16) return (int16_t)le16(p);
    // 24 and 32 bit: the sample in the top bits of an int32, then its top
    // 16 bits (GCC converts and shifts signed values arithmetically).
    if (f.bits == 24) return (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 16;
    return (int32_t)le32(p) >> 16;
}

// Convert n whole frames from in (raw file bytes) to mono int16 in out.
// Stereo (or more) mixes the first two channels: (a + b) / 2 stays in range.
inline void wav_convert(const WavFormat &f, const uint8_t *in, uint32_t n, int16_t *out) {
    for (uint32_t i = 0; i < n; i++, in += f.block_align) {
        int32_t s = wav_sample(f, in, 0);
        if (f.channels > 1) s = (s + wav_sample(f, in, 1)) / 2;
        out[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
    }
}

}  // namespace freak

#endif
