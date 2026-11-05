// Host renderer for the grains patch: loads the starter bank with the same
// loader as the board (lib/grains_bank.h), then plays each preset through
// the same controller and voice code (lib/grains_ctl.h, lib/grains_voice.h)
// and writes one stereo WAV per preset, without the delay and reverb.
//   make render_grains && ./tests/render_grains [out_dir] [wav_dir]
// Each preset holds C4 for 2.5 s with a short phrase on top (G4, then a
// legato E4; HIT presets get a new note every 0.4 s) and then releases.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../lib/grains_voice.h"

using namespace freak;

struct FileReader {
    FILE *f;
    uint32_t read(uint32_t off, uint8_t *buf, uint32_t n) {
        if (fseek(f, off, SEEK_SET) != 0) return 0;
        return (uint32_t)fread(buf, 1, n, f);
    }
};

struct HeapAlloc {
    void *operator()(uint32_t n) { return malloc(n); }  // kept for the whole run
};

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
        // Twice the voice level (its nominal peak is 0.5), as the master
        // fader near full would give, so the files are not too quiet.
        const float x[2] = {2 * l[i], 2 * r[i]};
        for (float v : x) {
            const float c = v > 1 ? 1 : (v < -1 ? -1 : v);
            const int16_t s = (int16_t)lrintf(c * 32767);
            fwrite(&s, 2, 1, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    const char *wavs = argc > 2 ? argv[2] : "objects/grains_wav";
    const int32_t nb = (int32_t)(kFallbackSeconds * kSampleRate);
    static std::vector<int16_t> builtin(nb);
    Bank &b = bank();
    bank_init(b, builtin.data(), nb);
    std::vector<uint32_t> stage((kMaxBlockAlign + kStage) / 4);
    for (int i = 1; i <= 8; i++) {
        char path[512], name[16];
        snprintf(name, sizeof name, "%02d.WAV", i);
        snprintf(path, sizeof path, "%s/%02d.wav", wavs, i);
        FILE *f = fopen(path, "rb");
        if (!f) { printf("missing %s\n", path); continue; }
        fseek(f, 0, SEEK_END);
        const uint32_t size = (uint32_t)ftell(f);
        FileReader rd = {f};
        HeapAlloc alloc;
        const GrainSource s = load_wav(rd, size, (uint8_t *)stage.data(), alloc);
        fclose(f);
        if (s.len > 0) bank_publish(b, s, name);
        else printf("could not load %s\n", path);
    }
    printf("bank: %d sources\n", b.count);

    for (int pi = 0; pi < kNumGrainPresets; pi++) {
        GrainsCtl c;
        c.init();
        c.apply_preset(pi);
        c.fader[F_MASTER] = 127;
        c.tick();
        static GrainVoice v;
        v.init(1234);
        const bool hit = c.mode == GM_HIT;
        const int blocks = (int)(4.0f * kControlRate);
        std::vector<float> l, r;
        double sum = 0, peak = 0;
        int nan = 0, grains_max = 0;
        for (int k = 0; k < blocks; k++) {
            const float t = (float)k / kControlRate;
            const int ms = (int)(t * 1000 + 0.5f);
            if (k == 0) c.midi(0x90, 60, 100, b.count);
            if (hit) {
                if (k > 0 && k % (int)(0.4f * kControlRate) == 0 && t < 2.5f) {
                    static const uint8_t seq[6] = {67, 64, 72, 62, 69, 60};
                    c.midi(0x90, seq[(k / (int)(0.4f * kControlRate)) % 6], 100, b.count);
                }
            } else {
                if (k == (int)(1.0f * kControlRate)) c.midi(0x90, 67, 90, b.count);
                if (k == (int)(1.6f * kControlRate)) c.midi(0x90, 64, 90, b.count);  // legato
                if (k == (int)(1.6f * kControlRate) + 1) c.midi(0x80, 67, 0, b.count);
                if (k == (int)(2.2f * kControlRate)) c.midi(0x80, 64, 0, b.count);
            }
            if (k == (int)(2.5f * kControlRate)) for (int n = 0; n < 128; n++) c.midi(0x80, (uint8_t)n, 0, b.count);
            (void)ms;
            c.tick();
            float bl[kBlock], br[kBlock];
            v.render(bl, br);
            grains_max = std::max(grains_max, v.eng.active_count());
            for (int i = 0; i < kBlock; i++) {
                if (!std::isfinite(bl[i]) || !std::isfinite(br[i])) { nan++; bl[i] = br[i] = 0; }
                l.push_back(bl[i]);
                r.push_back(br[i]);
                sum += bl[i] * bl[i] + br[i] * br[i];
                peak = std::fmax(peak, std::fmax(std::fabs(bl[i]), std::fabs(br[i])));
            }
        }
        char path[512];
        snprintf(path, sizeof path, "%s/grains_preset_%d.wav", out, pi);
        write_wav2(path, l, r);
        const double rms = std::sqrt(sum / (2.0 * l.size()));
        printf("preset %d %-15s %-5s source %d: rms %.3f peak %.3f, up to %2d grains, NaN %d -> %s\n", pi,
               kGrainPresets[pi].name, c.mode == GM_SCAN ? "SCAN" : (hit ? "HIT" : "CLOUD"), c.source, rms, peak,
               grains_max, nan, path);
    }
    return 0;
}
