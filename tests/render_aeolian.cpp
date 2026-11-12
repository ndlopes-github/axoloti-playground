// Host renderer for the aeolian wind harp: each preset through the same
// controller and harp code as the board (lib/aeolian_ctl.h,
// lib/aeolian_harp.h), dry (no delay or reverb). One stereo WAV per preset.
//   make render_aeolian && ./tests/render_aeolian [out_dir]
//   ./tests/render_aeolian <out_dir> probe   (preset 0, the numbers
//                                             objects/dev/probe.axo logs from
//                                             the left channel on the board)
// The phrase: 20 s of wind on the preset's tuning; at 8 s a new key (a
// fifth up), at 14 s every string plucked. Printed per preset: level, the
// spectral centroid at a few times (Hz), and the wind's range.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../lib/aeolian_ctl.h"

using namespace freak;

static void write_wav2(const char *path, const std::vector<float> &l, const std::vector<float> &r) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t n = (uint32_t)l.size(), rate = 48000, bytes = n * 4;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    fwrite("data", 1, 4, f); u32(bytes);
    for (uint32_t i = 0; i < n; i++) {
        const float x[2] = {l[i], r[i]};  // the harp's own level control sets the level
        for (float v : x) {
            const float c = v > 1 ? 1 : (v < -1 ? -1 : v);
            const int16_t s = (int16_t)lrintf(c * 32767);
            fwrite(&s, 2, 1, f);
        }
    }
    fclose(f);
}

static double centroid(const std::vector<float> &x, size_t end) {
    const int n = 2048;
    if (end < (size_t)n) return 0;
    double num = 0, den = 0;
    for (int b = 1; b < n / 2 && b * 48000.0 / n < 10000; b++) {
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
    for (int pi = 0; pi < (probe ? 1 : kNumAeoPresets); pi++) {
        static AeolianCtl c;
        static Harp h;
        c.init();  // as objects/aeolian_ctl.axo: init, preset, one tick
        if (pi) c.apply_preset(pi);
        c.fader[F_MASTER] = 127;
        c.tick();
        h.init();
        const int blocks = probe ? 2500 : (int)(20.0f * kControlRate);
        std::vector<float> l, r;
        double sum = 0, peak = 0, umin = 1e9, umax = 0;
        int nan = 0;
        const uint8_t root = kAeoPresets[pi].root;
        for (int k = 0; k < blocks; k++) {
            if (!probe && k == (int)(8 * kControlRate)) c.key_on((uint8_t)(root + 7), 90);
            if (!probe && k == (int)(14 * kControlRate)) c.pluck_all(0.8f);
            c.tick();
            h.tick(aeobus());
            float bl[kBlock], br[kBlock];
            h.render(aeobus(), bl, br);
            umin = std::fmin(umin, h.U);
            umax = std::fmax(umax, h.U);
            for (int i = 0; i < kBlock; i++) {
                if (!std::isfinite(bl[i]) || !std::isfinite(br[i])) { nan++; bl[i] = br[i] = 0; }
                l.push_back(bl[i]);
                r.push_back(br[i]);
                if (!probe || k >= 300) {
                    sum += probe ? bl[i] * bl[i] : 0.5 * (bl[i] * bl[i] + br[i] * br[i]);
                    peak = std::fmax(peak, probe ? std::fabs(bl[i]) : std::fmax(std::fabs(bl[i]), std::fabs(br[i])));
                }
            }
        }
        if (probe) {
            const size_t n0 = (size_t)300 * kBlock, n = l.size() - n0;
            int zc = 0;
            for (size_t i = n0 + 1; i < l.size(); i++)
                if ((l[i] >= 0) != (l[i - 1] >= 0)) zc++;
            printf("host probe (preset 0, left): rms^2 x1e6 %d peak x1000 %d zc/s %d\n", (int)(sum / n * 1e6),
                   (int)(peak * 1000), (int)(zc * 48000.0 / n));
            continue;
        }
        char path[512];
        snprintf(path, sizeof path, "%s/aeolian_preset_%d.wav", out, pi);
        write_wav2(path, l, r);
        printf("preset %d %-14s %-8s %-7s rms %.3f peak %.3f NaN %d | wind %.1f-%.1f m/s | centroid Hz at 2 5 8 11 17 s:"
               " %4.0f %4.0f %4.0f %4.0f %4.0f\n",
               pi, kAeoPresets[pi].name, kTuningNames[kAeoPresets[pi].tuning], kWeatherNames[kAeoPresets[pi].weather],
               std::sqrt(sum / l.size()), peak, nan, umin, umax, centroid(l, 2 * 48000), centroid(l, 5 * 48000),
               centroid(l, 8 * 48000), centroid(l, 11 * 48000), centroid(l, 17 * 48000));
    }
    return 0;
}
