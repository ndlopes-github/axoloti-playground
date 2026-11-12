// Host tests for the aeolian wind harp (lib/aeolian_harp.h) and its
// Launchkey mapping (lib/aeolian_ctl.h). make test
//
// What is checked, and against what:
//  - String tuning: the measured fundamental of a plucked string, against
//    the note, over the range and the brightness (the loop is tuned with
//    the loss filter's exact phase delay).
//  - Ring time: with a flat loss (BRIGHT full) every harmonic decays by g
//    per period, so the measured T60 must match the setting.
//  - Wake alone (no coupling): the van der Pol limit cycle, amplitude 2,
//    at the Strouhal frequency St U / d, up to 10 kHz.
//  - Lock-in: with coupling, a wake detuned 2 % above the 3rd harmonic is
//    pulled onto it; with the smallest coupling it is not.
//  - Wind: the OU gusts have the set standard deviation; Lorenz stays on
//    its attractor.
//  - Every preset and weather, the loudest wind: finite and bounded.
//  - The mapping the cheat sheet documents.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>

#include "../lib/aeolian_ctl.h"

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

static_assert(std::is_trivially_destructible<Harp>::value, "POD");
static_assert(std::is_trivially_destructible<AeolianCtl>::value, "POD");

// A bus with one string on, no wind.
static AeoBus quiet_bus(float note) {
    AeoBus b;
    for (int i = 0; i < kHarpStrings; i++) {
        b.note[i] = note; b.d[i] = 1.0f; b.on[i] = (i == 0); b.plucks[i] = 0;
    }
    b.wind = 0.0f; b.sigma = 0.0f; b.tau = 1.0f; b.weather = WX_STEADY; b.freeze = false;
    b.coupling = 0.001f; b.t60 = 5.0f; b.bright = 0.3f;  /* kappa */ b.air = 0.0f; b.body = 0.0f;
    b.glide = 0.0f; b.bend = 0.0f; b.pluck_amt = 1.0f;
    return b;
}

// The raw output of string 0 (before body and level control): n blocks.
static std::vector<float> run_string(Harp &h, AeoBus &b, int blocks) {
    std::vector<float> x;
    for (int k = 0; k < blocks; k++) {
        h.tick(b);
        float l[kBlock] = {0}, r[kBlock] = {0};
        h.st[0].process(l, r, kBlock, h.rng);
        for (int i = 0; i < kBlock; i++) x.push_back(l[i] + r[i]);
    }
    return x;
}

// Magnitude of the Hann-windowed DFT of x[start, start + n) at f Hz.
static double dft_mag(const std::vector<float> &x, size_t start, size_t n, double f) {
    double re = 0, im = 0;
    const double w = 2 * M_PI * f / kSampleRate;
    for (size_t i = 0; i < n; i++) {
        const double win = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1));
        re += win * x[start + i] * std::cos(w * i);
        im -= win * x[start + i] * std::sin(w * i);
    }
    return std::sqrt(re * re + im * im);
}

// The fundamental near `want`: the peak of the DFT magnitude, searched at
// 1 cent steps over +-20 cents, then at 0.02 cent steps around the best.
static double f0_peak(const std::vector<float> &x, size_t start, size_t n, double want) {
    double best = want, bm = -1;
    for (int c = -20; c <= 20; c++) {
        const double f = want * std::pow(2.0, c / 1200.0), m = dft_mag(x, start, n, f);
        if (m > bm) { bm = m; best = f; }
    }
    const double centre = best;
    for (int c = -50; c <= 50; c++) {
        const double f = centre * std::pow(2.0, c * 0.02 / 1200.0), m = dft_mag(x, start, n, f);
        if (m > bm) { bm = m; best = f; }
    }
    return best;
}
static void test_tuning() {
    static Harp h;
    double worst = 0;
    for (float note : {33.0f, 45.0f, 57.0f, 69.0f, 81.0f, 93.0f}) {
        for (float br : {1.0f, 0.2f, 0.02f}) {  // kappa: flat .. darkest
            h.init();
            AeoBus b = quiet_bus(note);
            b.bright = br;
            b.t60 = 20.0f;
            b.plucks[0] = 1;  // one pluck
            std::vector<float> x = run_string(h, b, 4000);
            const double want = 440.0 * std::pow(2.0, (note - 69) / 12.0);
            const double f = f0_peak(x, 4800, 48000, want);  // 1 s from 0.1 s
            const double cents = 1200 * std::log2(f / want);
            worst = std::fmax(worst, std::fabs(cents));
            CHECK(std::fabs(cents) < 3, "note %g bright %g: %.2f Hz, want %.2f (%.2f cents)", note, br, f, want, cents);
        }
    }
    printf("info: string tuning, worst %.2f cents over A1..A6 and the brightness range\n", worst);
}

static void test_ring_time() {
    static Harp h;
    for (float t60 : {0.5f, 2.0f, 8.0f}) {
        h.init();
        AeoBus b = quiet_bus(57);
        b.bright = 1.0f;  // kappa 1: flat loss
        b.t60 = t60;
        b.plucks[0] = 1;
        const int blocks = (int)(std::fmin(3.0f, t60) * kControlRate);
        std::vector<float> x = run_string(h, b, blocks);
        // The fundamental's amplitude (A3, 220 Hz) in 0.1 s windows at 10 %
        // and 60 % of the run: RING sets the fundamental's T60. (The
        // harmonics decay faster: the interpolation is a gentle low-pass.)
        const double f1 = 220.0;
        auto amp = [&](double t) { return dft_mag(x, (size_t)(t * kSampleRate), 4800, f1); };
        const double t1 = 0.1 * blocks / kControlRate, t2 = 0.6 * blocks / kControlRate;
        const double db = 20 * std::log10(amp(t1) / amp(t2));
        const double measured = 60.0 * (t2 - t1) / db;
        CHECK(std::fabs(measured / t60 - 1) < 0.05, "T60 %g s measured %.3f s", t60, measured);
        printf("info: ring time %.1f s: the fundamental's measured %.3f s\n", t60, measured);
    }
}

static void test_wake_free() {
    static Harp h;
    for (float fs_hz : {200.0f, 2000.0f, 10000.0f}) {
        h.init();
        h.local_amt = 0.0f;
        AeoBus b = quiet_bus(45);
        b.coupling = 0.0f;
        b.d[0] = 1.0f;
        b.wind = fs_hz * 1e-3f / kStrouhal;  // U = f d / St
        // let the limit cycle settle, then count rising zero crossings of q
        for (int k = 0; k < 3000; k++) {
            h.tick(b);
            float l[kBlock] = {0}, r[kBlock] = {0};
            h.st[0].process(l, r, kBlock, h.rng);
        }
        int zc = 0;
        double amax = 0;
        float qp = h.st[0].q;
        const int blocks = 6000;  // 2 s: +-1 crossing is 0.25 % at 200 Hz
        for (int k = 0; k < blocks; k++) {
            h.tick(b);
            for (int i = 0; i < kBlock; i++) {
                float l = 0, r = 0;
                h.st[0].process(&l, &r, 1, h.rng);
                const float q = h.st[0].q;
                if (qp < 0 && q >= 0) zc++;
                qp = q;
                amax = std::fmax(amax, std::fabs(q));
            }
        }
        const double f = zc / (blocks / kControlRate);
        printf("info: wake alone at St U/d = %.0f Hz: %.1f Hz, amplitude %.3f\n", fs_hz, f, amax);
        // The linear part is rotated exactly; the van der Pol term shifts the
        // frequency a little (O(eps^2) in continuous time, and the splitting
        // at large W dt): 1 % allowed.
        CHECK(std::fabs(f / fs_hz - 1) < 0.01, "wake frequency %g, want %g", f, fs_hz);
        CHECK(std::fabs(amax - 2.0) < 0.1, "van der Pol amplitude %g, want 2", amax);
    }
}

// Wake frequency / f1 over 2 s after 4 s, string 2 (excited at 0.15 of its
// length) at f1, the wake at ratio * f1; from below: the wind rises into
// the ratio over the 4 s instead of starting there.
static double wake_ratio(float f1, float ratio, float coupling, bool from_below = false) {
    static Harp h;
    const int str = 2;
    h.init();
    h.local_amt = 0.0f;
    const float note = 69.0f + 12.0f * std::log2(f1 / 440.0f);
    AeoBus b = quiet_bus(note);
    for (int i = 0; i < kHarpStrings; i++) b.on[i] = (i == str);
    b.coupling = coupling;
    const int settle = (int)(4 * kControlRate);
    for (int k = 0; k < settle; k++) {
        const float r = from_below ? ratio - 0.3f + 0.3f * (float)k / settle : ratio;
        b.wind = r * f1 * 1e-3f / kStrouhal;
        h.tick(b);
        float l[kBlock] = {0}, rr[kBlock] = {0};
        h.st[str].process(l, rr, kBlock, h.rng);
    }
    int zc = 0;
    float qp = h.st[str].q;
    for (int k = 0; k < (int)(2 * kControlRate); k++) {
        h.tick(b);
        for (int i = 0; i < kBlock; i++) {
            float l = 0, rr = 0;
            h.st[str].process(&l, &rr, 1, h.rng);
            if (qp < 0 && h.st[str].q >= 0) zc++;
            qp = h.st[str].q;
        }
    }
    return zc / 2.0 / f1;
}

static void test_lock_in() {
    const double locked = wake_ratio(110, 3.06f, 0.01f);
    const double free_ = wake_ratio(110, 3.06f, 0.001f);
    printf("info: wake at 3.06 f1: %.3f f1 with coupling 0.01, %.3f f1 with 0.001\n", locked, free_);
    CHECK(std::fabs(locked - 3.0) < 0.01, "not locked to the 3rd harmonic: %g", locked);
    CHECK(std::fabs(free_ - 3.0) > 0.03, "locked at the smallest coupling: %g", free_);
    // the lock-in band widens with coupling, and more so with the wind
    // rising into it (hysteresis, as in vortex-induced vibration)
    int w_lo = 0, w_hi = 0, w_hys = 0;
    for (float r = 2.80f; r <= 3.50f; r += 0.02f) {
        if (std::fabs(wake_ratio(110, r, 0.003f) - 3.0) < 0.01) w_lo++;
        if (std::fabs(wake_ratio(110, r, 0.03f) - 3.0) < 0.01) w_hi++;
        if (std::fabs(wake_ratio(110, r, 0.03f, true) - 3.0) < 0.01) w_hys++;
    }
    printf("info: lock-in band at 3 f1, in steps of 0.02 f1: %d at coupling 0.003, %d at 0.03, %d at 0.03 with the"
           " wind rising\n", w_lo, w_hi, w_hys);
    CHECK(w_hi > w_lo && w_hys > w_hi, "band: %d, %d, %d", w_lo, w_hi, w_hys);
}

static void test_wind() {
    static Harp h;
    h.init();
    AeoBus b = quiet_bus(45);
    b.wind = 5.0f; b.sigma = 0.3f; b.tau = 0.2f; b.weather = WX_GUSTY;
    double s1 = 0, s2 = 0;
    const int n = (int)(200 * kControlRate);  // 1000 correlation times
    for (int k = 0; k < n; k++) {
        h.tick(b);
        s1 += h.g_now;
        s2 += (double)h.g_now * h.g_now;
    }
    const double mean = s1 / n, sd = std::sqrt(s2 / n - mean * mean);
    printf("info: OU gusts: mean %.4f, sd %.4f (set 0.3)\n", mean, sd);
    // Standard error of the mean of an OU process over T: sigma sqrt(2 tau / T)
    // = 0.3 sqrt(0.4 / 200) = 0.013; allow 3 of them.
    CHECK(std::fabs(sd / 0.3 - 1) < 0.1 && std::fabs(mean) < 0.04, "OU mean %g sd %g", mean, sd);
    b.weather = WX_LORENZ; b.tau = 0.2f;
    double xmax = 0;
    for (int k = 0; k < n; k++) {
        h.tick(b);
        xmax = std::fmax(xmax, std::fabs(h.lx));
    }
    printf("info: Lorenz |x| max %.1f\n", xmax);
    CHECK(xmax < 25 && xmax > 10, "Lorenz x max %g", xmax);
    b.freeze = true;
    const float u = h.U;
    for (int k = 0; k < 3000; k++) h.tick(b);
    CHECK(h.U == u, "freeze moved the wind");
}

static void test_presets() {
    static AeolianCtl c;
    static Harp h;
    double worst = 0;
    int nan = 0;
    for (int p = 0; p < kNumAeoPresets; p++)
        for (int wx = 0; wx < NUM_WEATHER; wx++) {
            c.init();
            c.apply_preset(p);
            c.weather = wx;
            c.modwheel = 1.0f;
            c.perf[APERF_GUST] = 1.0f;  // the most wind there is: x2 x2.5
            c.perf[APERF_STORM] = 1.0f;
            h.init();
            for (int k = 0; k < (int)(15 * kControlRate); k++) {
                if (k == 3000) c.key_on(40, 120);
                if (k == 6000) c.key_on(52, 60);
                if (k == 9000) c.pluck_all(1.0f);
                c.tick();
                h.tick(aeobus());
                float l[kBlock], r[kBlock];
                h.render(aeobus(), l, r);
                for (int i = 0; i < kBlock; i++) {
                    if (!std::isfinite(l[i]) || !std::isfinite(r[i])) nan++;
                    worst = std::fmax(worst, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
                }
            }
        }
    printf("info: presets x weathers at the strongest wind: peak %.3f, NaN %d\n", worst, nan);
    CHECK(nan == 0, "NaN: %d", nan);
    CHECK(worst <= 2.0 / 3.0 + 1e-6, "output %g above the soft clip's bound", worst);
}

static void cc16(AeolianCtl &c, uint8_t num) { c.midi(0xBF, num, 127); }

// The controller publishes every other tick: two ticks always publish.
static void pub(AeolianCtl &c) {
    c.tick();
    c.tick();
}

static void test_ctl() {
    static AeolianCtl c;
    c.init();
    pub(c);
    CHECK(c.tuning == TUNE_UNISON && aeobus().weather == WX_GUSTY, "power-on: breeze");
    c.midi(0x90, 50, 100);  // D3
    pub(c);
    for (int i = 0; i < kHarpStrings; i++)
        CHECK(std::fabs(aeobus().note[i] - 50.0f) < 0.1f, "UNISON string %d: %g", i, aeobus().note[i]);
    cc16(c, 103);  // HARMONIC
    pub(c);
    CHECK(c.tuning == TUNE_HARMONIC, "track > : %d", c.tuning);
    CHECK(std::fabs(aeobus().note[2] - (50 + 19.0196f)) < 1e-3f && std::fabs(aeobus().note[7] - 86.0f) < 1e-3f,
          "harmonic series: %g %g", aeobus().note[2], aeobus().note[7]);
    cc16(c, 103); cc16(c, 103);  // CHORD
    c.midi(0x90, 60, 100); c.midi(0x90, 64, 100); c.midi(0x90, 67, 100);
    pub(c);
    // newest first: 67 64 60 50, then an octave up
    CHECK(aeobus().note[0] == 67 && aeobus().note[1] == 64 && aeobus().note[3] == 50 && aeobus().note[4] == 79,
          "chord: %g %g %g %g", aeobus().note[0], aeobus().note[1], aeobus().note[3], aeobus().note[4]);
    c.midi(0x90, 20, 100);  // below A1: raised by octaves
    CHECK(c.root == 44, "low root %g", c.root);
    // faders = diameters, buttons = strings on/off
    c.midi(0xB0, 71, 127); c.midi(0xB0, 71, 0); c.midi(0xB0, 71, 127);
    pub(c);
    CHECK(std::fabs(aeobus().d[0] - 3.0f) < 0.01f, "fader 1 full: %g mm", aeobus().d[0]);
    c.midi(0xB0, 13, 127);
    pub(c);
    CHECK(!aeobus().on[2], "button 3 switches string 3 off");
    // weather buttons, freeze
    cc16(c, 117); pub(c);
    CHECK(aeobus().weather == WX_LORENZ, "Record = LORENZ");
    cc16(c, 115); pub(c);
    CHECK(aeobus().weather == WX_STEADY, "Play = STEADY");
    cc16(c, 75); pub(c);
    CHECK(aeobus().freeze, "Quantise = freeze");
    // pads: CALM full = no wind; PLUCK counts
    const float w = aeobus().wind;
    c.midi(0x99, 37, 127); pub(c);
    CHECK(aeobus().wind == 0.0f, "CALM full: wind %g", aeobus().wind);
    c.midi(0x99, 37, 0); pub(c);
    CHECK(aeobus().wind == w, "CALM released");
    const uint32_t p0 = aeobus().plucks[0];
    c.midi(0x99, 39, 100); pub(c);
    CHECK(aeobus().plucks[0] == p0 + 1, "PLUCK");
    for (int i = 0; i < 20; i++) {
        c.ticks += 977;
        c.dice();
        pub(c);
        CHECK(c.tuning >= 0 && c.tuning < NUM_TUNINGS && c.weather >= 0 && c.weather < NUM_WEATHER, "dice");
    }
}

int main() {
    test_tuning();
    test_ring_time();
    test_wake_free();
    test_lock_in();
    test_wind();
    test_presets();
    test_ctl();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("aeolian: all tests passed\n");
    return 0;
}
