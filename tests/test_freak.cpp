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
    // scaled pickup: knob at 100, stored 40 -> f(v) = 40 - (100 - v) 40 / 100
    CHECK(k.knob[PAGE_FX][K_RAMOUNT] == 40 && !k.picked[PAGE_FX][K_RAMOUNT], "FX reverb amount stored 40, not caught up");
    cc(k, 1, 25, 90); CHECK(k.knob[PAGE_FX][K_RAMOUNT] == 36, "scaled: 10 steps down move 40 -> 36 (got %d)", k.knob[PAGE_FX][K_RAMOUNT]);
    cc(k, 1, 25, 0); CHECK(k.knob[PAGE_FX][K_RAMOUNT] == 0 && k.picked[PAGE_FX][K_RAMOUNT], "meets the knob at 0");
    cc(k, 1, 25, 60); CHECK(k.knob[PAGE_FX][K_RAMOUNT] == 60, "picked knob follows");
    // a knob first seen on this page anchors there and then scales: 127 -> 10 takes reverb time 64 -> 5
    cc(k, 1, 26, 127); CHECK(k.knob[PAGE_FX][K_RTIME] == 64, "first position only anchors");
    cc(k, 1, 26, 10); CHECK(k.knob[PAGE_FX][K_RTIME] == 5, "scaled from the anchor (got %d)", k.knob[PAGE_FX][K_RTIME]);
    cc(k, 16, 51, 127); CHECK(k.page == PAGE_SOUND, "arrow up -> SOUND page");
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

// The reported failure: after a page round trip, a knob that moved meanwhile
// must still respond, in the direction it is turned, even one step at a time.
static void test_knobs_never_dead() {
    Launchkey k;
    k.init();
    for (int v = 64; v <= 100; v++) cc(k, 1, 22, v);  // timbre knob to 100 on SOUND
    CHECK(k.knob[PAGE_SOUND][K_TIMBRE] == 100, "timbre follows on the power-on page");
    cc(k, 16, 107, 127);                                 // Device Lock: FX page
    for (int v = 100; v >= 20; v--) cc(k, 1, 22, v);     // the knob moves on the FX page
    cc(k, 16, 106, 127);                                 // Device Select: back to SOUND
    // timbre is stored at 100, the knob sits at 20: turning it down one step
    // at a time must lower timbre (scaled pickup), never leave it stuck
    int prev = k.knob[PAGE_SOUND][K_TIMBRE];
    int moved = 0;
    for (int v = 19; v >= 0; v--) {
        cc(k, 1, 22, v);
        const int now = k.knob[PAGE_SOUND][K_TIMBRE];
        CHECK(now <= prev, "turning down raised the value (%d -> %d)", prev, now);
        if (now < prev) moved++;
        prev = now;
    }
    CHECK(moved >= 15, "only %d of 20 single steps moved the value", moved);
    CHECK(k.knob[PAGE_SOUND][K_TIMBRE] == 0 && k.picked[PAGE_SOUND][K_TIMBRE], "meets the knob at the end of travel");
    // turning up instead: from stored 100 with the knob at 20 the value rises
    // too, and both reach 127 together
    k.init();
    for (int v = 64; v <= 100; v++) cc(k, 1, 22, v);
    cc(k, 16, 52, 127);
    for (int v = 100; v >= 20; v--) cc(k, 1, 22, v);
    cc(k, 16, 51, 127);
    prev = k.knob[PAGE_SOUND][K_TIMBRE];
    for (int v = 21; v <= 127; v++) {
        cc(k, 1, 22, v);
        CHECK(k.knob[PAGE_SOUND][K_TIMBRE] >= prev, "turning up lowered the value");
        prev = k.knob[PAGE_SOUND][K_TIMBRE];
    }
    CHECK(prev == 127 && k.picked[PAGE_SOUND][K_TIMBRE], "meets at 127 (%d)", prev);
    // once caught up the knob follows exactly
    cc(k, 1, 22, 60);
    CHECK(k.knob[PAGE_SOUND][K_TIMBRE] == 60, "picked knob follows exactly");
}

// Pages: Device Select (106) / arrow up (51) = SOUND, Device Lock (107) /
// arrow down (52) = FX; the page event names the CC.
static void test_pages() {
    Launchkey k;
    k.init();
    Event e;
    cc(k, 16, 107, 127); CHECK(k.page == PAGE_FX, "Device Lock -> FX");
    CHECK(k.pop_event(e) && e.type == EV_PAGE && e.a == PAGE_FX && e.b == 107, "page event names CC 107");
    cc(k, 16, 107, 0); CHECK(k.page == PAGE_FX, "release changes nothing");
    cc(k, 16, 106, 127); CHECK(k.page == PAGE_SOUND, "Device Select -> SOUND");
    cc(k, 16, 52, 127); CHECK(k.page == PAGE_FX, "arrow down -> FX");
    cc(k, 16, 51, 127); CHECK(k.page == PAGE_SOUND, "arrow up -> SOUND");
    cc(k, 1, 19, 127); cc(k, 1, 19, 0); CHECK(k.page == PAGE_SOUND, "button 9 no longer changes the page");
}

// Presets: button 9 steps through them and wraps; Undo returns to preset 0.
static void test_presets() {
    // preset 0 is the power-on sound
    const Preset &p0 = kPresets[0];
    bool same = p0.shape == kInitShape && (p0.flags == P_KEYTRACK);
    for (int g = 0; g < NUM_PAGES; g++)
        for (int i = 1; i < 8; i++) same = same && p0.knob[g][i] == kKnobInit[g][i];
    same = same && p0.knob[PAGE_FX][0] == kKnobInit[PAGE_FX][0];
    for (int f = 0; f < 8; f++) same = same && p0.fader[f] == kFaderInit[f];
    CHECK(same, "preset 0 differs from the power-on values");
    // shape <-> knob 1 value round trip
    for (int s = 0; s < kNumShapes; s++)
        CHECK(((shape_knob(s) * kNumShapes) >> 7) == s && shape_knob(s) <= 127, "shape_knob(%d)", s);

    Launchkey k;
    k.init();
    for (int v = 100; v >= 0; v--) cc(k, 1, 71, v);   // amp attack fader physically at 0
    cc(k, 1, 79, 50);                                 // volume at 50
    cc(k, 1, 19, 127); cc(k, 1, 19, 0);               // button 9: preset 1
    CHECK(k.preset == 1 && k.shape == kPresets[1].shape && k.delay_on, "preset 1 applied (shape %d)", k.shape);
    CHECK(k.fader[F_MASTER] == 50, "presets keep the volume");
    CHECK(k.fader[F_AA] == kPresets[1].fader[F_AA] && !k.fpicked[F_AA], "attack set by the preset, fader not caught up");
    cc(k, 1, 71, 1);  // one step up from 0: scaled, no jump to 1
    CHECK(k.fader[F_AA] > kPresets[1].fader[F_AA] && k.fader[F_AA] < 127, "fader scaled pickup (%d)", k.fader[F_AA]);
    for (int i = 2; i <= kNumPresets; i++) { cc(k, 1, 19, 127); cc(k, 1, 19, 0); }
    CHECK(k.preset == 0, "button 9 wraps after %d presets (now %d)", kNumPresets, k.preset);
    cc(k, 1, 19, 127);
    cc(k, 16, 77, 127);
    CHECK(k.preset == 0 && k.shape == kInitShape, "Undo -> preset 0");
}

// Dice: SOUND page values in range; amp envelope, FX page and volume kept.
static void test_dice() {
    Launchkey k;
    k.init();
    uint8_t fx[8], amp[4];
    for (int i = 0; i < 8; i++) fx[i] = k.knob[PAGE_FX][i];
    for (int f = 0; f < 4; f++) amp[f] = k.fader[f];
    int shapes_seen = 0;
    bool seen[kNumShapes] = {false};
    for (int roll = 0; roll < 200; roll++) {
        for (int t = 0; t < 7 + roll; t++) k.tick();
        cc(k, 16, 74, 127);
        const uint8_t *s = k.knob[PAGE_SOUND];
        CHECK(k.shape >= 0 && k.shape < kNumShapes && ((s[K_SHAPE] * kNumShapes) >> 7) == k.shape, "dice shape %d", k.shape);
        CHECK(s[K_CUTOFF] >= 50 && s[K_CUTOFF] <= 120 && s[K_RESO] <= 80 && s[K_LFOAMT] <= 50, "dice ranges");
        CHECK(k.fmode >= 0 && k.fmode < 3 && k.lfo_wave < kNumLfoWaves && k.lfo_target < NUM_LFO_TARGETS, "dice toggles");
        if (!seen[k.shape]) { seen[k.shape] = true; shapes_seen++; }
    }
    bool kept = true;
    for (int i = 0; i < 8; i++) kept = kept && k.knob[PAGE_FX][i] == fx[i];
    for (int f = 0; f < 4; f++) kept = kept && k.fader[f] == amp[f];
    CHECK(kept && k.fader[F_MASTER] == kFaderInit[F_MASTER], "dice must not touch FX, amp envelope, volume");
    CHECK(shapes_seen >= 25, "200 rolls reached only %d of 34 shapes", shapes_seen);
}

// Exhaustive check of the scaled-pickup map for every anchor (a, s).
static void test_scaled_pickup_map() {
    int bad = 0;
    for (int a = 1; a <= 126; a++)
        for (int s = 0; s <= 127; s++) {
            int prev = -1;
            for (int v = 0; v <= 127; v++) {
                const int f = Launchkey::scaled(v, a, s);
                if (f < 0 || f > 127 || f < prev) bad++;
                prev = f;
            }
            if (Launchkey::scaled(a, a, s) != s || Launchkey::scaled(0, a, s) != 0 ||
                Launchkey::scaled(127, a, s) != 127)
                bad++;
        }
    CHECK(bad == 0, "scaled pickup: %d violations of monotone / f(a)=s / f(0)=0 / f(127)=127", bad);
}

// Event log: page changes, resets and unknown CCs are reported once.
static void test_events() {
    Launchkey k;
    k.init();
    Event e;
    CHECK(!k.pop_event(e), "no events at power-on");
    cc(k, 16, 52, 127);
    CHECK(k.pop_event(e) && e.type == EV_PAGE && e.a == PAGE_FX, "page event");
    cc(k, 1, 33, 5); cc(k, 1, 33, 6);
    CHECK(k.pop_event(e) && e.type == EV_UNMAPPED && e.a == 1 && e.b == 33 && e.c == 5, "unmapped CC event");
    CHECK(!k.pop_event(e), "an unmapped CC is reported once");
    cc(k, 16, 77, 127);
    CHECK(k.pop_event(e) && e.type == EV_RESET, "reset event");
}

int main() {
    test_fast_math();
    test_env();
    test_svf();
    test_lfo_glide_delay();
    test_launchkey();
    test_knobs_never_dead();
    test_scaled_pickup_map();
    test_events();
    test_pages();
    test_presets();
    test_dice();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
