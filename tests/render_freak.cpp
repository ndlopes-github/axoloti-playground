// Host renderer: runs the freak voice code (lib/freak_voice.h, the same code
// the Axoloti object runs) with the real Braids oscillator and tables, and
// writes a WAV plus a short analysis (level, NaN/clip counts, fundamental).
//   make render && ./tests/render_freak out.wav [shape] [note]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../lib/freak_voice.h"

// The firmware's SDRAM allocator (firmware/axoloti_memory.c): a bump
// allocator, sizes rounded up to 4, sdram_malloc(0) returns the current
// pointer. Mirrored here so braids_init.h's contiguity check behaves as on
// the board.
extern "C" void *sdram_malloc(size_t size) {
    static char arena[1 << 20];
    static size_t used = 0;
    size = (size + 3) & ~(size_t)3;
    if (used + size > sizeof(arena)) return arena;  // firmware: returns the base
    char *p = arena + used;
    used += size;
    return p;
}

using namespace freak;

static void write_wav(const char *path, const std::vector<float> &x) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t n = (uint32_t)x.size(), rate = 48000, bytes = n * 2;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(bytes);
    for (float v : x) {
        float c = v > 1 ? 1 : (v < -1 ? -1 : v);
        int16_t s = (int16_t)lrintf(c * 32767);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

// Fundamental by normalised autocorrelation over 40 Hz .. 2 kHz. A periodic
// signal correlates as well at 2T, 3T, ... as at T, so take the smallest lag
// whose correlation is within 5 % of the best: the usual cure for octave
// errors. Returns 0 if nothing correlates above 0.5 (noise).
static double fundamental(const float *x, int n) {
    static double r[1201];
    double best = 0;
    for (int lag = 24; lag <= 1200; lag++) {
        double num = 0, d0 = 0, d1 = 0;
        for (int i = 0; i + lag < n; i++) { num += x[i] * x[i + lag]; d0 += x[i] * x[i]; d1 += x[i + lag] * x[i + lag]; }
        r[lag] = num / std::sqrt(d0 * d1 + 1e-30);
        if (r[lag] > best) best = r[lag];
    }
    if (best < 0.5) return 0;
    for (int lag = 25; lag < 1200; lag++)  // first local maximum near the best
        if (r[lag] >= 0.95 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) return 48000.0 / lag;
    return 0;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "freak.wav";
    Launchkey lk;
    lk.init();
    if (argc > 2) lk.shape = atoi(argv[2]);
    const float note = argc > 3 ? (float)atof(argv[3]) : 60.0f;
    lk.tick();

    static Voice v;  // static: Voice holds ~1 KB of oscillator state
    v.init();
    std::vector<float> y;
    const int blocks = (int)(1.5f * kControlRate);  // 1 s held, 0.5 s release
    int nan = 0, clip = 0;
    for (int k = 0; k < blocks; k++) {
        lk.tick();
        float out[kBlock];
        v.render(note, k < (int)kControlRate, 100.0f / 127.0f, out);
        for (int i = 0; i < kBlock; i++) {
            if (!std::isfinite(out[i])) { nan++; out[i] = 0; }
            if (std::fabs(out[i]) >= 0.999f) clip++;
            y.push_back(out[i]);
        }
    }
    double rms = 0;
    for (int i = 4800; i < 48000; i++) rms += y[i] * y[i];
    rms = std::sqrt(rms / (48000 - 4800));
    const double f0 = fundamental(&y[12000], 9600);
    const double expect = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
    printf("shape %d note %.1f: rms %.4f, NaN %d, clipped %d, fundamental %.1f Hz (expected %.1f)\n",
           lk.shape, note, rms, nan, clip, f0, expect);
    write_wav(path, y);
    return 0;
}
