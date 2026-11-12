// Host tests for the soliton patch: lib/pde_fft.h, lib/soliton_field.h and
// (below) the voice and the Launchkey mapping. make test
//
// What is checked, and against what:
//  - FFTs against a direct DFT in double precision.
//  - SOLITON (KdV): the Galerkin scheme conserves sum |u_k|^2 in continuous
//    time apart from the top-mode hyperviscosity, so the drift measures the
//    time stepping and that filter.
//  - SHOCK (Burgers): before the shock, max |u_y| = A / (1 - A t) exactly
//    for u0 = A sin y (characteristics), at the smallest viscosity.
//  - CHAOS (KS): in the linear regime mode 1 grows like exp((s^2 - s^4) t).
//  - ROGUE (NLS): the sidebands of a plane wave a grow like exp(sigma t),
//    sigma = q sqrt(4 a^2 - q^2) with q = s (modulational instability),
//    and the mass sum |psi_k|^2 does not grow.
//  - Every engine over the knob corners at the highest rate: finite,
//    bounded, and resolved (little energy in the top modes).
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>

#include "../lib/soliton_voice.h"

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

typedef std::complex<double> cd;

static double frand(uint32_t &s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (double)(s >> 8) / 16777216.0 * 2.0 - 1.0;
}

static void test_fft() {
    fft_init();
    uint32_t seed = 12345;
    double worst_c = 0, worst_r = 0, worst_i = 0;
    for (int n = 2; n <= 256; n *= 2) {
        std::vector<Cplx> x(n);
        std::vector<cd> xd(n);
        for (int j = 0; j < n; j++) {
            x[j].re = (float)frand(seed);
            x[j].im = (float)frand(seed);
            xd[j] = cd(x[j].re, x[j].im);
        }
        for (int inv = 0; inv < 2; inv++) {
            std::vector<Cplx> y = x;
            cfft(y.data(), n, inv != 0);
            for (int k = 0; k < n; k++) {
                cd s = 0;
                for (int j = 0; j < n; j++) s += xd[j] * std::polar(1.0, (inv ? 2 : -2) * M_PI * j * k / n);
                worst_c = std::fmax(worst_c, std::abs(cd(y[k].re, y[k].im) - s) / std::sqrt((double)n));
            }
        }
    }
    for (int n = 4; n <= 256; n *= 2) {
        std::vector<float> x(n), back(n);
        std::vector<Cplx> X(n / 2 + 1), z(n / 2);
        for (int j = 0; j < n; j++) x[j] = (float)frand(seed);
        rfft(x.data(), X.data(), n, z.data());
        for (int k = 0; k <= n / 2; k++) {
            cd s = 0;
            for (int j = 0; j < n; j++) s += (double)x[j] * std::polar(1.0, -2 * M_PI * j * k / n);
            s /= n;
            worst_r = std::fmax(worst_r, std::abs(cd(X[k].re, X[k].im) - s));
        }
        irfft(X.data(), back.data(), n, z.data());
        for (int j = 0; j < n; j++) worst_i = std::fmax(worst_i, std::fabs(back[j] - x[j]));
    }
    CHECK(worst_c < 2e-6, "cfft error %g (per sqrt n)", worst_c);
    CHECK(worst_r < 1e-6, "rfft error %g", worst_r);
    CHECK(worst_i < 2e-6, "irfft(rfft(x)) - x: %g", worst_i);
    printf("info: fft errors: cfft %.2g, rfft %.2g, round trip %.2g\n", worst_c, worst_r, worst_i);

    double worst_s = 0, worst_m = 0;
    for (double a = -1000; a <= 1000; a += 0.0137) {
        float c, s;
        sincos_unit((float)a, c, s);
        // compare at the float argument the function actually received
        const double af = (double)(float)a;
        worst_s = std::fmax(worst_s, std::fmax(std::fabs(c - std::cos(af)), std::fabs(s - std::sin(af))));
        worst_m = std::fmax(worst_m, std::fabs((double)c * c + (double)s * s - 1.0));
    }
    // The reduction to [-pi, pi] in float costs ~ulp(1000) = 6e-5 rad.
    CHECK(worst_s < 2e-4, "sincos_unit error %g", worst_s);
    CHECK(worst_m < 3e-7, "sincos_unit modulus error %g", worst_m);
    double worst_e = 0;
    for (double x = -20; x < 5; x += 0.001) worst_e = std::fmax(worst_e, std::fabs(exp_acc((float)x) / std::exp(x) - 1));
    CHECK(worst_e < 2e-4, "exp_acc relative error %g", worst_e);
    bool below = true;
    for (double x = -0.4999; x < 0; x += 1e-4) below = below && exp_acc((float)x) <= 1.0f;
    CHECK(below, "exp_acc(x) <= 1 for -0.5 < x < 0");
}


// A field driven by hand: settings fixed, one work item per call.
struct Rig {
    Field f;
    FieldCtl c;
    void start(int engine, float timbre, float color, float A, float seed, float rate, float shape = 0.0f) {
        f.init(1);
        c.engine = engine; c.timbre = timbre; c.color = color; c.damp = 0.0f; c.rate = rate; c.f0 = 110.0f;
        const float ph[3] = {0, 0, 0};
        f.note_on(c, A, shape, seed, ph, true);
        for (int i = 0; i < 4; i++) f.run_item(c);  // first table and the step boundary
    }
    // Physical time advanced over n steps (12 items each): the h of a step
    // is summed when its first stage starts.
    double run_steps(int n) {
        double t = 0;
        for (int i = 0; i < n * kItemsPerStep; i++) {
            if (f.item == 0) t += f.h;
            f.run_item(c);
        }
        return t;
    }
    double energy() { return (double)f.energy(); }
    // max |u_y| on a fine grid (spectral derivative)
    double max_uy() {
        double m = 0;
        for (int j = 0; j < 512; j++) {
            const double y = 2 * M_PI * j / 512;
            double d = 0;
            for (int k = 1; k <= kModes; k++) {
                const cd e = cd(f.v[k].re, f.v[k].im) * cd(0, k) * std::polar(1.0, k * y);
                d += 2 * e.real();
            }
            m = std::fmax(m, std::fabs(d));
        }
        return m;
    }
    double tail() {  // share of the energy in modes 36..42 (ROGUE: wavenumber k s >= 10)
        double t = 0, e = 0;
        if (f.engine == ENG_ROGUE) {
            for (int k = 0; k < kGrid; k++) {
                const int q = k <= kGrid / 2 ? k : kGrid - k;
                const double p = (double)f.v[k].re * f.v[k].re + (double)f.v[k].im * f.v[k].im;
                e += p;
                if (q * f.s >= 10.0f) t += p;  // physical wavenumber above 10 (kept: up to 14)
            }
        } else {
            for (int k = 1; k <= kModes; k++) {
                const double p = (double)f.v[k].re * f.v[k].re + (double)f.v[k].im * f.v[k].im;
                e += p;
                if (k >= 36) t += p;
            }
        }
        return e > 0 ? t / e : 0;
    }
};

static void test_kdv_energy() {
    // Zabusky-Kruskal-like: A cos y, the smallest delta (most solitons).
    // At the CFL limit, the fastest rate: 7500 steps is a 60 s note.
    static Rig g;
    g.start(ENG_SOLITON, 1.0f, 0.0f, 1.0f, 0.0f, 100.0f);
    const double e0 = g.energy();
    const double t = g.run_steps(7500);
    const double e1 = g.energy();
    printf("info: KdV delta %.3f: t = %.1f after 7500 steps, energy drift %.2e, tail %.1e\n", g.f.delta, t,
           e1 / e0 - 1, g.tail());
    CHECK(std::fabs(e1 / e0 - 1) < 0.03, "KdV energy drift %g", e1 / e0 - 1);
    CHECK(g.tail() < 1e-5, "KdV under-resolved: tail %g", g.tail());
    CHECK(t > 10, "KdV ran only to t = %g", t);
}

static void test_burgers_breaking() {
    static Rig g;
    const float A = 1.0f;
    g.start(ENG_SHOCK, 1.0f, 0.0f, A, 0.0f, 1000.0f);
    // fixed small steps for accuracy: rate = 0.002 per step
    g.c.rate = 0.002f / g.f.t_step;  // h = 0.002 per step, well inside the CFL limit
    double t = 0;
    while (t < 0.5 / A - 1e-9) t += g.run_steps(1);
    const double uy = g.max_uy(), exact = A / (1 - A * t);
    printf("info: Burgers at t = %.3f: max|u_y| %.4f, characteristics %.4f\n", t, uy, exact);
    CHECK(std::fabs(uy / exact - 1) < 0.01, "Burgers slope %g vs %g", uy, exact);
    // energy decays once the shock forms (t_b = 1/A), and stays bounded
    const double e0 = g.energy();
    g.c.rate = 1000.0f;
    g.run_steps(1250);
    const double e1 = g.energy();
    printf("info: Burgers energy after the shock: %.3f of the value at t = 0.5\n", e1 / e0);
    CHECK(e1 < 0.9 * e0, "Burgers energy did not decay: %g -> %g", e0, e1);
}

static void test_ks_growth() {
    static Rig g;
    g.start(ENG_CHAOS, 0.0f, 0.0f, 1e-4f, 0.0f, 1.0f);
    g.c.rate = 0.01f / g.f.t_step;
    const double s = g.f.s, lam = s * s - s * s * s * s;
    const double a0 = std::hypot(g.f.v[1].re, g.f.v[1].im);
    const double t = g.run_steps(500);
    const double a1 = std::hypot(g.f.v[1].re, g.f.v[1].im);
    const double measured = std::log(a1 / a0) / t;
    printf("info: KS mode 1 growth %.5f, linear theory %.5f (L/2pi = %.2f)\n", measured, lam, 1 / s);
    CHECK(std::fabs(measured / lam - 1) < 1e-3, "KS growth %g vs %g", measured, lam);
}

static void test_nls_mi() {
    static Rig g;
    const float a = 1.0f;
    g.start(ENG_ROGUE, 0.0f, 0.0f, a, 0.0f, 1.0f);  // eps = 1e-3
    g.c.rate = 0.004f / g.f.t_step;
    const double q = g.f.s, sigma = q * std::sqrt(4 * a * a - q * q);
    auto side = [&]() { return std::hypot(g.f.v[1].re, g.f.v[1].im); };
    auto mass = [&]() {
        double m = 0;
        for (int k = 0; k < kGrid; k++) m += (double)g.f.v[k].re * g.f.v[k].re + (double)g.f.v[k].im * g.f.v[k].im;
        return m;
    };
    const double m0 = mass();
    double t = g.run_steps(500);  // let the decaying part die out (e^{-2 sigma t} < 1e-2 at t = 2)
    const double s0 = side();
    const double dt = g.run_steps(500);  // t = 2 .. 4: past the decaying part, before saturation
    const double s1 = side();
    t += dt;
    const double measured = std::log(s1 / s0) / dt;
    printf("info: NLS sideband growth %.4f, theory %.4f (s = %.3f), mass drift %.1e at t = %.2f\n", measured, sigma,
           q, mass() / m0 - 1, t);
    CHECK(std::fabs(measured / sigma - 1) < 0.01, "NLS MI growth %g vs %g", measured, sigma);
    // On to the breather and beyond: the mass must not grow.
    g.c.rate = 50.0f;
    double mmax = 0;
    for (int i = 0; i < 40; i++) {
        g.run_steps(200);
        mmax = std::fmax(mmax, mass());
    }
    printf("info: NLS mass after the breathers: max %.6f of the start\n", mmax / m0);
    CHECK(mmax <= m0 * (1 + 1e-4), "NLS mass grew: %g", mmax / m0);
}

static void test_sweep() {
    // All engines, knob corners, the loudest notes, the fastest rate,
    // 20 s at 250 steps/s.
    static Rig g;
    const char *names[4] = {"SHOCK", "SOLITON", "CHAOS", "ROGUE"};
    for (int e = 0; e < NUM_ENGINES; e++) {
        double worst_tail = 0, worst_amp = 0, min_t = 1e9;
        for (int ti = 0; ti <= 2; ti++)
            for (int ci = 0; ci <= 1; ci++) {
                const float A = engine_amp_max(e);
                g.start(e, 0.5f * ti, (float)ci, A, 0.5f, 20.0f, (float)ci);
                const double e0 = g.energy();
                double t = 0;
                bool ok = true;
                for (int chunk = 0; chunk < 25 && ok; chunk++) {
                    t += g.run_steps(200);
                    const double en = g.energy();
                    ok = std::isfinite(en);
                    worst_amp = std::fmax(worst_amp, std::sqrt(en / e0));
                    worst_tail = std::fmax(worst_tail, g.tail());
                }
                CHECK(ok, "%s timbre %.1f color %d: not finite", names[e], 0.5 * ti, ci);
                min_t = std::fmin(min_t, t);
            }
        printf("info: %-7s worst tail %.1e, RMS up to %.1f x the start, slowest run t = %.1f in 20 s\n", names[e],
               worst_tail, worst_amp, min_t);
        CHECK(worst_tail < 1e-3, "%s under-resolved: tail %g", names[e], worst_tail);
        CHECK(worst_amp < 50, "%s grew %g x", names[e], worst_amp);
    }
}

static void test_rewind() {
    static Rig g;
    g.start(ENG_SOLITON, 0.6f, 0.0f, 1.0f, 0.0f, 3.0f);
    std::vector<Cplx> v0(g.f.v, g.f.v + kModes + 1);
    const double tf = g.run_steps(500);  // 2 s at 3 units/s
    g.c.rate = -3.0f;
    const double tb = g.run_steps(500);
    double d = 0, n = 0;
    for (int k = 1; k <= kModes; k++) {
        d += std::norm(cd(g.f.v[k].re - v0[k].re, g.f.v[k].im - v0[k].im));
        n += std::norm(cd(v0[k].re, v0[k].im));
    }
    printf("info: KdV forward %.2f then back %.2f: relative distance to the start %.1e\n", tf, tb, std::sqrt(d / n));
    // Not exact: RK4 is not time-symmetric, and the forward run's top-mode
    // hyperviscosity is not undone backwards (by design: it would amplify).
    CHECK(std::sqrt(d / n) < 3e-2, "rewind did not return: %g", std::sqrt(d / n));
}

static void test_table() {
    static Rig g;
    g.start(ENG_CHAOS, 1.0f, 0.0f, 1.0f, 0.5f, 20.0f);
    g.run_steps(3000);
    for (float f0 : {55.0f, 440.0f, 1760.0f, 4000.0f}) {
        g.c.f0 = f0;
        g.run_steps(1);
        const float *t = g.f.tab[g.f.tcur];
        std::vector<Cplx> X(kTableLen / 2 + 1), z(kTableLen / 2);
        rfft(t, X.data(), kTableLen, z.data());
        double above = 0, all = 0;
        for (int j = 1; j <= kTableLen / 2; j++) {
            const double p = std::norm(cd(X[j].re, X[j].im));
            all += p;
            if (j * f0 > kBandStop) above += p;
        }
        CHECK(above <= 1e-10 * all, "f0 %g: energy above 20 kHz %g of %g", f0, above, all);
        CHECK(t[kTableLen] == t[0], "guard sample");
    }
    // RMS of the table (low note: nothing removed) near kOutRms, CHAOS uses
    // the smoothed RMS of the field
    g.c.f0 = 55.0f;
    g.run_steps(20);
    const float *t = g.f.tab[g.f.tcur];
    double e = 0;
    for (int i = 0; i < kTableLen; i++) e += (double)t[i] * t[i];
    const double rms = std::sqrt(e / kTableLen);
    printf("info: CHAOS table RMS %.3f (target %.2f)\n", rms, kOutRms);
    CHECK(rms > 0.5 * kOutRms && rms < 2 * kOutRms, "table RMS %g", rms);
}

// ---------------------------------------------------------------- controller
// The mapping docs/soliton_cheatsheet.tex documents.
static_assert(std::is_trivially_destructible<SolVoice>::value, "polyphonic subpatch: no atexit");
static_assert(std::is_trivially_destructible<SolitonCtl>::value, "POD");
static_assert(std::is_trivially_destructible<Field>::value, "POD");

static void cc16(SolitonCtl &c, uint8_t num) { c.midi(0xBF, num, 127); }
static void pad(SolitonCtl &c, uint8_t note, uint8_t vel) { c.midi(0x99, note, vel); }

static void test_ctl() {
    static SolitonCtl c;
    c.init();
    c.tick();
    CHECK(sbus().engine == ENG_SOLITON, "power-on engine %d", sbus().engine);
    cc16(c, 103); c.tick();
    CHECK(sbus().engine == ENG_CHAOS, "track > : %d", sbus().engine);
    cc16(c, 103); cc16(c, 103); c.tick();
    CHECK(sbus().engine == ENG_SHOCK, "track > wraps: %d", sbus().engine);
    cc16(c, 102); c.tick();
    CHECK(sbus().engine == ENG_ROGUE, "track < wraps: %d", sbus().engine);

    // FIELD page (round button 104), knob 1 = RATE
    c.reset_sound();
    cc16(c, 104);
    CHECK(c.page == SPAGE_FIELD, "page %d", c.page);
    c.midi(0xB0, 21, 127);  // first position seen: scaled pickup from there
    c.midi(0xB0, 21, 0);
    c.midi(0xB0, 21, 127);
    c.tick();
    CHECK(std::fabs(sbus().rate - 20.0f) < 0.02f, "RATE at 127: %g", sbus().rate);
    const float r = sbus().rate;
    // bottom pads: FREEZE, REWIND, FAST, KICK (notes 36..39)
    pad(c, 36, 127); c.tick();
    CHECK(sbus().rate == 0.0f, "FREEZE full: %g", sbus().rate);
    pad(c, 36, 0); c.tick();
    CHECK(sbus().rate == r, "FREEZE released: %g", sbus().rate);
    pad(c, 37, 64); c.tick();
    CHECK(sbus().rate < 0.0f, "REWIND: %g", sbus().rate);
    pad(c, 37, 0);
    pad(c, 38, 127); c.tick();
    CHECK(std::fabs(sbus().rate / r - 8.0f) < 0.01f, "FAST full: x%g", sbus().rate / r);
    pad(c, 38, 0);
    const uint32_t k0 = sbus().kicks;
    pad(c, 39, 100); c.midi(0xA9, 39, 50); pad(c, 39, 0); c.tick();
    CHECK(sbus().kicks == k0 + 1, "one kick per strike, not per pressure: %u", sbus().kicks - k0);
    cc16(c, 75); c.tick();
    CHECK(sbus().rate == 0.0f && c.freeze_latch, "Quantise latches freeze");
    cc16(c, 75); c.tick();
    CHECK(sbus().rate == r, "latch off");
    // top pads: presets 0..7 (notes 40..43, 48..51)
    static const uint8_t top[8] = {40, 41, 42, 43, 48, 49, 50, 51};
    for (int i = 0; i < kNumSolPresets; i++) {
        pad(c, top[i], 100); c.tick();
        CHECK(c.preset == i && sbus().engine == kSolPresets[i].engine, "pad %d: preset %d engine %d", i, c.preset,
              sbus().engine);
    }
    for (int i = 0; i < 20; i++) {
        c.ticks += 977;
        c.dice();
        c.tick();
        CHECK(sbus().engine >= 0 && sbus().engine < NUM_ENGINES, "dice engine %d", sbus().engine);
    }
}

// --------------------------------------------------------------- voices
// Every preset, three voices with notes coming and going, an engine change
// and the pads mid-note: finite, bounded by the voice's own limit (gain
// <= 1/4 times soft_clip <= 2/3), silent when idle, and at most two
// voices' items in any tick.
static void test_voices() {
    static SolitonCtl c;
    static SolVoice v[3];
    double worst = 0;
    int nan = 0, max_items = 0;
    for (int p = 0; p < kNumSolPresets; p++) {
        c.init();
        c.apply_preset(p);
        c.tick();
        for (int i = 0; i < 3; i++) v[i].init();
        static const float notes[3] = {36, 60, 96};
        float o[kBlock];
        bool silent_ok = true;
        for (int k = 0; k < (int)(4.0f * kControlRate); k++) {
            const float t = k / kControlRate;
            if (k == (int)(1.0f * kControlRate)) cc16(c, 103);                 // engine change mid-note
            if (k == (int)(1.5f * kControlRate)) pad(c, 39, 120);              // kick
            if (k == (int)(1.6f * kControlRate)) pad(c, 39, 0);
            if (k == (int)(2.0f * kControlRate)) pad(c, 37, 127);              // rewind
            if (k == (int)(2.4f * kControlRate)) pad(c, 37, 0);
            c.tick();
            int items = 0;
            for (int i = 0; i < 3; i++) {
                const bool gate = (i == 0 && t < 3.0f) || (i > 0 && t > 0.5f && t < 2.5f);
                const int before = v[i].f.item;
                v[i].render(notes[i], gate, 0.5f + 0.25f * i, o);
                if (v[i].f.item != before) items++;
                for (int j = 0; j < kBlock; j++) {
                    if (!std::isfinite(o[j])) nan++;
                    worst = std::fmax(worst, std::fabs(o[j]));
                    if (v[i].idle(gate) && o[j] != 0.0f) silent_ok = false;
                }
            }
            max_items = std::max(max_items, items);
        }
        CHECK(silent_ok, "preset %d: an idle voice was not silent", p);
    }
    printf("info: voices: peak %.3f (limit %.3f), NaN %d, at most %d voice items per tick\n", worst, 0.25 * 2 / 3,
           nan, max_items);
    CHECK(nan == 0, "NaN in voice output: %d", nan);
    CHECK(worst <= 0.25 * 2.0 / 3.0 + 1e-6, "voice output %g above its bound", worst);
    CHECK(max_items <= 2, "%d voice items in one tick", max_items);
}

// Rewind is refused for the dissipative engines (backward heat equation:
// ill-posed), accepted for KdV and NLS.
static void test_rewind_rules() {
    for (int e = 0; e < NUM_ENGINES; e++) {
        static Rig g;
        g.start(e, 0.5f, 0.0f, 0.8f, 0.3f, 2.0f);
        g.run_steps(10);
        g.c.rate = -2.0f;
        g.run_steps(2);
        const bool back = g.f.h < 0.0f, frozen = g.f.h == 0.0f;
        CHECK(Field::reversible(e) ? back : frozen, "engine %d: rewind h = %g", e, g.f.h);
    }
}

int main() {
    test_fft();
    test_kdv_energy();
    test_burgers_breaking();
    test_ks_growth();
    test_nls_mi();
    test_rewind();
    test_table();
    test_sweep();
    test_rewind_rules();
    test_ctl();
    test_voices();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("soliton: all tests passed\n");
    return 0;
}
