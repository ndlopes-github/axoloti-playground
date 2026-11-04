// Host tests for lib/freak_dsp.h and lib/launchkey_ctl.h: make test
// They check the error bounds claimed in the comments, filter stability over
// the whole parameter range, envelope timing, the delay's output bound, and
// the Launchkey mapping that docs/cheatsheet.tex documents.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../lib/launchkey_ctl.h"

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

static void test_fast_math() {
    double worst = 0;
    for (double x = -20; x <= 20; x += 0.0007) {
        double e = std::fabs(fast_exp2((float)x) / std::exp2(x) - 1.0);
        if (e > worst) worst = e;
    }
    CHECK(worst < 1.6e-4, "fast_exp2 relative error %g", worst);

    worst = 0;
    for (double x = -20; x <= 20; x += 0.0007) {
        double e = std::fabs(fast_sin((float)x) - std::sin(x));
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-5, "fast_sin absolute error %g", worst);

    worst = 0;
    for (double x = 10.0 / 48000; x <= 0.45; x += 1e-5) {
        double e = std::fabs(fast_tan_pi((float)x) / std::tan(M_PI * x) - 1.0);
        if (e > worst) worst = e;
    }
    CHECK(worst < 1e-4, "fast_tan_pi relative error %g", worst);

    // one_pole_coef: t * fs steps leave 1/100 of the distance, to 0.3 % (see
    // the error analysis at one_minus_exp2_neg), from 1 ms to 10 s
    const float times[] = {0.001f, 0.004f, 0.02f, 0.25f, 1.0f, 10.0f};
    for (float t : times) {
        const float c = one_pole_coef(t, kControlRate, kLog2_100);
        const int n = (int)(t * kControlRate + 0.5f);
        if (n < 1) continue;
        double x = 1.0;
        for (int i = 0; i < n; i++) x += c * (0.0 - x);
        // compare with the exact remainder for the rounded step count
        const double exact = std::pow(0.01, (double)n / (t * kControlRate));
        CHECK(std::fabs(x / exact - 1.0) < 3e-3, "one_pole t=%g: remainder %g, exact %g", t, x, exact);
    }

    CHECK(soft_clip(1.0f) == soft_clip(5.0f) && std::fabs(soft_clip(1.0f) - 2.0f / 3) < 1e-6, "soft clip bound");
}

static void test_env() {
    Env e;
    e.init();
    // attack 0.1 s from 0: reaches 1 at tick 0.1 * 3000 = 300
    int n = 0;
    while (e.stage != Env::DECAY && n < 10000) { e.tick(true, 0.1f, 0.2f, 0.5f, 0.3f, false); n++; }
    CHECK(std::abs(n - 300) <= 2, "attack took %d ticks, expected 300", n);
    // decay towards 0.5: after 0.2 s, 1 % of the distance (0.005) remains
    for (int i = 0; i < 600; i++) e.tick(true, 0.1f, 0.2f, 0.5f, 0.3f, false);
    CHECK(std::fabs(e.x - 0.5f) < 0.006f, "decay level %g", e.x);
    for (int i = 0; i < 3000; i++) e.tick(true, 0.1f, 0.2f, 0.5f, 0.3f, false);
    CHECK(e.stage == Env::SUSTAIN && e.x == 0.5f, "sustain stage %d level %g", e.stage, e.x);
    // release 0.3 s: 1 % of 0.5 left after 900 ticks; idle eventually
    for (int i = 0; i < 900; i++) e.tick(false, 0.1f, 0.2f, 0.5f, 0.3f, false);
    CHECK(std::fabs(e.x - 0.005f) < 0.001f, "release level %g", e.x);
    for (int i = 0; i < 6000; i++) e.tick(false, 0.1f, 0.2f, 0.5f, 0.3f, false);
    CHECK(e.stage == Env::IDLE && e.x == 0.0f, "idle after release");

    // cycle: a = 0.05, d = 0.05 loops while the gate is held
    e.init();
    int attacks = 0, prev = Env::IDLE;
    for (int i = 0; i < 3000; i++) {
        e.tick(true, 0.05f, 0.05f, 0.5f, 0.1f, true);
        if (e.stage == Env::ATTACK && prev != Env::ATTACK) attacks++;
        prev = e.stage;
        CHECK(e.x >= 0.0f && e.x <= 1.0f, "cycle level %g out of range", e.x);
    }
    CHECK(attacks >= 5 && attacks <= 12, "cycle: %d attacks in 1 s", attacks);
}

static void test_svf() {
    const float fcs[] = {10.0f, 200.0f, 1000.0f, 8000.0f, 21600.0f};
    const float qs[] = {0.5f, 0.7071f, 5.0f, 20.0f, 25.0f};
    for (float fc : fcs)
        for (float q : qs)
            for (int mode = 0; mode < 3; mode++) {
                Svf f;
                f.init();
                f.set(fc, q);
                float tail = 0;
                for (int i = 0; i < 200000; i++) {
                    float y = f.process(i == 0 ? 1.0f : 0.0f, mode);
                    if (!std::isfinite(y)) { tail = 1e9f; break; }
                    if (i > 199000) tail = std::fmax(tail, std::fabs(y));
                }
                CHECK(tail < 1e-3f, "svf fc %g q %g mode %d: impulse tail %g", fc, q, mode, tail);
            }
    // DC: LP passes, HP blocks
    Svf f;
    f.init();
    f.set(1000.0f, 0.7071f);
    float lp = 0, hp = 0;
    for (int i = 0; i < 48000; i++) lp = f.process(1.0f, 0);
    f.init();
    f.set(1000.0f, 0.7071f);
    for (int i = 0; i < 48000; i++) hp = f.process(1.0f, 2);
    CHECK(std::fabs(lp - 1.0f) < 1e-3f && std::fabs(hp) < 1e-3f, "DC: lp %g hp %g", lp, hp);
    // normalised band-pass: unit gain at the centre (exact for TPT with tan prewarp)
    f.init();
    f.set(1000.0f, 10.0f);
    float peak = 0;
    for (int i = 0; i < 96000; i++) {
        float y = f.process(std::sin(2 * M_PI * 1000.0 * i / 48000.0), 1);
        if (i > 48000) peak = std::fmax(peak, std::fabs(y));
    }
    CHECK(std::fabs(peak - 1.0f) < 0.01f, "band-pass peak gain %g", peak);
}

static void test_lfo_glide_delay() {
    for (int w = 0; w < kNumLfoWaves; w++) {
        Lfo l;
        l.init();
        int wraps = 0;
        float prev = 0;
        for (int i = 0; i < 30000; i++) {  // 10 s at 2 Hz
            float p = l.phase;
            float y = l.tick(2.0f, w);
            if (l.phase < p) wraps++;
            CHECK(y >= -1.0f && y <= 1.0f, "lfo wave %d value %g", w, y);
            prev = y;
        }
        (void)prev;
        CHECK(std::abs(wraps - 20) <= 1, "lfo wave %d: %d cycles in 10 s at 2 Hz", w, wraps);
    }

    Glide g;
    g.init();
    g.tick(60.0f, 0.5f);  // first note: no glide
    float p = 0;
    for (int i = 0; i < 1500; i++) p = g.tick(72.0f, 0.5f);  // 0.5 s
    CHECK(std::fabs(p - (72.0f - 0.12f)) < 0.02f, "glide after its time: %g (1 %% of 12 left)", p);

    static float mem[48000];
    Delay d;
    d.init(mem, 48000);
    // with the smoothed time settled at 0.25 s, an impulse comes back after 12000 samples
    for (int i = 0; i < 48000; i++) d.process(0.0f, 12000.0f, 0.0f);
    int at = -1;
    for (int i = 0; i < 20000; i++) {
        float y = d.process(i == 0 ? 1.0f : 0.0f, 12000.0f, 0.0f);
        if (y > 0.5f && at < 0) at = i;
    }
    CHECK(std::abs(at - 12000) <= 1, "delay echo at %d, expected 12000", at);
    // bound: |y| <= max|in| / (1 - fb) = 20 for fb = 0.95
    uint32_t rng = 1;
    float worst = 0;
    for (int i = 0; i < 480000; i++) {
        rng = rng * 1664525u + 1013904223u;
        float in = (float)(rng >> 8) / 8388608.0f - 1.0f;
        worst = std::fmax(worst, std::fabs(d.process(in, 3000.0f, 0.95f)));
    }
    CHECK(worst <= 20.0f, "delay output %g exceeds the bound 20", worst);
}

static void cc(Launchkey &k, int ch, int num, int v) { k.midi((uint8_t)(0xB0 | (ch - 1)), (uint8_t)num, (uint8_t)v); }

static void test_launchkey() {
    Launchkey k;
    k.init();
    k.tick();
    const Bus &b = bus();
    CHECK(b.shape == kInitShape, "init shape %d", b.shape);
    CHECK(std::fabs(b.master - (100.0f / 127) * (100.0f / 127)) < 1e-6f, "init master %g", b.master);
    CHECK(std::fabs(b.amp_a - 0.001f * std::pow(10.0f, 4.0f * 10 / 127)) < 1e-5f, "attack time %g", b.amp_a);

    // knob 1 sweeps every shape, monotonically
    int last = -1, seen = 0;
    for (int v = 0; v <= 127; v++) {
        cc(k, 1, 21, v);
        CHECK(k.shape >= last, "shape not monotone at %d", v);
        if (k.shape != last) seen++;
        last = k.shape;
    }
    CHECK(seen == kNumShapes && last == kNumShapes - 1, "knob 1 reached %d shapes, last %d", seen, last);

    // pads and banks
    k.midi(0x99, 40, 100); CHECK(k.shape == 0, "pad top-left -> %d", k.shape);
    k.midi(0x99, 51, 100); CHECK(k.shape == 7, "pad top-right -> %d", k.shape);
    k.midi(0x99, 36, 100); CHECK(k.shape == 8, "pad bottom-left -> %d", k.shape);
    k.midi(0x99, 47, 100); CHECK(k.shape == 15, "pad bottom-right -> %d", k.shape);
    cc(k, 16, 105, 127);
    k.midi(0x99, 40, 100); CHECK(k.shape == 16, "bank 2 pad top-left -> %d", k.shape);
    k.midi(0x99, 47, 100); CHECK(k.shape == 31, "bank 2 pad bottom-right -> %d", k.shape);
    k.midi(0x99, 40, 0);   CHECK(k.shape == 31, "pad release must not change the shape");

    // track < > step and wrap
    cc(k, 16, 103, 127); CHECK(k.shape == 32, "track > -> %d", k.shape);
    cc(k, 16, 103, 127); cc(k, 16, 103, 127); CHECK(k.shape == 0, "track > wraps -> %d", k.shape);
    cc(k, 16, 102, 127); CHECK(k.shape == 33, "track < wraps -> %d", k.shape);

    // fader button 1 cycles LP -> BP -> HP -> LP; release (0) does nothing
    cc(k, 1, 11, 127); cc(k, 1, 11, 0); CHECK(k.fmode == 1, "fmode %d", k.fmode);
    cc(k, 1, 11, 127); cc(k, 1, 11, 127); CHECK(k.fmode == 0, "fmode wraps %d", k.fmode);

    // pages and soft takeover
    cc(k, 1, 25, 100);  // knob 5 physically at 100 on the sound page (resonance)
    cc(k, 16, 52, 127); CHECK(k.page == PAGE_FX, "arrow down -> FX page");
    const uint8_t stored = k.knob[PAGE_FX][K_RAMOUNT];  // 40
    cc(k, 1, 25, 90); CHECK(k.knob[PAGE_FX][K_RAMOUNT] == stored, "knob not picked up yet must not change the value");
    cc(k, 1, 25, (int)stored + 1); CHECK(k.knob[PAGE_FX][K_RAMOUNT] == stored + 1, "pick-up within 2 steps");
    cc(k, 1, 25, 60); CHECK(k.knob[PAGE_FX][K_RAMOUNT] == 60, "picked knob follows");
    cc(k, 1, 26, 127);  // knob 6 at 127, stored reverb time 64
    cc(k, 1, 26, 10); CHECK(k.knob[PAGE_FX][K_RTIME] == 10, "crossing the stored value picks up");
    cc(k, 1, 19, 127); CHECK(k.page == PAGE_SOUND, "fader button 9 toggles the page");
    CHECK(k.knob[PAGE_SOUND][K_RESO] == 100, "sound page resonance kept at 100");

    // tap tempo: 1500 ticks = 0.5 s -> 2 Hz
    for (int i = 0; i < 100; i++) k.tick();
    cc(k, 16, 76, 127);
    for (int i = 0; i < 1500; i++) k.tick();
    cc(k, 16, 76, 127);
    CHECK(std::fabs(k.lfo_hz - 2.0f) < 1e-3f, "tap tempo -> %g Hz", k.lfo_hz);

    // pitch bend: full up = +2 semitones (to within one 14-bit step)
    k.midi(0xE0, 0x7F, 0x7F); k.tick();
    CHECK(std::fabs(b.pitch_mod - 2.0f) < 1e-3f, "bend up -> %g", b.pitch_mod);
    k.midi(0xE0, 0x00, 0x40); k.tick();
    CHECK(b.pitch_mod == 0.0f, "bend centre -> %g", b.pitch_mod);

    // Undo resets the sound, keeps the page
    cc(k, 16, 52, 127);
    cc(k, 16, 77, 127);
    CHECK(k.page == PAGE_FX && k.shape == kInitShape && k.fmode == 0 && k.knob[PAGE_FX][K_RAMOUNT] == 40,
          "reset: page %d shape %d fmode %d", k.page, k.shape, k.fmode);
}

int main() {
    test_fast_math();
    test_env();
    test_svf();
    test_lfo_glide_delay();
    test_launchkey();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
