// Host renderer for the soliton patch: each preset through the same
// controller and voice code as the board (lib/soliton_ctl.h,
// lib/soliton_voice.h), three voices as the polyphonic subpatch would
// allocate them, dry (no delay or reverb). One mono WAV per preset.
//   make render_soliton && ./tests/render_soliton [out_dir]
//   ./tests/render_soliton <out_dir> probe   (one voice, C3, velocity 0.8,
//                                             the board's bench_soliton/probe)
// The phrase: C3 alone for 1.5 s (the field's own evolution), then G3 and
// C4 join (a chord of three fields), all released at 3.5 s, 1.5 s of tail.
// Printed per preset: level, the spectral centroid of the output at a few
// times (the timbre's motion, in harmonics of C3), and the steps taken.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../lib/soliton_voice.h"

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
        // x3: the voice's nominal peak is 1/4 (headroom for 3 voices), and
        // the master fader near full would give about this much.
        const float v = 3.0f * x[i];
        const float c = v > 1 ? 1 : (v < -1 ? -1 : v);
        const int16_t s = (int16_t)lrintf(c * 32767);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

// Spectral centroid of 2048 samples ending at sample `end`, in Hz (direct
// DFT up to 8 kHz on a Hann window; slow but this is a test tool).
static double centroid(const std::vector<float> &x, size_t end) {
    const int n = 2048;
    if (end < (size_t)n) return 0;
    double num = 0, den = 0;
    for (int b = 1; b < n / 2 && b * 48000.0 / n < 8000; b++) {
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

struct Key {
    float note;
    bool gate;
    float vel;
};

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    const bool probe = argc > 2 && strcmp(argv[2], "probe") == 0;
    const int first = probe ? 0 : 0, last = probe ? 0 : kNumSolPresets - 1;
    for (int pi = first; pi <= last; pi++) {
        static SolitonCtl c;
        c.init();
        if (pi) c.apply_preset(pi);
        c.fader[F_MASTER] = 127;
        c.tick();
        static SolVoice v[3];
        for (int i = 0; i < 3; i++) v[i].init();
        Key keys[3] = {{48, false, 0.8f}, {55, false, 0.7f}, {60, false, 0.75f}};
        const float len = probe ? 2500.0f / kControlRate : 5.0f;  // probe.axo: blocks 300..2500
        const int blocks = (int)(len * kControlRate);
        std::vector<float> x;
        double sum = 0, peak = 0;
        int nan = 0;
        for (int k = 0; k < blocks; k++) {
            const float t = (float)k / kControlRate;
            keys[0].gate = probe || t < 3.5f;
            keys[1].gate = !probe && t >= 1.5f && t < 3.5f;
            keys[2].gate = !probe && t >= 1.5f && t < 3.5f;
            c.tick();
            float mix[kBlock] = {0}, o[kBlock];
            for (int i = 0; i < 3; i++) {
                v[i].render(keys[i].note, keys[i].gate, keys[i].vel, o);
                for (int j = 0; j < kBlock; j++) mix[j] += o[j];
            }
            for (int j = 0; j < kBlock; j++) {
                if (!std::isfinite(mix[j])) { nan++; mix[j] = 0; }
                x.push_back(mix[j]);
                if (!probe || t >= 0.1f) {  // the probe skips the first 0.1 s
                    sum += mix[j] * mix[j];
                    peak = std::fmax(peak, std::fabs(mix[j]));
                }
            }
        }
        const double n = probe ? x.size() - (size_t)(0.1 * kSampleRate) : x.size();
        if (probe) {
            // Same statistics as objects/dev/probe.axo, for the comparison.
            int zc = 0;
            for (size_t i = (size_t)(0.1 * kSampleRate) + 1; i < x.size(); i++)
                if ((x[i] >= 0) != (x[i - 1] >= 0)) zc++;
            printf("host probe (preset 0, C3, vel 0.8): rms^2 x1e6 %d peak x1000 %d zc/s %d\n",
                   (int)(sum / n * 1e6), (int)(peak * 1000), (int)(zc * 48000.0 / n));
            continue;
        }
        char path[512];
        snprintf(path, sizeof path, "%s/soliton_preset_%d.wav", out, pi);
        write_wav(path, x);
        printf("preset %d %-15s %-7s rms %.3f peak %.3f NaN %d | centroid/C3 at 0.2 0.7 1.4 3.0 s: %4.1f %4.1f %4.1f %4.1f"
               " | steps %u -> %s\n",
               pi, kSolPresets[pi].name, kEngineNames[c.engine], std::sqrt(sum / n), peak, nan,
               centroid(x, (size_t)(0.2 * kSampleRate)) / 130.81, centroid(x, (size_t)(0.7 * kSampleRate)) / 130.81,
               centroid(x, (size_t)(1.4 * kSampleRate)) / 130.81, centroid(x, (size_t)(3.0 * kSampleRate)) / 130.81,
               v[0].f.steps, path);
    }
    return 0;
}
