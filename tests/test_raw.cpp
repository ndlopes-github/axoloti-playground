// Host tests for the raw patch (lib/raw_voice.h, lib/raw_ctl.h). make test
//
// Checked against exact results where there are any: the filters' linear
// response against the bilinear transform of the analog transfer function,
// the self-oscillation threshold k = 2 and its frequency, the Newton solve
// against a fully converged one, the decimator's pass and stop bands,
// zero-mean pulses, envelope timing; and bounds (no NaN, bounded output)
// over random settings. Measured numbers (aliasing, solver error) are
// printed.
#include <cmath>
#include <cstdio>
#include <vector>

#include "../lib/raw_ctl.h"

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

static const double kPiD = 3.14159265358979323846;

// Amplitude of the component at frequency f (Hz, rate fs) in x[n0..n1), by
// least squares on sin and cos over a whole number of samples.
static double amp_at(const std::vector<float> &x, size_t n0, size_t n1, double f, double fs) {
    double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0;
    for (size_t n = n0; n < n1; n++) {
        const double s = std::sin(2 * kPiD * f * n / fs), c = std::cos(2 * kPiD * f * n / fs);
        ss += s * s; cc += c * c; sc += s * c; xs += x[n] * s; xc += x[n] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
    return std::sqrt(a * a + b * b);
}

// Exact response of the trapezoidal (bilinear, prewarped at fc) filter.
static double sk_mag(bool hp, double f, double fc, double fs, double k) {
    const double W = std::tan(kPiD * f / fs) / std::tan(kPiD * fc / fs);  // s = jW
    const double re = 1 - W * W, im = (2 - k) * W;
    const double num = hp ? W * W : 1.0;
    return num / std::sqrt(re * re + im * im);
}

// raw_G against the exact g/(1+g), g = tan(pi fc/fs): the bound in raw_voice.h.
static void test_raw_G() {
    double worst = 0;
    for (double fc = 5; fc <= kRawMaxCut; fc *= 1.01) {
        const double g = std::tan(kPiD * fc / kRawRate), want = g / (1 + g);
        worst = std::fmax(worst, std::fabs(raw_G((float)fc) - want));
    }
    std::printf("raw_G: max |error| %.2e (bound 4e-7 + rounding)\n", worst);
    CHECK(worst < 6e-7, "raw_G error %.2e", worst);
}

static void test_filter_linear() {
    const double fs = kRawRate;
    const double fcs[3] = {200, 2000, 12000};
    const double ks[3] = {0.0, 1.0, 1.8};
    const double fr[5] = {0.25, 0.7, 1.0, 1.4, 4.0};
    double worst = 0;
    for (int hp = 0; hp < 2; hp++)
        for (double fc : fcs)
            for (double k : ks)
                for (double r : fr) {
                    const double f = fc * r;
                    if (f > 0.45 * fs / 2) continue;
                    Sk35 s;
                    s.init();
                    const float G = raw_G((float)fc);
                    const size_t n = 96000;
                    std::vector<float> y(n);
                    for (size_t i = 0; i < n; i++) {
                        const float x = (float)(1e-3 * std::sin(2 * kPiD * f * i / fs));
                        y[i] = hp ? s.hp(x, G, (float)k) : s.lp(x, G, (float)k);
                    }
                    const double got = amp_at(y, n / 2, n, f, fs) / 1e-3;
                    // raw_G clamps nothing here, but fast_tan_pi has its own error:
                    // compare with the filter it actually built.
                    const double g = G / (1.0 - G);
                    const double fc_eff = std::atan(g) * fs / kPiD;
                    const double want = sk_mag(hp, f, fc_eff, fs, k);
                    const double rel = std::fabs(got - want) / want;
                    if (rel > worst) worst = rel;
                    CHECK(rel < 1e-3, "%s fc %.0f k %.1f f %.0f: |H| %.6f, exact %.6f", hp ? "HPF" : "LPF", fc, k, f, got, want);
                }
    std::printf("filters, linear: worst relative error of |H| vs the bilinear transform %.2e\n", worst);
}

// Zero crossings per second of x[n0..n1) / 2 = frequency.
static double freq_zc(const std::vector<float> &x, size_t n0, size_t n1, double fs) {
    int zc = 0;
    for (size_t i = n0 + 1; i < n1; i++)
        if ((x[i] >= 0) != (x[i - 1] >= 0)) zc++;
    return zc * fs / (2.0 * (n1 - n0));
}
static double rms(const std::vector<float> &x, size_t n0, size_t n1) {
    double s = 0;
    for (size_t i = n0; i < n1; i++) s += (double)x[i] * x[i];
    return std::sqrt(s / (n1 - n0));
}

static void test_self_oscillation() {
    const double fs = kRawRate;
    for (int hp = 0; hp < 2; hp++)
        for (double fc : {150.0, 1000.0, 5000.0}) {
            for (double k : {1.9, 2.1, 2.2}) {
                Sk35 s;
                s.init();
                const float G = raw_G((float)fc);
                const size_t n = 96000;
                std::vector<float> y(n);
                for (size_t i = 0; i < n; i++) {
                    const float x = i == 0 ? 0.1f : 0.0f;
                    y[i] = hp ? s.hp(x, G, (float)k) : s.lp(x, G, (float)k);
                }
                const double r1 = rms(y, n / 2, 3 * n / 4), r2 = rms(y, 3 * n / 4, n);
                if (k < 2) {
                    CHECK(r2 < 1e-6, "%s fc %.0f k %.1f should decay: rms %.2e", hp ? "HPF" : "LPF", fc, k, r2);
                } else {
                    const double f = freq_zc(y, n / 2, n, fs);
                    CHECK(std::fabs(f / fc - 1) < 0.03, "%s fc %.0f k %.1f oscillates at %.1f Hz", hp ? "HPF" : "LPF", fc, k, f);
                    CHECK(r2 > 0.05 && std::fabs(r2 / r1 - 1) < 0.02, "%s fc %.0f k %.1f: rms %.4f then %.4f (steady?)",
                          hp ? "HPF" : "LPF", fc, k, r1, r2);
                    // describing function of the cubic clip, N(a) = 1 - a^2/4 for
                    // a <= 1: k N(a) = 2 gives the amplitude of the fed-back
                    // signal's input; printed, not checked (the loop also
                    // filters the clip's harmonics).
                    if (fc == 1000.0)
                        std::printf("self-oscillation %s k %.1f at fc %.0f: %.1f Hz, peak-ish %.3f (cubic-clip estimate %.3f)\n",
                                    hp ? "HPF" : "LPF", k, fc, f, r2 * std::sqrt(2.0), std::sqrt(4 * (1 - 2 / k)));
                }
            }
        }
}

// The Newton solve. Two filters that differ only in the solve cannot be
// compared over time once they self-oscillate: their phases drift apart
// although both are right. So: (1) the local error, at every sample from
// the same state, of kNewton steps against 40 (r is strictly increasing
// with r' >= 0.45, so |y - y*| <= |r(y)| / 0.45 and the residual bounds
// the error); (2) whole trajectories below self-oscillation (k <= 1.9).
static void solve_sweep(float kmax, double *local, double *traj, double *peak) {
    Sk35 a, b;
    a.init();
    b.init();
    *local = *traj = *peak = 0;
    float p = 0;
    const size_t n = 4 * 96000;
    for (size_t i = 0; i < n; i++) {
        const double t = (double)i / n;
        const float fc = (float)(50.0 * std::pow(300.0, 0.5 - 0.5 * std::cos(6 * kPiD * t)));  // 50 Hz .. 15 kHz, 3 sweeps
        const float G = raw_G(fc), H = 1.0f - G;
        const float k = (float)(kmax * std::fmin(1.0, 1.2 * t));
        p += 110.0f / kRawRate;
        if (p >= 1) p -= 1;
        const float x = 2 * p - 1;  // naive saw, full scale: steps every period
        // local: the solve that b.lp is about to do, and a converged one
        const float A = G * (G * x + H * b.s1) + H * b.s2, kB = k * G * H;
        const float y1 = Sk35::solve(b.y, A, kB);
        float yc = b.y;
        for (int it = 0; it < 20; it++) yc = Sk35::solve(yc, A, kB);
        *local = std::fmax(*local, std::fabs(y1 - yc));
        const float ya = a.lp(x, G, k), yb = b.lp(x, G, k);
        (void)ya;
        *peak = std::fmax(*peak, std::fabs(yb));
        CHECK(std::isfinite(yb), "NaN in the solve");
    }
    // trajectories: a with kNewton steps, c converged at every sample
    struct Conv : Sk35 {
        float lp_conv(float x, float G, float k) {
            const float H = 1.0f - G;
            const float A = G * (G * x + H * s1) + H * s2;
            float ys = y;
            for (int i = 0; i < 20; i++) ys = solve(ys, A, k * G * H);
            const float z = k * clip3(ys);
            const float l1 = G * (x - z) + H * s1;
            s1 = 2.0f * l1 - s1;
            const float out = G * (z + l1) + H * s2;
            s2 = 2.0f * out - s2;
            y = out;
            return out;
        }
    } c;
    a.init();
    c.init();
    p = 0;
    for (size_t i = 0; i < n; i++) {
        const double t = (double)i / n;
        const float fc = (float)(50.0 * std::pow(300.0, 0.5 - 0.5 * std::cos(6 * kPiD * t)));
        const float G = raw_G(fc);
        const float k = (float)(std::fmin(kmax, 1.9f) * std::fmin(1.0, 1.2 * t));
        p += 110.0f / kRawRate;
        if (p >= 1) p -= 1;
        const float x = 2 * p - 1;
        *traj = std::fmax(*traj, std::fabs(a.lp(x, G, k) - c.lp_conv(x, G, k)));
    }
}

static void test_solver() {
    double local, traj, peak;
    solve_sweep(kPeakMax, &local, &traj, &peak);
    std::printf("solver: %d Newton steps, local error max %.2e (output peak %.3f); trajectory to k 1.9 max difference %.2e\n",
                kNewton, local, peak, traj);
    CHECK(local < 1e-3, "Newton local error %.2e", local);
    CHECK(traj < 1e-3, "trajectory difference %.2e", traj);
}

static void test_halfband() {
    HalfbandCoefs hc;
    raw_halfband_design(hc);
    double dc = 0.5;
    for (int i = 0; i < kHbPairs; i++) dc += 2 * hc.c[i];
    std::printf("half-band: DC gain %.6f\n", dc);
    double pass = 0, stop = 0;
    for (double f = 100; f < 48000; f += 997) {
        if (f > 20000 && f < 28000) continue;
        Halfband d;
        d.init();
        const size_t n = 9600;
        std::vector<float> y(n);
        for (size_t i = 0; i < n; i++) {
            const float a = (float)std::sin(2 * kPiD * f * (2 * i) / 96000.0);
            const float b = (float)std::sin(2 * kPiD * f * (2 * i + 1) / 96000.0);
            y[i] = d.process(hc, a, b);
        }
        const double fo = f < 24000 ? f : 48000 - f;  // where it lands at 48 kHz
        const double g = amp_at(y, 200, n, fo, 48000.0);
        if (f <= 20000) pass = std::fmax(pass, std::fabs(20 * std::log10(g)));
        else stop = std::fmax(stop, g);
    }
    std::printf("half-band: pass band (to 20 kHz) within %.4f dB, stop band (from 28 kHz) %.1f dB\n", pass,
                20 * std::log10(stop));
    CHECK(pass < 0.01, "pass-band ripple %.4f dB", pass);
    CHECK(20 * std::log10(stop) < -65, "stop band only %.1f dB", 20 * std::log10(stop));
}

// Alias-to-signal ratio (dB) up to 20 kHz of a periodic waveform rendered
// at 96 kHz and decimated: energy outside +-6 bins of the harmonics of f0,
// over a Blackman-Harris window.
template <class F>
static double alias_db(F wave, double f0) {
    HalfbandCoefs hc;
    raw_halfband_design(hc);
    Halfband d;
    d.init();
    const int n = 16384;
    std::vector<float> y(n + 100);
    float p = 0;
    const float dt = (float)(f0 / 96000.0), idt = 1 / dt;
    for (int i = 0; i < n + 100; i++) {
        float s[2];
        for (int o = 0; o < 2; o++) {
            s[o] = wave(p, dt, idt);
            p += dt;
            if (p >= 1) p -= 1;
        }
        y[i] = d.process(hc, s[0], s[1]);
    }
    double sig = 0, alias = 0;
    for (int b = 1; b * 48000.0 / n < 20000; b++) {
        double re = 0, im = 0;
        for (int i = 0; i < n; i++) {
            const double t = 2 * kPiD * i / (n - 1);
            const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
            re += w * y[100 + i] * std::cos(2 * kPiD * b * i / n);
            im -= w * y[100 + i] * std::sin(2 * kPiD * b * i / n);
        }
        const double pw = re * re + im * im, f = b * 48000.0 / n;
        const double h = f / f0, dist = std::fabs(h - std::round(h)) * f0 / (48000.0 / n);
        if (dist <= 6 && std::round(h) >= 1) sig += pw;  // Blackman-Harris main lobe: +-4 bins
        else alias += pw;
    }
    return 10 * std::log10(alias / sig);
}

static void test_oscillators() {
    // pulses: zero mean for every width (exactly, for the naive pulse; the
    // BLEP corrections are odd about each edge, so they add none either)
    for (float w : {0.5f, 0.25f, 0.05f}) {
        const float dt = 311.13f / 96000.0f, idt = 1 / dt;
        double s = 0;
        float p = 0;
        const int n = 96000 * 3;
        for (int i = 0; i < n; i++) {
            s += raw_pulse(p, w, dt, idt);
            p += dt;
            if (p >= 1) p -= 1;
        }
        CHECK(std::fabs(s / n) < 2e-3, "pulse w %.2f mean %.2e", w, s / n);
    }
    const double f0 = 2793.83;  // F7, not a divisor of the rates
    const double saw = alias_db([](float p, float dt, float idt) { return raw_saw(p, dt, idt); }, f0);
    const double saw_naive = alias_db([](float p, float, float) { return 2 * p - 1; }, f0);
    const double tri = alias_db([](float p, float dt, float idt) { return raw_tri(p, dt, idt); }, f0);
    const double tri_naive = alias_db([](float p, float, float) { return 4 * (p < 0.5f ? 0.5f - p : p - 0.5f) - 1; }, f0);
    const double sq = alias_db([](float p, float dt, float idt) { return raw_pulse(p, 0.5f, dt, idt); }, f0);
    std::printf("aliasing at %.0f Hz (to 20 kHz, after 2x decimation): saw %.1f dB (naive %.1f), triangle %.1f dB (naive %.1f), square %.1f dB\n",
                f0, saw, saw_naive, tri, tri_naive, sq);
    CHECK(saw < saw_naive - 10, "PolyBLEP saw no better than naive");
    CHECK(tri < tri_naive - 10, "PolyBLAMP triangle no better than naive");
}

static void test_envelopes() {
    // EG2 attack from 0 reaches 1 after the attack time
    EnvCoefs c;
    env_coefs(c, 0.0f, 0.1f, 0.2f, 0.5f, 0.3f);
    RawEnv e;
    e.init();
    e.trigger(false);
    int n = 0;
    while (e.stage == RawEnv::ATTACK && n < 100000) { eg2_tick(e, c, true); n++; }
    CHECK(std::fabs(n / 4800.0 - 1) < 0.01, "EG2 attack 0.1 s took %d samples", n);
    // decay: 99 % of the way to the sustain level in 0.2 s
    int m = 0;
    while (e.x - 0.5f > 0.005f && m < 100000) { eg2_tick(e, c, true); m++; }
    CHECK(std::fabs(m / 9600.0 - 1) < 0.01, "EG2 decay 0.2 s took %d samples", m);
    // hold: after the key goes up the level stays for the hold time
    env_coefs(c, 0.25f, 0.001f, 0.001f, 0.7f, 0.05f);
    e.init();
    e.trigger(false);
    for (int i = 0; i < 4800; i++) eg2_tick(e, c, true);
    int h = 0;
    while (e.stage != RawEnv::RELEASE && h < 100000) { eg2_tick(e, c, false); h++; }
    CHECK(std::abs(h - 12000) <= 1, "EG2 hold 0.25 s: release after %d samples", h);
    // EG1: nothing during the delay, then attack
    EnvCoefs c1;
    env_coefs(c1, 0.1f, 0.05f, 0.0f, 1.0f, 0.1f);
    RawEnv g;
    g.init();
    g.trigger(true);
    int d = 0;
    while (g.x == 0.0f && d < 100000) { eg1_tick(g, c1, true); d++; }
    CHECK(std::abs(d - 4801) <= 1, "EG1 delay 0.1 s: rose after %d samples", d);
    int a = 0;
    while (g.stage == RawEnv::ATTACK && a < 100000) { eg1_tick(g, c1, true); a++; }
    CHECK(std::fabs((a + 1) / 2400.0 - 1) < 0.01, "EG1 attack 0.05 s took %d samples", a + 1);
}

// PITCH<EG1 sweeps into the note: a held note ends in tune whatever the
// amount, and starts off by it. Pitch from zero crossings of a saw through
// an open LPF (A3 = 220 Hz), counted over the last half second.
static double voice_freq(float peg1, double t0, double t1) {
    static RawVoice v;
    static RawCtl rc;
    v.init();
    rc.init();  // publishes preset 0; the fields below override it
    RawBus b = rawbus();
    b.note = 57; b.gate = true; b.trig = 1;
    b.wave1 = W1_SAW; b.lvl1 = 1; b.lvl2 = 0; b.noise = 0;
    b.lp_oct = 10; b.lp_k = 0; b.hp_oct = 0; b.hp_k = 0; b.lp_eg = b.hp_eg = 0; b.lp_mg = b.hp_mg = 0;
    b.pitch_mg = 0; b.bend = 0; b.glide = 0; b.keytrack = 0;
    b.pitch_eg1 = peg1; b.e1_d = 0; b.e1_a = 0.05f; b.e1_r = 0.1f;
    b.e2_a = 0.001f; b.e2_s = 1; b.e2_h = 0;
    std::vector<float> y;
    for (int k = 0; k < (int)(t1 * kControlRate) + 1; k++) {
        float o[kBlock];
        v.render(b, o);
        for (float s : o) y.push_back(s);
    }
    return freq_zc(y, (size_t)(t0 * 48000), (size_t)(t1 * 48000), 48000.0);
}

static void test_pitch_eg1() {
    for (float a : {0.0f, 12.0f, -12.0f}) {
        const double f = voice_freq(a, 0.5, 1.0);
        CHECK(std::fabs(1200 * std::log2(f / 220.0)) < 5, "PITCH<EG1 %.0f: held note at %.2f Hz, not 220", a, f);
    }
    // in the first 10 ms (EG1 attack 50 ms) the pitch is near the start offset
    const double hi = voice_freq(12.0f, 0.0, 0.01);
    CHECK(hi > 330.0, "PITCH<EG1 +12: starts at %.0f Hz, should start near 440", hi);
}

// The voice: random settings, loud, no NaN, bounded output.
static void test_voice_bounds() {
    static RawVoice v;
    v.init();
    RawBus b = RawBus();
    uint32_t r = 12345;
    auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r >> 8) * (1.0f / 16777216.0f); };
    double peak = 0;
    int bad = 0;
    for (int round = 0; round < 60; round++) {
        b.note = 24 + 72 * rnd();
        b.gate = rnd() < 0.8f;
        b.trig++;
        b.vel = rnd();
        b.glide = rnd() < 0.5f ? 0.0f : rnd();
        b.bend = 4 * rnd() - 2;
        b.wave1 = (int)(4 * rnd()) % 4;
        b.wave2 = (int)(4 * rnd()) % 4;
        b.oct1 = (int)(4 * rnd()) % 4 - 2;
        b.oct2 = (int)(4 * rnd()) % 4 - 1;
        b.pw = 0.05f + 0.45f * rnd();
        b.pitch2 = 24 * rnd() - 12;
        b.lvl1 = rnd(); b.lvl2 = rnd(); b.noise = rnd();
        b.drive = 0.5f * fast_exp2(4 * rnd());
        b.hp_oct = 10 * rnd(); b.lp_oct = 10 * rnd();
        b.hp_k = kPeakMax * rnd(); b.lp_k = kPeakMax * rnd();
        b.hp_mg = 4 * rnd(); b.lp_mg = 4 * rnd(); b.hp_eg = 6 * rnd(); b.lp_eg = 8 * rnd();
        b.keytrack = rnd() < 0.5f ? 0.0f : 1.0f;
        b.pitch_mg = 12 * rnd(); b.pitch_eg1 = 48 * rnd() - 24;
        b.mg_hz = 0.1f * fast_exp2(9 * rnd()); b.mg_shape = 2 * rnd() - 1; b.mg_sh = rnd() < 0.3f;
        b.sh_lp = 3 * rnd();
        b.e1_d = rnd(); b.e1_a = rnd(); b.e1_r = rnd();
        b.e2_h = rnd(); b.e2_a = 0.5f * rnd(); b.e2_d = rnd(); b.e2_s = rnd(); b.e2_r = rnd();
        b.velamp = rnd();
        b.level = 1.0f;
        for (int k = 0; k < 600; k++) {
            float out[kBlock];
            v.render(b, out);
            for (int i = 0; i < kBlock; i++) {
                if (!std::isfinite(out[i])) bad++;
                else peak = std::fmax(peak, std::fabs(out[i]));
            }
        }
    }
    std::printf("voice, 60 random settings: peak %.3f, non-finite %d\n", peak, bad);
    CHECK(bad == 0, "non-finite output");
    CHECK(peak < 4.0, "output peak %.3f", peak);
}

// The controller: mono keys, legato, hold, pages, presets, dice.
static RawCtl c;
static void key(uint8_t n, uint8_t v = 100) { c.midi(0x90, n, v); }
static void off(uint8_t n) { c.midi(0x80, n, 0); }
static void cc16(uint8_t n) { c.midi(0xBF, n, 127); c.midi(0xBF, n, 0); }

static void test_keys() {
    c.init();
    c.legato = false;
    key(60);
    CHECK(c.gate && c.note == 60 && c.trig == 1, "first key");
    key(64);
    CHECK(c.note == 64 && c.trig == 2, "second key retriggers (legato off)");
    off(64);
    CHECK(c.gate && c.note == 60 && c.trig == 2, "release falls back to the held key, no retrigger");
    off(60);
    CHECK(!c.gate, "all keys up: gate off");
    c.midi(0x90, 62, 0);  // note on, velocity 0 = release of a key not held
    CHECK(!c.gate && c.nkeys == 0, "stray release");
    // legato: overlapping keys do not retrigger; a detached key does
    c.legato = true;
    key(60);
    const uint32_t t0 = c.trig;
    key(67);
    CHECK(c.trig == t0 && c.note == 67, "legato: no retrigger");
    off(60);
    CHECK(c.note == 67 && c.gate, "releasing a key that is not sounding changes nothing");
    off(67);
    key(65);
    CHECK(c.trig == t0 + 1, "legato: a detached key retriggers");
    off(65);
    // hold: gate stays on; a new key is a new phrase; hold off releases
    cc16(115);
    CHECK(c.hold, "Play turns HOLD on");
    key(48);
    off(48);
    CHECK(c.gate && c.note == 48, "hold keeps the note");
    const uint32_t t1 = c.trig;
    key(50);
    CHECK(c.trig == t1 + 1 && c.note == 50, "a key after all are up retriggers, even with legato");
    off(50);
    cc16(115);
    CHECK(!c.hold && !c.gate, "hold off releases");
    // the key stack keeps the newest kKeyStack keys
    c.legato = false;
    for (int i = 0; i < 20; i++) key((uint8_t)(40 + i));
    CHECK(c.nkeys == kKeyStack && c.note == 59, "stack overflow handled");
    for (int i = 19; i >= 0; i--) off((uint8_t)(40 + i));
    CHECK(c.nkeys == 0 && !c.gate, "stack emptied");
    // the bus follows (published every other tick)
    key(72, 127);
    c.tick(); c.tick();
    CHECK(rawbus().note == 72.0f && rawbus().gate && rawbus().vel == 1.0f, "bus");
    off(72);
}

static void test_mapping() {
    c.init();
    // pages: Device Select / 106 SOUND, 104 MOD, Device Lock / 107 FX
    cc16(104);
    CHECK(c.page == RPAGE_MOD, "104 -> MOD");
    cc16(107);
    CHECK(c.page == RPAGE_FX, "107 -> FX");
    cc16(51);
    CHECK(c.page == RPAGE_SOUND, "51 -> SOUND");
    // waves and scales cycle through every value
    for (int i = 0; i < NUM_W1; i++) cc16(103);
    CHECK(c.wave1 == kRawPresets[0].wave1, "track > cycles VCO1's waves");
    cc16(102);
    CHECK(c.wave1 == (kRawPresets[0].wave1 + NUM_W1 - 1) % NUM_W1, "track < steps back");
    int seen = 0;
    for (int i = 0; i < 4; i++) { c.midi(0xB0, 11, 127); seen |= 1 << (c.oct1 + 2); }
    CHECK(seen == 15, "VCO1 scale takes 32' 16' 8' 4'");
    seen = 0;
    for (int i = 0; i < 4; i++) { c.midi(0xB0, 13, 127); seen |= 1 << (c.oct2 + 1); }
    CHECK(seen == 15, "VCO2 scale takes 16' 8' 4' 2'");
    // value maps at the ends of travel
    CHECK(std::fabs(raw_pitch2(64)) < 1e-6f && std::fabs(raw_pitch2(127) - 12) < 1e-4f && std::fabs(raw_pitch2(1) + 12) < 1e-4f,
          "VCO2 pitch: 0 at the centre, +-12");
    CHECK(raw_peak(127) == kPeakMax && raw_peak(115) < 2.0f && raw_peak(117) > 2.0f, "PEAK: self-oscillation from 116");
    CHECK(std::fabs(raw_mg_hz(0) - 0.1f) < 1e-4f && std::fabs(raw_mg_hz(127) - 51.2f) < 0.05f, "MG 0.1 .. 51 Hz");
    // every preset publishes finite values within the voice's ranges
    for (int p = 0; p < kNumRawPresets; p++) {
        c.apply_preset(p);
        c.publish();
        const RawBus &r = rawbus();
        CHECK(r.wave1 >= 0 && r.wave1 < NUM_W1 && r.wave2 >= 0 && r.wave2 < NUM_W2, "preset %d waves", p);
        CHECK(r.oct1 >= -2 && r.oct1 <= 1 && r.oct2 >= -1 && r.oct2 <= 2, "preset %d scales", p);
        CHECK(r.hp_k >= 0 && r.hp_k <= kPeakMax && r.lp_k >= 0 && r.lp_k <= kPeakMax, "preset %d peaks", p);
        CHECK(r.pw >= 0.05f && r.pw <= 0.5f && r.drive >= 0.5f && r.drive <= 8.0f, "preset %d pw/drive", p);
    }
    // dice: within its ranges, and Undo restores preset 0
    for (int i = 0; i < 50; i++) {
        c.ticks += 977;
        cc16(74);
        CHECK(c.wave1 != W1_NOISE && c.knob[RPAGE_SOUND][RK_LVL1] >= 60, "dice keeps a pitched VCO1");
    }
    cc16(77);
    CHECK(c.preset == 0 && c.knob[RPAGE_SOUND][RK_LPCUT] == kRawPresets[0].knob[RPAGE_SOUND][RK_LPCUT], "Undo resets");
    // SCREAM pad at full pressure: both peaks at the maximum
    c.midi(0x99, 36, 127);
    c.publish();
    CHECK(rawbus().hp_k == kPeakMax && rawbus().lp_k == kPeakMax, "SCREAM");
    c.midi(0x99, 36, 0);
    c.publish();
    CHECK(rawbus().lp_k == raw_peak(kRawPresets[0].knob[RPAGE_SOUND][RK_LPPEAK]), "SCREAM released");
}

int main() {
    test_keys();
    test_mapping();
    test_raw_G();
    test_filter_linear();
    test_self_oscillation();
    test_solver();
    test_halfband();
    test_oscillators();
    test_envelopes();
    test_pitch_eg1();
    test_voice_bounds();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("raw: all tests passed\n");
    return 0;
}
