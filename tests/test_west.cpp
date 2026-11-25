// Host tests for the westcoast voices (lib/west_voice.h). make test
//
// Exact results where there are any: the sine against libm; the folder's
// harmonics against the Jacobi-Anger expansion (2 J_n(beta)); through-zero
// FM against phase modulation (sidebands J_k(I/r)); the high-pass corner.
// Measured and printed: the vactrol's strike and tail, the function
// generator's timing and modes, aliasing through the decimator, bounds.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "../lib/west_ctl.h"

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

// J_n(x) = (1/pi) int_0^pi cos(n t - x sin t) dt (Bessel's integral), by the
// midpoint rule with 4000 points (the integrand is smooth and periodic:
// the error is far below what the tests need).
static double bessel_j(int n, double x) {
    const int N = 4000;
    double s = 0;
    for (int i = 0; i < N; i++) {
        const double t = kPiD * (i + 0.5) / N;
        s += std::cos(n * t - x * std::sin(t));
    }
    return s / N;
}

// Amplitude of the component at frequency f (cycles per sample) in x.
static double amp_at(const std::vector<double> &x, double f) {
    double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0;
    for (size_t n = 0; n < x.size(); n++) {
        const double s = std::sin(2 * kPiD * f * n), c = std::cos(2 * kPiD * f * n);
        ss += s * s; cc += c * c; sc += s * c; xs += x[n] * s; xc += x[n] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
    return std::sqrt(a * a + b * b);
}

static void test_sine() {
    double worst = 0;
    for (int i = 0; i < 100000; i++) {
        const double p = i / 100000.0;
        worst = std::fmax(worst, std::fabs(w_psin((float)p) - std::sin(2 * kPiD * p)));
    }
    double worst2 = 0;
    for (int i = -20000; i <= 20000; i++) {
        const double u = i * 0.00075;  // |u| <= 15
        worst2 = std::fmax(worst2, std::fabs(w_sin((float)u) - std::sin(u)));
    }
    double worst3 = 0, worst4 = 0;
    const float *S = wtab().s;
    for (int i = 0; i < 100000; i++) {
        const double p = i / 100000.0;
        worst3 = std::fmax(worst3, std::fabs(w_tsin(S, (float)p) - std::sin(2 * kPiD * p)));
    }
    for (int i = -20000; i <= 20000; i++) {
        const double u = i * 0.00075;
        worst4 = std::fmax(worst4, std::fabs(w_tsin_u(S, (float)u) - std::sin(u)));
    }
    std::printf("table sine: w_tsin max error %.2e, w_tsin_u (|u| <= 15) %.2e (bound 1e-5)\n", worst3, worst4);
    CHECK(worst3 < 1e-5 && worst4 < 1e-5, "table sine error %.2e / %.2e", worst3, worst4);
    std::printf("w_psin max error %.2e, w_sin (|u| <= 15) %.2e\n", worst, worst2);
    CHECK(worst < 5e-6, "w_psin error %.2e", worst);
    CHECK(worst2 < 2e-5, "w_sin error %.2e", worst2);
    // the triangle: in phase with the sine, peaks +-1
    CHECK(std::fabs(w_tri_wave(0.0f)) < 1e-6 && std::fabs(w_tri_wave(0.25f) - 1) < 1e-6 &&
          std::fabs(w_tri_wave(0.5f)) < 1e-6 && std::fabs(w_tri_wave(0.75f) + 1) < 1e-6, "triangle phase");
}

// The folder on a pure sine: for c = 0 and a = 1,
// sin(beta sin th) = 2 sum_k J_{2k+1}(beta) sin((2k+1) th); at t = 0 the
// plain sine.
static void test_folder() {
    const int n = 4096, period = 64;  // exactly 64 periods
    double worst = 0;
    for (double t : {0.125, 0.3, 0.6, 1.0}) {
        float a, beta;
        w_timbre((float)t, a, beta);
        std::vector<double> y(n);
        for (int i = 0; i < n; i++) {
            const float x = w_psin((float)((i % period) / (double)period));
            y[i] = w_fold(x, a, beta, 0.0f);
        }
        for (int h = 1; h <= 15; h++) {
            const double want = (h % 2) ? 2 * std::fabs(bessel_j(h, beta)) : 0.0;
            worst = std::fmax(worst, std::fabs(amp_at(y, (double)h / period) - want));
        }
    }
    std::printf("folder: harmonics 1-15 vs 2|J_n(beta)|, max |error| %.2e\n", worst);
    CHECK(worst < 1e-3, "folder harmonics off by %.2e", worst);
    // t = 0: the plain sine
    std::vector<double> y(n);
    float a, beta;
    w_timbre(0.0f, a, beta);
    for (int i = 0; i < n; i++) y[i] = w_fold(w_psin((float)((i % period) / (double)period)), a, beta, 0.0f);
    double harm = 0;
    for (int h = 2; h <= 15; h++) harm = std::fmax(harm, amp_at(y, (double)h / period));
    CHECK(harm < 1e-5 && std::fabs(amp_at(y, 1.0 / period) - 1) < 1e-5, "TIMBRE 0 is not a sine (harmonics %.2e)", harm);
    // symmetry brings even harmonics in
    w_timbre(0.5f, a, beta);
    for (int i = 0; i < n; i++) y[i] = w_fold(w_psin((float)((i % period) / (double)period)), a, beta, 0.7f);
    CHECK(amp_at(y, 2.0 / period) > 0.05, "SYMMETRY gave no 2nd harmonic");
}

// Through-zero FM with a sine MO at ratio r and index I is phase modulation
// with index I/r: sidebands at f (1 + k r) with amplitudes |J_k(I/r)|. A
// non-integer r keeps the lines (and their reflections below 0 Hz) apart.
static void test_fm() {
    WestParams P = WestParams();
    P.ratio = 0.37f; P.mo_track = true; P.mo_shape = 0; P.timbre = 0; P.sym = 0;
    P.lpg_mode = WL_VCA; P.ping = false; P.lpg_bias = 1; P.lpg_decay = 0.3f;
    P.rise = P.fall = 0.01f; P.fmode = WF_SUSTAIN; P.fresp = WR_LIN; P.level = 1;
    double worst = 0;
    for (float I : {0.5f, 1.5f}) {  // 1.5: the phase runs backwards for part of each cycle
        P.fm = I;
        WestVoice v;
        v.init(1);
        v.note = 57;  // 220 Hz
        v.gate = true;
        v.trig = 1;
        std::vector<float> o96(2 * kBlock);
        std::vector<double> y;
        for (int b = 0; b < 3000; b++) {
            for (float &s : o96) s = 0;
            v.render(P, 0, o96.data());
            if (b >= 1000) for (float s : o96) y.push_back(s);
        }
        const double f = 220.0 / 96000.0, beta = I / P.ratio;
        // the VCA gain is v^2 with v -> 1 (bias 1); the high-pass at 15 Hz
        // passes these lines (>= 0.1 f = 22 Hz) with a small loss: compare
        // ratios to the carrier
        const double c0 = amp_at(y, f) / std::fabs(bessel_j(0, beta));
        for (int k = -2; k <= 3; k++) {
            const double fk = std::fabs(f * (1 + k * P.ratio));
            if (fk * 96000 < 60) continue;  // too close to the high-pass
            const double got = amp_at(y, fk) / c0, want = std::fabs(bessel_j(k, beta));
            worst = std::fmax(worst, std::fabs(got - want));
        }
    }
    std::printf("through-zero FM: sidebands vs |J_k(I/r)|, max |error| %.2e (relative to the carrier)\n", worst);
    CHECK(worst < 0.02, "FM sidebands off by %.2e", worst);
}

static void test_hp() {
    const double g = std::tan(kPiD * 15 / 96000.0), want = g / (1 + g);
    CHECK(std::fabs(kWHpG - want) < 1e-8, "kWHpG %.9f, want %.9f", kWHpG, want);
}

// The vactrol: a strike at full velocity, then the tail, for T = 0.3 s.
static void test_vactrol() {
    WestVoice v;
    v.init(1);
    const float T = 0.3f, klin = 1.0f / (T * kSampleRate), koff = 6.0f * klin;
    float peak = 0;
    int tpeak = 0, t20 = -1, t40 = -1, t_idle = -1;
    for (int n = 0; n < 48000 * 10; n++) {
        v.vactrol_step(n < (int)kWStrike ? 1.0f : 0.0f, koff, klin);
        if (v.v > peak) { peak = v.v; tpeak = n; }
        const float gain = v.v * v.v;
        if (n > tpeak && t20 < 0 && gain < 0.1f * peak * peak) t20 = n;
        if (n > tpeak && t40 < 0 && gain < 0.01f * peak * peak) t40 = n;
        if (t_idle < 0 && n > tpeak && v.v < 1e-4f) t_idle = n;
    }
    std::printf("vactrol (T 0.3 s): strike peak %.2f at %.1f ms; gain -20 dB after %.0f ms, -40 dB after %.0f ms; idle after %.2f s\n",
                peak, tpeak / 48.0, (t20 - tpeak) / 48.0, (t40 - tpeak) / 48.0, t_idle / 48000.0);
    CHECK(peak > 0.6f && peak < 0.9f, "strike peak %.2f", peak);
    CHECK(t_idle > 0 && t_idle < 48000 * 4, "vactrol never idles");
    // never outside [0, 1]
    v.init(1);
    bool ok = true;
    srand(3);
    for (int n = 0; n < 200000; n++) {
        v.vactrol_step(rand() / (float)RAND_MAX, 6.0f / (0.02f * kSampleRate), 1.0f / (0.02f * kSampleRate));
        ok = ok && v.v >= 0 && v.v <= 1;
    }
    CHECK(ok, "vactrol left [0, 1]");
}

static void test_function() {
    WestParams P = WestParams();
    P.rise = 0.1f; P.fall = 0.2f; P.fresp = WR_LIN;
    const float up = 1.0f / (P.rise * kSampleRate), down = 1.0f / (P.fall * kSampleRate);
    for (int mode : {WF_TRANSIENT, WF_SUSTAIN, WF_CYCLE}) {
        P.fmode = mode;
        WestVoice v;
        v.init(1);
        v.gate = true;
        v.fstage = WestVoice::F_RISE;
        int rise_n = 0, fall_n = 0, cycles = 0;
        int last = WestVoice::F_RISE;
        for (int n = 0; n < 48000; n++) {  // 1 s, key held
            v.func_step(P, up, down);
            if (v.fstage == WestVoice::F_RISE) rise_n++;
            if (v.fstage == WestVoice::F_FALL) fall_n++;
            if (last == WestVoice::F_FALL && v.fstage == WestVoice::F_RISE) cycles++;
            last = v.fstage;
        }
        if (mode == WF_TRANSIENT)
            CHECK(std::abs(rise_n - 4800) <= 2 && std::abs(fall_n - 9600) <= 2 && v.fstage == WestVoice::F_IDLE,
                  "transient: rise %d fall %d", rise_n, fall_n);
        if (mode == WF_SUSTAIN) CHECK(v.fstage == WestVoice::F_HOLD && v.r == 1.0f, "sustain does not hold");
        if (mode == WF_CYCLE) CHECK(cycles == 3, "cycle: %d cycles in 1 s, want 3 (0.3 s each)", cycles);
    }
}

// Aliasing: the voice through the decimator, folded at full TIMBRE, gate
// open. Alias-to-signal ratio to 20 kHz (energy off the harmonics).
static void fft(std::vector<double> &re, std::vector<double> &im) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * kPiD / len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; k++) {
                const double wr = std::cos(ang * k), wi = std::sin(ang * k);
                const double xr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                const double xi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k + len / 2] = re[i + k] - xr;
                im[i + k + len / 2] = im[i + k] - xi;
                re[i + k] += xr;
                im[i + k] += xi;
            }
    }
}
static double alias_db(const std::vector<float> &y, double f0) {
    const int n = 16384;
    std::vector<double> re(n), im(n, 0.0);
    for (int i = 0; i < n; i++) {
        const double t = 2 * kPiD * i / (n - 1);
        re[i] = (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t)) * y[4000 + i];
    }
    fft(re, im);
    double sig = 0, alias = 0;
    for (int b = 1; b * 48000.0 / n < 20000; b++) {
        const double pw = re[b] * re[b] + im[b] * im[b], f = b * 48000.0 / n;
        const double h = f / f0, dist = std::fabs(h - std::round(h)) * f0 / (48000.0 / n);
        if (dist <= 6 && std::round(h) >= 1) sig += pw;
        else alias += pw;
    }
    return 10 * std::log10(alias / sig);
}
static void test_aliasing() {
    WestParams P = WestParams();
    P.ratio = 1; P.mo_track = true; P.fm = 0; P.sym = 0.3f;
    P.lpg_mode = WL_VCA; P.ping = false; P.lpg_bias = 1; P.lpg_decay = 0.3f;
    P.rise = P.fall = 0.01f; P.fmode = WF_SUSTAIN; P.level = 1;
    std::printf("aliasing (folded, TIMBRE 1, decimated to 48 kHz, to 20 kHz):");
    for (int note : {60, 72, 84, 96}) {
        P.timbre = 1;
        WestVoice v;
        v.init(1);
        v.note = (uint8_t)note;
        v.gate = true;
        v.trig = 1;
        WestMix mix;
        mix.init();
        std::vector<float> o96(2 * kBlock), out;
        float o[kBlock];
        for (int b = 0; b < 1400; b++) {
            for (float &s : o96) s = 0;
            v.render(P, 0, o96.data());
            mix.process(o96.data(), o);
            for (float s : o) out.push_back(s);
        }
        const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        std::printf(" %.0f Hz %.1f dB;", f0, alias_db(out, f0));
    }
    std::printf("\n");
}

// Random settings: finite, and |out| <= 2 level (|fold| <= 1, the high-pass
// output |y - lp| <= 2, the gate's one-poles have G < 1/2 and gain <= 1).
static void test_bounds() {
    srand(5);
    auto u = [] { return rand() / (float)RAND_MAX; };
    double worst = 0;
    bool finite = true;
    for (int t = 0; t < 200; t++) {
        WestParams P = WestParams();
        P.ratio = 0.25f + 7.75f * u(); P.mo_track = rand() % 2; P.mo_hz = 0.1f + 500 * u();
        P.mo_shape = u(); P.fm = 8 * u(); P.timbre = u(); P.sym = 2 * u() - 1;
        P.range = rand() % 4 - 2; P.bend = 4 * u() - 2; P.glide = u() < 0.5f ? 0 : u();
        P.rise = 0.001f + u(); P.fall = 0.001f + u(); P.fmode = rand() % NUM_WF; P.fresp = rand() % NUM_WR;
        P.f_timbre = u(); P.f_fm = 4 * u(); P.lpg_mode = rand() % NUM_WL; P.ping = rand() % 2;
        P.lpg_decay = 0.02f + 2 * u(); P.lpg_bias = u() * u(); P.rnd_note = u(); P.drift = u(); P.level = 1;
        WestVoice v;
        v.init((uint32_t)t + 1);
        v.note = (uint8_t)(24 + rand() % 84);
        v.gate = true;
        v.trig = 1;
        v.vel = u();
        std::vector<float> o96(2 * kBlock);
        for (int b = 0; b < 600; b++) {
            if (b == 300) v.gate = false;
            for (float &s : o96) s = 0;
            v.render(P, 2 * u() - 1, o96.data());
            for (float s : o96) {
                finite = finite && std::isfinite(s);
                worst = std::fmax(worst, std::fabs(s));
            }
        }
    }
    std::printf("random settings: max |voice out| %.2f (bound 2)\n", worst);
    CHECK(finite, "non-finite output");
    CHECK(worst <= 2.0 + 1e-4, "voice output %.3f above its bound", worst);
}


// The controller (lib/west_ctl.h): keys, presets, pickup, snap, dice.
static WestCtl wc;
static void ev(uint8_t s, uint8_t d1, uint8_t d2) { wc.midi(s, d1, d2); wc.tick(); }
static void test_ctl() {
    wc.init();
    const WestParams &P = wbus().P;
    CHECK(P.ping && P.mo_track && P.lpg_mode == WL_COMBO, "power-on is not preset 1 (bongo)");
    for (int n : {48, 52, 55, 59, 62}) ev(0x90, (uint8_t)n, 100);
    int on = 0;
    for (int i = 0; i < kWVoices; i++) on += wbus().gate[i];
    CHECK(on == 4, "%d voices on", on);
    bool has48 = false;
    for (int i = 0; i < kWVoices; i++) has48 = has48 || (wbus().gate[i] && wbus().note[i] == 48);
    CHECK(!has48, "the fifth key did not steal the oldest");
    for (int n : {52, 55, 59, 62}) ev(0x80, (uint8_t)n, 0);
    for (int i = 0; i < kWVoices; i++) CHECK(!wbus().gate[i], "voice %d still on", i);
    // snap: the ratio lands on a simple ratio
    CHECK(std::fabs(w_snap(1.43f) - 1.5f) < 1e-6 && std::fabs(w_snap(3.6f) - 4.0f) < 1e-6, "snap");
    // preset 4 (top pad 4, note 43): folded lead, the gate follows the function
    ev(0x99, 43, 100);
    CHECK(!P.ping && P.fmode == WF_SUSTAIN, "preset 4 not loaded");
    // pickup on fader 4 (TIMBRE): stored 60, physically at 120
    ev(0xB0, 74, 120);
    CHECK(wc.fader[WFD_TIMBRE] == 60, "TIMBRE jumped to %d", wc.fader[WFD_TIMBRE]);
    ev(0xB0, 74, 121);
    CHECK(wc.fader[WFD_TIMBRE] > 60 && wc.fader[WFD_TIMBRE] < 80, "TIMBRE did not move smoothly: %d", wc.fader[WFD_TIMBRE]);
    // dice moves the oscillator faders, not the function's
    const uint8_t rise = wc.fader[WFD_RISE];
    ev(0xBF, 74, 127);
    CHECK(wc.fader[WFD_RISE] == rise, "dice moved RISE");
    // FOLD pad with pressure raises the timbre
    const float t0 = P.timbre;
    ev(0x99, 36, 64);
    ev(0xA9, 36, 127);
    CHECK(P.timbre > t0, "FOLD pad did nothing");
    ev(0x89, 36, 0);
    CHECK(std::fabs(P.timbre - t0) < 1e-6, "FOLD pad stuck");
    // every preset publishes in-range values
    for (int i = 0; i < kNumWPresets; i++) {
        wc.apply_preset(i);
        wc.publish();
        CHECK(P.ratio >= 0.25f && P.ratio <= 8.0f && P.rise > 0 && P.fall > 0 && P.lpg_decay >= kWMinDecay - 1e-6f,
              "preset %d out of range", i + 1);
    }
}


// Every preset sounds: one key at velocity 0.8 through the controller,
// the voices and the mixer reaches -20 dBFS at least (the first presets
// that pinged a COMBO gate peaked at -26 dB: the strike only opened the
// vactrol to 0.59). And a strike opens a closed gate to the key's velocity.
static void test_presets_audible() {
    for (int p = 0; p < kNumWPresets; p++) {
        static WestCtl c;
        c.init();
        c.apply_preset(p);
        WestVoice v[kWVoices];
        for (int i = 0; i < kWVoices; i++) v[i].init((uint32_t)i + 1);
        WestMix mix;
        mix.init();
        double pk = 0, vmax = 0;
        for (int b = 0; b < 3000; b++) {
            if (b == 10) c.key_on(60, 0.8f);
            if (b == 1500) c.key_off(60);
            c.tick();
            WestBus &B = wbus();
            float o96[2 * kBlock] = {0}, o[kBlock];
            for (int i = 0; i < kWVoices; i++) {
                v[i].note = B.note[i];
                v[i].gate = B.gate[i];
                if (B.trig[i] != v[i].trig_seen) v[i].vel = B.vel[i];
                v[i].trig = B.trig[i];
                v[i].render(B.P, 0, o96);
                vmax = std::fmax(vmax, v[i].v);
            }
            mix.process(o96, o);
            for (float s : o) pk = std::fmax(pk, std::fabs(s));
        }
        std::printf("  preset %d %-16s peak %.3f (%.1f dBFS), vactrol max %.2f\n", p + 1, kWPresets[p].name, pk, 20 * std::log10(pk), vmax);
        CHECK(pk > 0.1, "preset %d (%s) peaks at %.3f", p + 1, kWPresets[p].name, pk);
        if (wbus().P.ping) CHECK(std::fabs(vmax - 0.8) < 0.01, "preset %d: the strike opened the gate to %.3f, not 0.8", p + 1, vmax);
    }
}

int main() {
    wtab().init();
    test_sine();
    test_folder();
    test_fm();
    test_hp();
    test_vactrol();
    test_function();
    test_aliasing();
    test_bounds();
    test_ctl();
    std::printf("presets, one key at velocity 0.8:\n");
    test_presets_audible();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("westcoast: all tests passed\n");
    return 0;
}
