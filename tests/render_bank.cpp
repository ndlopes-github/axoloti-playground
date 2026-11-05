// Renders the grains starter bank (objects/grains_wav/01..08.wav) with
// freak's voice (lib/freak_voice.h, the real Braids code), so the sources
// come from the same engine as the flash patch. Each source is 4 s of mono
// 16-bit 48 kHz audio, C4-based (MIDI note 60 plays it at its own pitch),
// changing along its length so that POSITION matters. Levels are set to an
// RMS of -15 dBFS (peaks at most -1 dBFS), and both ends fade over 5 ms (the
// engine wraps around the source, so the ends meet).
//   make bank
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../lib/freak_voice.h"

// The firmware's bump allocator, as in tests/render_freak.cpp.
extern "C" void *sdram_malloc(size_t size) {
    static char arena[1 << 20];
    static size_t used = 0;
    size = (size + 3) & ~(size_t)3;
    if (used + size > sizeof(arena)) return arena;
    char *p = arena + used;
    used += size;
    return p;
}

using namespace freak;

static const float kSeconds = 4.0f;

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
        const float c = v > 1 ? 1 : (v < -1 ? -1 : v);
        const int16_t s = (int16_t)lrintf(c * 32767);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

// Per-block hook: t in [0, 1) through the source; sets the bus and the
// notes (gate[i], note[i]) of up to 3 voices.
struct Scene {
    const char *file, *what;
    int shape;
    int voices;
    void (*block)(float t, Bus &b, bool gate[3], float note[3]);
};

// Amp envelope: instant attack, full sustain, the given release.
static void flat(Bus &b, float release) {
    b.amp_a = 0.001f; b.amp_d = 0.1f; b.amp_s = 1.0f; b.amp_r = release;
    b.mod_a = 0.001f; b.mod_d = 0.1f; b.mod_s = 0.0f; b.mod_r = 0.1f;
    b.env_amt_oct = 0.0f;
    b.cutoff_oct = 10.0f;  // 20 kHz: the filter is open
    b.q = 0.6f;
    b.fmode = 0;
    b.keytrack = false; b.velcut = false; b.modtimbre = false; b.cycle = false; b.glide = false;
    b.pitch_mod = 0.0f;
    b.spice = 0.0f;
}

static const Scene kScenes[] = {
    {"01.wav", "vowel FOF, a-e-i-o-u along the source", 10, 2,
     [](float t, Bus &b, bool g[3], float n[3]) {
         flat(b, 0.3f);
         b.timbre = t; b.color = 0.45f;
         g[0] = g[1] = true; n[0] = 60; n[1] = 67;
     }},
    {"02.wav", "paraphonic chords, a new chord each second", 27, 1,
     [](float t, Bus &b, bool g[3], float n[3]) {
         flat(b, 0.3f);
         static const float chord[4] = {0.15f, 0.45f, 0.65f, 0.9f};
         b.timbre = 0.2f + 0.5f * t; b.color = chord[(int)(t * 4) & 3];
         g[0] = true; n[0] = 60;
     }},
    {"03.wav", "harmonics, the centre rising along the source", 11, 1,
     [](float t, Bus &b, bool g[3], float n[3]) {
         flat(b, 0.3f);
         b.timbre = t; b.color = 0.3f;
         g[0] = true; n[0] = 60;
     }},
    {"04.wav", "bell, 8 strikes with rising inharmonicity", 19, 2,
     [](float t, Bus &b, bool g[3], float n[3]) {
         flat(b, 1.5f);
         const int k = (int)(t * 8);  // strike k, alternating voices so tails overlap
         b.timbre = 0.55f; b.color = 0.1f + 0.1f * k;
         const float in = t * 8 - k;
         g[0] = (k % 2 == 0) && in < 0.8f; g[1] = (k % 2 == 1) && in < 0.8f;
         n[0] = n[1] = 60;
     }},
    {"05.wav", "particles, density and pitch spread rising", 32, 1,
     [](float t, Bus &b, bool g[3], float n[3]) {
         flat(b, 0.3f);
         b.timbre = 0.2f + 0.7f * t; b.color = 0.3f + 0.5f * t;
         g[0] = true; n[0] = 60;
     }},
    {"06.wav", "feedback FM, more modulation, a new ratio each second", 13, 1,
     [](float t, Bus &b, bool g[3], float n[3]) {
         flat(b, 0.3f);
         static const float ratio[4] = {0.25f, 0.5f, 0.7f, 0.9f};
         b.timbre = 0.8f * t; b.color = ratio[(int)(t * 4) & 3];
         g[0] = true; n[0] = 60;
     }},
    {"07.wav", "plucked arpeggio C E G B D B G E, 8 notes a second", 15, 3,
     [](float t, Bus &b, bool g[3], float n[3]) {
         flat(b, 0.6f);
         static const float arp[8] = {60, 64, 67, 71, 74, 71, 67, 64};
         const int k = (int)(t * kSeconds * 8);
         b.timbre = 0.4f; b.color = 0.5f;
         for (int i = 0; i < 3; i++) g[i] = false;
         const float in = t * kSeconds * 8 - k;
         g[k % 3] = in < 0.7f;  // round robin: each pluck rings while the next starts
         n[k % 3] = arp[k % 8];
     }},
    {"08.wav", "saw swarm C3 G3 C4, detune rising, filter opening", 1, 3,
     [](float t, Bus &b, bool g[3], float n[3]) {
         flat(b, 0.3f);
         b.timbre = 0.2f + 0.6f * t; b.color = 0.0f;
         b.cutoff_oct = 3.9f + 5.3f * t;  // 300 Hz .. 12 kHz
         b.q = 1.5f;
         g[0] = g[1] = g[2] = true; n[0] = 48; n[1] = 55; n[2] = 60;
     }},
};

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "objects/grains_wav";
    for (const Scene &s : kScenes) {
        Launchkey lk;
        lk.init();
        lk.tick();
        static Voice v[3];
        for (int i = 0; i < s.voices; i++) v[i].init();
        bus().shape = s.shape;
        const int blocks = (int)(kSeconds * kControlRate);
        std::vector<float> y;
        bool gate[3] = {false, false, false};
        float note[3] = {60, 60, 60};
        for (int k = 0; k < blocks; k++) {
            s.block((float)k / blocks, bus(), gate, note);
            bus().shape = s.shape;
            float mix[kBlock] = {0};
            for (int i = 0; i < s.voices; i++) {
                float out[kBlock];
                v[i].render(note[i], gate[i], 0.8f, out);
                for (int j = 0; j < kBlock; j++) mix[j] += out[j];
            }
            for (int j = 0; j < kBlock; j++) y.push_back(mix[j]);
        }
        float peak = 0;
        double raw = 0;
        int nan = 0;
        for (float &x : y) {
            if (!std::isfinite(x)) { x = 0; nan++; }
            peak = std::fmax(peak, std::fabs(x));
            raw += (double)x * x;
        }
        raw = std::sqrt(raw / y.size());
        // Even loudness: RMS -15 dBFS, unless that would put the peak above
        // -1 dBFS.
        const float g = peak > 0 ? std::fmin(0.891f / peak, (float)(0.178 / raw)) : 0.0f;
        const int fade = 240;  // 5 ms
        double rms = 0;
        for (size_t i = 0; i < y.size(); i++) {
            float w = 1.0f;
            if (i < (size_t)fade) w = (float)i / fade;
            if (i + fade > y.size()) w = (float)(y.size() - i) / fade;
            y[i] *= g * w;
            rms += y[i] * y[i];
        }
        rms = std::sqrt(rms / y.size());
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, s.file);
        write_wav(path, y);
        printf("%s  %-58s rms %.3f (%.1f dBFS)  gain %.1f  NaN %d\n", s.file, s.what, rms, 20 * std::log10(rms + 1e-12), g, nan);
    }
    return 0;
}
