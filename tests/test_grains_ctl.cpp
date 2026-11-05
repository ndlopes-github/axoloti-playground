// Host tests for the grains patch around the engine: WAV loading
// (lib/wav.h, lib/grains_bank.h), the controller (lib/grains_ctl.h) and the
// voice (lib/grains_voice.h). make test
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../lib/grains_voice.h"

using namespace freak;

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            failures++;                                     \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                       \
            std::printf("\n");                              \
        }                                                   \
    } while (0)

// ---------------------------------------------------------------- WAV files

static void put16(std::vector<uint8_t> &v, uint32_t x) { v.push_back(x & 255); v.push_back(x >> 8 & 255); }
static void put32(std::vector<uint8_t> &v, uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }
static void put4(std::vector<uint8_t> &v, const char *s) { v.insert(v.end(), s, s + 4); }

// Value of frame i, channel c, in [-1, 1): a different ramp per channel.
static double test_value(int i, int c) { return std::fmod(0.001 * i * (c + 1) + 0.25 * c, 2.0) - 1.0; }

struct WavSpec {
    uint16_t format, channels, bits;
    uint32_t rate, frames;
    bool extensible, list_before, odd_chunk, stream_size;
    uint32_t truncate;  // bytes cut from the end
};

static std::vector<uint8_t> make_wav(const WavSpec &s) {
    std::vector<uint8_t> v;
    const uint16_t ba = s.channels * (s.bits / 8);
    put4(v, "RIFF"); put32(v, 0); put4(v, "WAVE");
    if (s.list_before) { put4(v, "LIST"); put32(v, 26); for (int i = 0; i < 26; i++) v.push_back('x'); }
    put4(v, "fmt ");
    put32(v, s.extensible ? 40 : 16);
    put16(v, s.extensible ? 0xFFFE : s.format); put16(v, s.channels); put32(v, s.rate);
    put32(v, s.rate * ba); put16(v, ba); put16(v, s.bits);
    if (s.extensible) {
        put16(v, 22); put16(v, s.bits); put32(v, 3);
        put16(v, s.format); for (int i = 0; i < 14; i++) v.push_back(0);  // GUID: subformat first
    }
    if (s.odd_chunk) { put4(v, "junk"); put32(v, 3); v.push_back(1); v.push_back(2); v.push_back(3); v.push_back(0); }
    put4(v, "data"); put32(v, s.stream_size ? 0xFFFFFFFFu : s.frames * ba);
    for (uint32_t i = 0; i < s.frames; i++)
        for (int c = 0; c < s.channels; c++) {
            const double x = test_value(i, c);
            if (s.format == 3) { float f = (float)x; uint32_t u; memcpy(&u, &f, 4); put32(v, u); }
            else if (s.bits == 16) put16(v, (uint32_t)(int32_t)lrint(x * 32767));
            else if (s.bits == 24) { const int32_t q = (int32_t)lrint(x * 8388607); v.push_back(q & 255); v.push_back(q >> 8 & 255); v.push_back(q >> 16 & 255); }
            else if (s.bits == 32) put32(v, (uint32_t)(int32_t)lrint(x * 2147483647.0));
            else v.push_back((uint8_t)(128 + lrint(x * 127)));
        }
    put4(v, "LIST"); put32(v, 4); put4(v, "INFO");  // a chunk after the data: not audio
    const uint32_t riff = (uint32_t)v.size() - 8;
    memcpy(&v[4], &riff, 4);
    v.resize(v.size() - s.truncate);
    return v;
}

// Reads from memory, and checks the SD driver's rule: a read of a sector or
// more starts at a multiple of 512 into a 4-byte aligned buffer.
struct MemReader {
    const std::vector<uint8_t> *f;
    int violations;
    uint32_t read(uint32_t off, uint8_t *buf, uint32_t n) {
        if (n >= 512 && (off % 512 != 0 || (uintptr_t)buf % 4 != 0)) violations++;
        if (off >= f->size()) return 0;
        const uint32_t k = (uint32_t)std::min<size_t>(n, f->size() - off);
        memcpy(buf, f->data() + off, k);
        return k;
    }
};

// Exact-size heap blocks, so ASan traps any write past a loaded source.
struct HeapAlloc {
    std::vector<void *> blocks;
    void *operator()(uint32_t n) { void *p = malloc(n); blocks.push_back(p); return p; }
    ~HeapAlloc() { for (void *p : blocks) free(p); }
};

static void test_wav() {
    const WavSpec specs[] = {
        {1, 1, 16, 44100, 3001, false, false, false, false, 0},
        {1, 2, 16, 48000, 5000, false, true, true, false, 0},
        {1, 1, 24, 48000, 3000, false, false, true, false, 0},
        {1, 2, 24, 96000, 2900, false, true, false, false, 0},  // 6-byte frames straddle every block
        {1, 2, 32, 48000, 2100, false, false, false, false, 0},
        {3, 2, 32, 22050, 2500, false, false, false, false, 0},
        {1, 2, 16, 48000, 2222, true, false, false, false, 0},
        {3, 1, 32, 48000, 1500, true, true, false, false, 0},
        {1, 1, 16, 48000, 4000, false, false, false, true, 0},  // stream: data size 0xFFFFFFFF
        {1, 2, 16, 48000, 4000, false, false, false, false, 1000},  // truncated
    };
    for (const WavSpec &s : specs) {
        const std::vector<uint8_t> file = make_wav(s);
        MemReader rd = {&file, 0};
        HeapAlloc alloc;
        std::vector<uint32_t> stage((kMaxBlockAlign + kStage) / 4);
        const GrainSource src = load_wav(rd, (uint32_t)file.size(), (uint8_t *)stage.data(), alloc);
        uint32_t want = s.frames;
        if (s.truncate) want = (uint32_t)(s.frames - (s.truncate - 12 + s.channels * (s.bits / 8) - 1) / (s.channels * (s.bits / 8)));
        // A stream size reads to the end of the file: the trailing LIST chunk
        // (12 bytes) becomes 6 more 16-bit mono frames.
        if (s.stream_size) want += 6;
        CHECK(src.len == (int32_t)want, "fmt %d ch %d bits %d: %d frames, want %u", s.format, s.channels, s.bits, src.len, want);
        CHECK(rd.violations == 0, "fmt %d ch %d bits %d: %d unaligned sector reads", s.format, s.channels, s.bits, rd.violations);
        CHECK(std::fabs(src.ratio - s.rate / 48000.0f) < 1e-6f, "ratio %g", src.ratio);
        double worst = 0;
        const int32_t check = s.stream_size ? (int32_t)s.frames : src.len;
        for (int32_t i = 0; src.data && i < check; i++) {
            double x = test_value(i, 0);
            if (s.channels > 1) x = 0.5 * (x + test_value(i, 1));
            worst = std::fmax(worst, std::fabs(src.data[i] / 32767.0 - x));
        }
        CHECK(worst < 1e-4, "fmt %d ch %d bits %d: sample error %g", s.format, s.channels, s.bits, worst);
    }
    // Rejected: 8-bit, not RIFF, too short, no data chunk.
    const WavSpec bad8 = {1, 1, 8, 48000, 1000, false, false, false, false, 0};
    std::vector<uint8_t> f8 = make_wav(bad8), junk(4000, 7), tiny = make_wav(specs[0]);
    tiny.resize(30);
    const std::vector<uint8_t> *bad[3] = {&f8, &junk, &tiny};
    for (const std::vector<uint8_t> *f : bad) {
        MemReader rd = {f, 0};
        HeapAlloc alloc;
        std::vector<uint32_t> stage((kMaxBlockAlign + kStage) / 4);
        const GrainSource src = load_wav(rd, (uint32_t)f->size(), (uint8_t *)stage.data(), alloc);
        CHECK(src.len == 0 && alloc.blocks.empty(), "a bad file loaded (%d frames)", src.len);
    }
    // Garbage chunk sizes must not loop or read out of bounds.
    std::vector<uint8_t> g = make_wav(specs[0]);
    const uint32_t huge = 0xFFFFFFF0u;
    memcpy(&g[16], &huge, 4);  // fmt chunk size
    MemReader rd = {&g, 0};
    WavFormat wf;
    CHECK(!wav_parse(rd, (uint32_t)g.size(), wf), "huge chunk accepted");
}

static void test_fallback() {
    const int32_t n = (int32_t)(kFallbackSeconds * kSampleRate);
    std::vector<int16_t> mem(n);
    Bank &b = bank();
    bank_init(b, mem.data(), n);
    CHECK(b.count == 1 && b.src[0].len == n && b.src[0].data == mem.data(), "built-in source not published");
    int peak = 0;
    double e0 = 0, e1 = 0;  // brightness: energy of the first difference, first and last 0.1 s
    for (int32_t i = 1; i < n; i++) {
        peak = std::max(peak, std::abs((int)mem[i]));
        const double d = (mem[i] - mem[i - 1]) / 32768.0;
        if (i < 4800) e0 += d * d;
        if (i >= n - 4800) e1 += d * d;
    }
    CHECK(peak > 8000 && peak < 32767, "built-in source peak %d", peak);
    CHECK(e1 > 20 * e0, "built-in source does not brighten: %g -> %g", e0, e1);
    CHECK(&bank_source(b, 5) == &b.src[0] && &bank_source(b, 0) == &b.src[0], "bank_source fallback");
}

// ---------------------------------------------------------------- controller

static void cc16(GrainsCtl &c, uint8_t cc, int count = 9) { c.midi(0xBF, cc, 127, count); c.midi(0xBF, cc, 0, count); }

static void test_keys() {
    GrainsCtl c;
    c.init();
    c.midi(0x90, 60, 100, 1);
    CHECK(c.gate && c.sounding == 60 && c.strikes == 1 && c.notes == 1, "first key");
    c.midi(0x90, 64, 50, 1);
    CHECK(c.sounding == 64 && c.strikes == 1 && c.notes == 2, "legato key must not start a phrase");
    c.midi(0x90, 64, 0, 1);  // note-on velocity 0 = release
    CHECK(c.gate && c.sounding == 60, "release returns to the held key");
    c.midi(0x80, 60, 0, 1);
    CHECK(!c.gate, "all keys up: gate off");
    c.midi(0x90, 62, 100, 1);
    CHECK(c.strikes == 2, "new phrase");
    c.midi(0x80, 62, 0, 1);

    cc16(c, 105);  // HOLD on
    CHECK(c.hold, "hold on");
    c.midi(0x90, 67, 100, 1);
    c.midi(0x80, 67, 0, 1);
    CHECK(c.gate && c.sounding == 67, "hold keeps the note");
    c.midi(0x90, 69, 100, 1);
    CHECK(c.strikes == 4 && c.sounding == 69, "a key after release under hold starts a phrase");
    c.midi(0x80, 69, 0, 1);
    cc16(c, 105);  // HOLD off
    CHECK(!c.hold && !c.gate, "hold off ends the note");

    c.midi(0xB0, 64, 127, 1);  // pedal down
    c.midi(0x90, 72, 100, 1);
    c.midi(0x80, 72, 0, 1);
    CHECK(c.gate, "pedal keeps the note");
    c.midi(0xB0, 64, 0, 1);
    CHECK(!c.gate, "pedal up ends it");

    for (int n = 40; n < 40 + 2 * kNoteStack; n++) c.midi(0x90, (uint8_t)n, 90, 1);  // overflow the stack
    CHECK(c.nstack == kNoteStack && c.sounding == 40 + 2 * kNoteStack - 1, "full stack");
    for (int n = 40; n < 40 + 2 * kNoteStack; n++) c.midi(0x80, (uint8_t)n, 0, 1);
    CHECK(c.nstack == 0 && !c.gate, "stack empties");
}

static void test_controls() {
    GrainsCtl c;
    c.init();
    cc16(c, 104);
    CHECK(c.page == GPAGE_GRAIN, "CC 104 = GRAIN page");
    c.midi(0xB0, 21, 30, 9);  // knob 1 on GRAIN, not yet caught up: scaled pickup
    c.midi(0xB0, 21, 0, 9);
    CHECK(c.knob[GPAGE_GRAIN][GK_DENSITY] == 0, "density follows to 0: %d", c.knob[GPAGE_GRAIN][GK_DENSITY]);
    CHECK(c.knob[GPAGE_SOUND][GK_SPICE] == kGrainPresets[0].knob[GPAGE_SOUND][GK_SPICE], "SOUND knob 1 untouched");
    cc16(c, 51);
    CHECK(c.page == GPAGE_SOUND, "CC 51 = SOUND page");
    cc16(c, 52);
    CHECK(c.page == GPAGE_FX, "CC 52 = FX page");
    cc16(c, 117);
    CHECK(c.mode == GM_HIT, "Record = HIT");
    cc16(c, 115);
    CHECK(c.mode == GM_SCAN, "Play = SCAN");
    cc16(c, 116);
    CHECK(c.mode == GM_CLOUD, "Stop = CLOUD");
    cc16(c, 75);
    c.tick();
    CHECK(c.freeze_latch && gbus().freeze, "Quantise = freeze latch");
    c.source = 1;
    cc16(c, 103, 3);
    CHECK(c.source == 2, "track > steps the source");
    cc16(c, 103, 3);
    CHECK(c.source == 0, "and wraps: %d", c.source);
    c.source = 7;  // a preset source that is not loaded
    cc16(c, 102, 3);
    CHECK(c.source == 1, "track < from a missing source: %d", c.source);
    cc16(c, 103, 0);
    CHECK(c.source == 1, "no sources: no step");
    c.midi(0x99, 38, 90, 9);  // bottom pad 3 = SCRUB
    c.midi(0xA9, 38, 20, 9);  // pressure
    c.tick();
    CHECK(std::fabs(gbus().position - 20 / 127.0f) < 1e-6f, "SCRUB pressure sets the position");
    c.midi(0x99, 38, 0, 9);
    c.midi(0x99, 41, 100, 9);  // top pad 2 = preset 1
    CHECK(c.preset == 1 && c.mode == kGrainPresets[1].mode, "pad preset");
    cc16(c, 74);
    CHECK(c.mode >= 0 && c.mode < NUM_GMODES, "dice");
    cc16(c, 77);
    CHECK(c.preset == 0, "Undo = preset 0");
}

static void test_maps() {
    CHECK(scan_speed(64) == 0.0f && scan_speed(61) == 0.0f && scan_speed(67) == 0.0f, "scan centre still");
    CHECK(scan_speed(60) < 0.0f && scan_speed(68) > 0.0f, "scan dead band too wide");
    CHECK(std::fabs(scan_speed(127) - 4.0f) < 0.01f && std::fabs(scan_speed(0) + 4.0f) < 0.1f, "scan ends");
    CHECK(std::fabs(scan_speed(107) - 1.0f) < 0.05f && std::fabs(scan_speed(21) + 1.0f) < 0.05f, "scan x1 at 107/21: %g %g", scan_speed(107), scan_speed(21));
    for (int v = 68; v < 127; v++) CHECK(scan_speed((uint8_t)(v + 1)) > scan_speed((uint8_t)v), "scan not increasing at %d", v);
    CHECK(std::fabs(grain_size(0) - 0.005f) < 1e-5f && std::fabs(grain_size(127) - 1.0f) < 1e-3f, "size range");
    CHECK(std::fabs(grain_density(0) - 0.5f) < 1e-4f && std::fabs(grain_density(127) - 200.0f) < 0.1f, "density range");
    CHECK(grain_burst(0) == 1 && grain_burst(127) == kMaxGrains, "burst range");
    for (int i = 0; i < kNumGrainPresets; i++) {
        const GrainPreset &p = kGrainPresets[i];
        bool ok = p.mode < NUM_GMODES && p.source >= 1 && p.source <= 8 && p.fmode < 3 && p.lfo_target < NUM_GLFO_TARGETS && p.lfo_wave < kNumLfoWaves;
        for (int g = 0; g < NUM_GPAGES; g++)
            for (int k = 0; k < 8; k++) ok = ok && p.knob[g][k] <= 127;
        for (int f = 0; f < 8; f++) ok = ok && p.fader[f] <= 127;
        CHECK(ok, "preset %d out of range", i);
    }
}

// ---------------------------------------------------------------- voice

static void test_voice() {
    const int32_t n = (int32_t)(kFallbackSeconds * kSampleRate);
    std::vector<int16_t> mem(n);
    bank_init(bank(), mem.data(), n);
    for (int mode = 0; mode < NUM_GMODES; mode++) {
        GrainsCtl c;
        c.init();
        c.mode = mode;
        c.tick();
        static GrainVoice v;
        v.init(77);
        double held = 0, after = 0, peak = 0;
        bool finite = true;
        const int blocks = (int)(3.0f * kControlRate);
        for (int k = 0; k < blocks; k++) {
            if (k == 10) c.midi(0x90, 60, 110, 1);
            if (k == (int)kControlRate) c.midi(0x80, 60, 0, 1);
            c.tick();
            float l[kBlock], r[kBlock];
            v.render(l, r);
            for (int i = 0; i < kBlock; i++) {
                finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
                peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
                if (k > 300 && k < (int)kControlRate) held += l[i] * l[i];
                if (k > 2.5f * kControlRate) after += l[i] * l[i];
            }
        }
        CHECK(finite, "mode %d: non-finite output", mode);
        CHECK(peak <= 0.5 + 1e-6, "mode %d: peak %g above 0.5", mode, peak);
        CHECK(held > 1.0, "mode %d: silent while held (energy %g)", mode, held);
        CHECK(after < 1e-6, "mode %d: still sounding after release (energy %g)", mode, after);
        std::printf("info: mode %d: peak %.3f, energy held %.1f\n", mode, peak, held);
    }
}

int main() {
    test_wav();
    test_fallback();
    test_keys();
    test_controls();
    test_maps();
    test_voice();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("grains ctl/bank/voice: all tests passed\n");
    return 0;
}
