// Host renderer for the raw monosynth: each preset through the same
// controller and voice code as the board (lib/raw_ctl.h, lib/raw_voice.h),
// dry (no delay or reverb). One mono WAV per preset.
//   make render_raw && ./tests/render_raw [out_dir]
//   ./tests/render_raw <out_dir> probe   (preset 0, C3 held from the start:
//                                         the numbers objects/dev/probe.axo
//                                         logs from patches/bench_raw/probe_c3.axp)
// The phrase: eight keys, half a second each and overlapping by 50 ms (so
// LEGATO shows), the last held for 2 s, then 2 s of release. Printed per
// preset: level, peak, and the spectral centroid at a few times (Hz).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../lib/raw_ctl.h"

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
    for (uint32_t i = 0; i < n; i++) {
        const float c = x[i] > 1 ? 1 : (x[i] < -1 ? -1 : x[i]);
        const int16_t s = (int16_t)lrintf(c * 32767);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

static double centroid(const std::vector<float> &x, size_t end) {
    const int n = 2048;
    if (end < (size_t)n || end > x.size()) return 0;
    double num = 0, den = 0;
    for (int b = 1; b < n / 2 && b * 48000.0 / n < 12000; b++) {
        double re = 0, im = 0;
        for (int i = 0; i < n; i++) {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1));
            const double s = w * x[end - n + i];
            re += s * std::cos(2 * M_PI * b * i / n);
            im -= s * std::sin(2 * M_PI * b * i / n);
        }
        const double p = re * re + im * im, f = b * 48000.0 / n;
        num += f * p;
        den += p;
    }
    return den > 0 ? num / den : 0;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    const bool probe = argc > 2 && strcmp(argv[2], "probe") == 0;
    static const uint8_t phrase[8] = {48, 51, 55, 60, 58, 55, 51, 43};
    for (int pi = 0; pi < (probe ? 1 : kNumRawPresets); pi++) {
        static RawCtl c;
        static RawVoice v;
        c.init();  // as objects/raw_ctl.axo: init, preset, test note, one tick
        if (pi) c.apply_preset(pi);
        if (probe) c.key_on(48, 100);
        c.tick();
        v.init();
        const int blocks = probe ? 2500 : (int)(8.0f * kControlRate);
        std::vector<float> y;
        double sum = 0, peak = 0;
        int nan = 0;
        const int step = (int)(0.5f * kControlRate), lap = (int)(0.05f * kControlRate);
        for (int k = 0; k < blocks; k++) {
            if (!probe) {
                for (int j = 0; j < 8; j++) {
                    if (k == j * step) c.key_on(phrase[j], (uint8_t)(70 + 7 * j));
                    if (j < 7 && k == (j + 1) * step + lap) c.key_off(phrase[j]);
                }
                if (k == 7 * step + (int)(2.0f * kControlRate)) c.key_off(phrase[7]);
            }
            c.tick();
            float b[kBlock];
            v.render(rawbus(), b);
            for (int i = 0; i < kBlock; i++) {
                if (!std::isfinite(b[i])) { nan++; b[i] = 0; }
                y.push_back(b[i]);
                if (!probe || k >= 300) {
                    sum += (double)b[i] * b[i];
                    peak = std::fmax(peak, std::fabs(b[i]));
                }
            }
        }
        if (probe) {
            const size_t n0 = (size_t)300 * kBlock, n = y.size() - n0;
            int zc = 0;
            for (size_t i = n0 + 1; i < y.size(); i++)
                if ((y[i] >= 0) != (y[i - 1] >= 0)) zc++;
            printf("host probe (preset 0, C3 held): rms^2 x1e6 %d peak x1000 %d zc/s %d\n", (int)(sum / n * 1e6),
                   (int)(peak * 1000), (int)(zc * 48000.0 / n));
            continue;
        }
        char path[512];
        snprintf(path, sizeof path, "%s/raw_preset_%d.wav", out, pi);
        write_wav(path, y);
        printf("preset %d %-13s rms %.3f peak %.3f NaN %d | centroid Hz at 0.4 1.4 2.4 3.9 5.5 s: %5.0f %5.0f %5.0f %5.0f %5.0f\n",
               pi, kRawPresets[pi].name, std::sqrt(sum / y.size()), peak, nan, centroid(y, (size_t)(0.4 * 48000)),
               centroid(y, (size_t)(1.4 * 48000)), centroid(y, (size_t)(2.4 * 48000)), centroid(y, (size_t)(3.9 * 48000)),
               centroid(y, (size_t)(5.5 * 48000)));
    }
    return 0;
}
