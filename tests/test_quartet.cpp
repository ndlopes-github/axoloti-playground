// Host tests for the quartet voice (lib/quartet_voice.h). make test
//
// Exact results where there are any: q_g against tan; the four stage gains
// against direct division; the linearised low-pass (c -> 0, L -> inf)
// against the bilinear transform of the analog ladder, and its
// self-oscillation threshold k = 4. Measured and printed: self-oscillation
// frequency and level, the Newton solve's error, the distance of the
// lag-one stage gains from the exact implicit trapezoidal solve, aliasing,
// envelope timing, LFO/S&H behaviour, and bounds over random settings.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "../lib/quartet_ctl.h"

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
static const double kFs = 48000.0;

// Amplitude of the component at f in x[n0..n1), by least squares on sin, cos.
static double amp_at(const std::vector<float> &x, size_t n0, size_t n1, double f) {
    double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0;
    for (size_t n = n0; n < n1; n++) {
        const double s = std::sin(2 * kPiD * f * n / kFs), c = std::cos(2 * kPiD * f * n / kFs);
        ss += s * s; cc += c * c; sc += s * c; xs += x[n] * s; xc += x[n] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
    return std::sqrt(a * a + b * b);
}

// Frequency from upward zero crossings (linearly interpolated) in x[n0..n1).
static double freq_zc(const std::vector<float> &x, size_t n0, size_t n1) {
    double first = -1, last = -1;
    int count = 0;
    for (size_t n = n0 + 1; n < n1; n++) {
        if (x[n - 1] < 0 && x[n] >= 0) {
            const double t = (n - 1) + x[n - 1] / (x[n - 1] - x[n]);
            if (first < 0) first = t;
            last = t;
            count++;
        }
    }
    return count > 1 ? (count - 1) * kFs / (last - first) : 0;
}

static double peak(const std::vector<float> &x, size_t n0, size_t n1) {
    double p = 0;
    for (size_t n = n0; n < n1; n++) p = std::fmax(p, std::fabs(x[n]));
    return p;
}

// |H| of the linearised ladder: the bilinear one-pole (prewarped at fc)
// H1 = g (1 + z^-1) / ((1 + g) - (1 - g) z^-1), and H = H1^4 / (1 + k H1^4).
static double ladder_mag(double f, double fc, double k) {
    const double g = std::tan(kPiD * fc / kFs), w = 2 * kPiD * f / kFs;
    const double zr = std::cos(w), zi = -std::sin(w);  // z^-1
    const double nr = g * (1 + zr), ni = g * zi;
    const double dr = (1 + g) - (1 - g) * zr, di = -(1 - g) * zi;
    const double den = dr * dr + di * di;
    const double hr = (nr * dr + ni * di) / den, hi = (ni * dr - nr * di) / den;
    // H1^2, H1^4
    const double h2r = hr * hr - hi * hi, h2i = 2 * hr * hi;
    const double h4r = h2r * h2r - h2i * h2i, h4i = 2 * h2r * h2i;
    const double er = 1 + k * h4r, ei = k * h4i;
    return std::sqrt((h4r * h4r + h4i * h4i) / (er * er + ei * ei));
}

static void test_q_g() {
    double worst = 0;
    for (double fc = 5; fc <= kQMaxCut; fc *= 1.01) {
        const double want = std::tan(kPiD * fc / kFs);
        worst = std::fmax(worst, std::fabs(q_g((float)fc) / want - 1));
    }
    std::printf("q_g: max relative error %.2e (bound 8e-9 + float rounding)\n", worst);
    CHECK(worst < 1e-6, "q_g relative error %.2e", worst);
}

static void test_tanh() {
    double worst = 0, prev = -2;
    bool mono = true, odd = true, sig = true;
    for (int i = -6000; i <= 6000; i++) {
        const float z = i * 0.001f;
        const double t = q_tanh(z);
        worst = std::fmax(worst, std::fabs(t - std::tanh((double)z)));
        if (t < prev - 2.5e-7) mono = false;  // 2 ulp near 1: float rounding
        prev = t;
        if (std::fabs(q_tanh(-z) + t) > 1e-7) odd = false;
        float N, D;
        q_sigma(z, N, D);
        const double s = N / D;
        if (!(s > 0 && s <= 1 + 1e-7)) sig = false;
        if (z != 0 && std::fabs(s - t / z) > 1e-6) sig = false;
    }
    std::printf("tanh stand-in: max |T - tanh| %.4f\n", worst);
    CHECK(mono && odd, "T not odd and nondecreasing");
    CHECK(sig, "sigma = T(z)/z not in (0, 1] or not N/D");
    CHECK(worst < 0.03, "T far from tanh: %.4f", worst);
}

// gains(): one division for four stages, against a_i/(D_i + a_i) directly.
static void test_gains() {
    OtaLadder f;
    f.init();
    srand(7);
    double worst = 0;
    for (int t = 0; t < 10000; t++) {
        f.z1 = (rand() / (float)RAND_MAX - 0.5f) * 10;
        f.z2 = (rand() / (float)RAND_MAX - 0.5f) * 10;
        f.z3 = (rand() / (float)RAND_MAX - 0.5f) * 1;
        f.z4 = (rand() / (float)RAND_MAX - 0.5f) * 0.1f;
        const float g = q_g(5 + 19995 * (rand() / (float)RAND_MAX));
        float G[4], Gm, S;
        f.gains(g, f.z1, f.z2, f.z3, f.z4, G, Gm, S);
        const float z[4] = {f.z1, f.z2, f.z3, f.z4};
        double prod = 1;
        for (int i = 0; i < 4; i++) {
            float N, D;
            q_sigma(z[i], N, D);
            const double want = (double)g * N / (D + (double)g * N);
            worst = std::fmax(worst, std::fabs(G[i] / want - 1));
            prod *= want;
        }
        worst = std::fmax(worst, std::fabs(Gm / prod - 1));
    }
    std::printf("stage gains: max relative error %.2e\n", worst);
    CHECK(worst < 2e-6, "stage gains relative error %.2e", worst);
}

static void linearise(OtaLadder &f) {
    f.init();
    f.c = 1e-6f;   // sigma = 1 - O(c^2)
    f.lim = 1e6f;  // F(y) = k y - O(1/lim^2)
}

static void test_ladder_linear() {
    double worst = 0;
    for (double fc : {200.0, 2000.0, 12000.0})
        for (double k : {0.0, 1.0, 2.0, 3.0, 3.8})
            for (double r : {0.25, 0.5, 1.0, 2.0, 4.0}) {
                const double f = r * fc;
                if (f > 20000) continue;
                OtaLadder lf;
                linearise(lf);
                lf.set_k((float)k);
                const float g = q_g((float)fc);
                const int n = 72000;
                std::vector<float> y(n);
                for (int i = 0; i < n; i++)
                    y[i] = lf.process(0.01f * (float)std::sin(2 * kPiD * f * i / kFs), g);
                const double got = amp_at(y, 48000, n, f) / 0.01, want = ladder_mag(f, fc, k);
                worst = std::fmax(worst, std::fabs(got / want - 1));
                CHECK(std::fabs(got / want - 1) < 2e-3, "linear |H| fc %.0f k %.1f f %.0f: %.5f vs %.5f",
                      fc, k, f, got, want);
            }
    std::printf("linearised low-pass vs bilinear ladder: max relative |H| error %.2e\n", worst);
}

// Linearised: k = 4 is the exact threshold (the bilinear map keeps the
// analog stability boundary). Below it a kick dies out, above it grows.
static void test_threshold() {
    for (double k : {3.95, 4.05}) {
        OtaLadder lf;
        linearise(lf);
        lf.set_k((float)k);
        const float g = q_g(1000);
        std::vector<float> y(96000);
        for (int i = 0; i < 96000; i++) y[i] = lf.process(i == 0 ? 1e-3f : 0.0f, g);
        const double a = peak(y, 4800, 9600), b = peak(y, 86400, 96000);
        CHECK(k < 4 ? b < 0.5 * a : b > 2 * a, "k %.2f: peak %.3g then %.3g", k, a, b);
    }
}

// The full model at RESONANCE 1 (k = kQResMax): it settles to a steady
// oscillation; its frequency falls below fc as the stages saturate.
static void test_self_oscillation() {
    std::printf("self-oscillation at k %.1f (c %.1f, L %.1f):", kQResMax, kQOta, kQLim);
    for (double fc : {100.0, 440.0, 1000.0, 4000.0, 10000.0}) {
        OtaLadder f;
        f.init();
        f.set_k(kQResMax);
        const float g = q_g((float)fc);
        const int n = 144000;
        std::vector<float> y(n);
        for (int i = 0; i < n; i++) y[i] = f.process(i == 0 ? 0.1f : 0.0f, g);
        const double fr = freq_zc(y, 96000, n), p1 = peak(y, 96000, 120000), p2 = peak(y, 120000, n);
        std::printf(" %.0f Hz -> %.3f fc peak %.2f;", fc, fr / fc, p2);
        CHECK(fr / fc > 0.8 && fr / fc < 1.01, "fc %.0f: oscillates at %.3f fc", fc, fr / fc);
        CHECK(p2 > 0.2 && p2 < 0.8, "fc %.0f: peak %.3f", fc, p2);
        CHECK(std::fabs(p1 / p2 - 1) < 0.02, "fc %.0f: not steady (%.3f, %.3f)", fc, p1, p2);
    }
    std::printf("\n");
}

// A voice-like input: saw 0.5 + sub 0.4 (dt for C3 by default).
struct SawSub {
    float p = 0, ss = 1, dt, idt;
    explicit SawSub(double hz = 130.81) : dt((float)(hz / kFs)), idt(1 / dt) {}
    float next() {
        const float v = kQLvlWave * q_saw(p, idt) + kQLvlSub * ss;
        p += dt;
        if (p >= 1) { p -= 1; ss = -ss; }
        return v;
    }
};

// The solve's error: at every sample of a saw through a swept cutoff at
// full resonance, n Newton steps against a converged root (double), in y.
static void test_solver() {
    double worst[3] = {0, 0, 0};
    for (float drive : {1.0f, 2.0f}) {  // the voice's level, and twice it
        OtaLadder f;
        f.init();
        f.set_k(kQResMax);
        SawSub src;
        const int n = 4 * 48000;
        for (int i = 0; i < n; i++) {
            const double t = (double)i / n;
            const float g = q_g((float)(50 * std::pow(360.0, t < 0.5 ? 2 * t : 2 - 2 * t)));  // 50 Hz .. 18 kHz .. 50 Hz
            const float x = drive * src.next();
            float e[4], w0;
            f.predict(x, g, e, w0);
            float G[4], Gm, S;
            f.gains(g, e[0], e[1], e[2], e[3], G, Gm, S);
            const double A = (double)Gm * x + S;
            // converged root of y + Gm F(y) = A
            double y = A;
            for (int it = 0; it < 60; it++) {
                const double z = kQResMax * y / f.lim;
                const double F = z >= 1 ? 2.0 / 3 : (z <= -1 ? -2.0 / 3 : z - z * z * z / 3);
                const double Fd = (z >= 1 || z <= -1) ? 0 : 1 - z * z;
                y -= (y + Gm * f.lim * F - A) / (1 + Gm * kQResMax * Fd);
            }
            for (int s = 0; s < 3; s++) {
                const float w = OtaLadder::solve(w0, f.kl * (Gm * x + S), kQResMax * Gm, s);
                worst[s] = std::fmax(worst[s], std::fabs(f.lk * w - y));
            }
            f.process(x, g);
        }
    }
    std::printf("solve error in y (max over a swept saw, k %.1f, 1x and 2x level): corrector from the predictor: start %.1e, +1 Newton %.1e, +2 %.1e (kQNewton = %d)\n",
                kQResMax, worst[0], worst[1], worst[2], kQNewton);
    CHECK(worst[kQNewton] < 5e-5, "solve error %.2e", worst[kQNewton]);
}

// The exact implicit trapezoidal solve of the same model, in double. Per
// stage y = s + (g/c) T(c (v - y)): the residual is increasing in y
// (slope 1 + g T' >= 1) with the root in [s - g/c, s + g/c] since |T| <= 1;
// Newton safeguarded by bisection. Then y4 = cascade(x - F(y4)): cascade is
// nondecreasing in its input and F nondecreasing, so y4 - cascade(x - F(y4))
// is increasing; regula falsi (Illinois) on [-40, 40].
struct RefLadder {
    double s[4] = {0, 0, 0, 0};
    double c = kQOta, L = kQLim;
    static double T(double z) {
        if (z >= 3) return 1;
        if (z <= -3) return -1;
        return z * (27 + z * z) / (27 + 9 * z * z);
    }
    static double Td(double z) {
        if (z >= 3 || z <= -3) return 0;
        const double a = z * z - 9, b = 27 + 9 * z * z;
        return 9 * a * a / (b * b);
    }
    double F(double y, double k) const {
        const double z = k * y / L;
        return z >= 1 ? L * 2 / 3 : (z <= -1 ? -L * 2 / 3 : L * (z - z * z * z / 3));
    }
    double stage(double v, double st, double g) const {
        double lo = st - g / c, hi = st + g / c;
        double y = (g * v + st) / (1 + g);  // the linear stage's answer
        if (!(y > lo && y < hi)) y = 0.5 * (lo + hi);
        for (int i = 0; i < 60; i++) {
            const double r = y - st - (g / c) * T(c * (v - y));
            if (r > 0) hi = y; else lo = y;
            if (std::fabs(r) < 1e-15 || hi - lo < 1e-15) break;
            double yn = y - r / (1 + g * Td(c * (v - y)));
            if (!(yn > lo && yn < hi)) yn = 0.5 * (lo + hi);
            y = yn;
        }
        return y;
    }
    double cascade(double u, double g, double ys[4]) const {
        double v = u;
        for (int i = 0; i < 4; i++) v = ys[i] = stage(v, s[i], g);
        return v;
    }
    double process(double x, double g, double k) {
        double ys[4];
        double a = -40, b = 40;
        double fa = a - cascade(x - F(a, k), g, ys), fb = b - cascade(x - F(b, k), g, ys);
        int side = 0;
        double m = 0;
        for (int i = 0; i < 200; i++) {
            m = (a * fb - b * fa) / (fb - fa);
            const double fm = m - cascade(x - F(m, k), g, ys);
            if (std::fabs(fm) < 1e-13 || b - a < 1e-13) break;
            if (fm > 0) { b = m; fb = fm; if (side == 1) fa *= 0.5; side = 1; }
            else { a = m; fa = fm; if (side == -1) fb *= 0.5; side = -1; }
        }
        cascade(x - F(m, k), g, ys);
        for (int i = 0; i < 4; i++) s[i] = 2 * ys[i] - s[i];
        return ys[3];
    }
};

// Below self-oscillation the waveforms are compared; at RESONANCE 1 the
// oscillation's frequency and level (two self-oscillating runs drift apart
// in phase, so their waveforms are not comparable).
static void test_lag_vs_implicit() {
    std::printf("predictor-corrector vs the exact implicit solve (saw + sub, C3; rms difference / rms):\n");
    for (double k : {0.0, 3.0})
        for (double fc : {300.0, 2000.0, 8000.0}) {
            OtaLadder f, lin;
            f.init();
            lin.init();
            lin.c = 1e-6f;  // the stages linear, the limiter kept
            f.set_k((float)k);
            lin.set_k((float)k);
            RefLadder ref;
            SawSub a;
            const float g = q_g((float)fc);
            const int n = 9600;
            double d2 = 0, dl2 = 0, r2 = 0;
            for (int i = 0; i < n; i++) {
                const float x = a.next();
                const double yr = ref.process(x, g, k);
                const double yf = f.process(x, g), yl = lin.process(x, g);
                if (i >= n / 2) { d2 += (yf - yr) * (yf - yr); dl2 += (yl - yr) * (yl - yr); r2 += yr * yr; }
            }
            const double lag = std::sqrt(d2 / r2), nl = std::sqrt(dl2 / r2);
            std::printf("  k %.1f fc %4.0f: pred-corr %.4f; linear stages %.4f\n", k, fc, lag, nl);
            CHECK(lag < 0.005, "k %.1f fc %.0f: pred-corr %.4f rms from the exact solve (nonlinearity %.4f)", k, fc, lag, nl);
        }
    std::printf("  self-oscillation at k %.1f, pred-corr / implicit:", kQResMax);
    for (double fc : {300.0, 1000.0, 4000.0, 10000.0}) {
        OtaLadder f;
        f.init();
        f.set_k(kQResMax);
        RefLadder ref;
        const float g = q_g((float)fc);
        const int n = 48000;
        std::vector<float> yf(n), yr(n);
        for (int i = 0; i < n; i++) {
            const float x = i == 0 ? 0.1f : 0.0f;
            yf[i] = f.process(x, g);
            yr[i] = (float)ref.process(x, g, kQResMax);
        }
        const double ff = freq_zc(yf, 24000, n), fr = freq_zc(yr, 24000, n);
        const double pf = peak(yf, 24000, n), pr = peak(yr, 24000, n);
        std::printf(" %.0f Hz: f %.3f/%.3f fc, peak %.3f/%.3f;", fc, ff / fc, fr / fc, pf, pr);
        CHECK(std::fabs(ff / fr - 1) < 0.03, "fc %.0f: pred-corr oscillates at %.4g Hz, implicit at %.4g Hz", fc, ff, fr);
        CHECK(std::fabs(pf / pr - 1) < 0.15, "fc %.0f: pred-corr peak %.3f, implicit %.3f", fc, pf, pr);
    }
    std::printf("\n");
}

// In-place radix-2 FFT (n a power of 2), for the alias measure.
static void fft(std::vector<double> &re, std::vector<double> &im) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double a = -2 * kPiD / len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; k++) {
                const double wr = std::cos(a * k), wi = std::sin(a * k);
                const double xr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                const double xi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k + len / 2] = re[i + k] - xr;
                im[i + k + len / 2] = im[i + k] - xi;
                re[i + k] += xr;
                im[i + k] += xi;
            }
    }
}


// The board's low-pass (Ladder4, linear stages + limiter): the exact linear
// response with the limiter out of reach, the threshold k = 4, the
// self-oscillation at fc, the solve's error, and its distance from the
// full model (what dropping the stage saturation changes).
static void test_ladder4() {
    double worst = 0;
    for (double fc : {200.0, 2000.0, 12000.0})
        for (double k : {0.0, 2.0, 3.8})
            for (double r : {0.25, 1.0, 4.0}) {
                const double f = r * fc;
                if (f > 20000) continue;
                Ladder4 lf;
                lf.init();
                lf.lim = 1e6f;
                lf.set_k((float)k);
                const float G = q_G((float)fc);
                const int n = 72000;
                std::vector<float> y(n);
                for (int i = 0; i < n; i++) y[i] = lf.process(0.01f * (float)std::sin(2 * kPiD * f * i / kFs), G);
                const double got = amp_at(y, 48000, n, f) / 0.01, want = ladder_mag(f, fc, k);
                worst = std::fmax(worst, std::fabs(got / want - 1));
            }
    std::printf("Ladder4: linear |H| vs bilinear ladder, max relative error %.2e\n", worst);
    CHECK(worst < 2e-3, "Ladder4 linear response error %.2e", worst);
    for (double k : {3.95, 4.05}) {
        Ladder4 lf;
        lf.init();
        lf.lim = 1e6f;
        lf.set_k((float)k);
        std::vector<float> y(96000);
        for (int i = 0; i < 96000; i++) y[i] = lf.process(i == 0 ? 1e-3f : 0.0f, q_G(1000));
        const double a = peak(y, 4800, 9600), b = peak(y, 86400, 96000);
        CHECK(k < 4 ? b < 0.5 * a : b > 2 * a, "Ladder4 k %.2f: peak %.3g then %.3g", k, a, b);
    }
    std::printf("Ladder4 self-oscillation at k %.1f:", kQResMax);
    for (double fc : {100.0, 440.0, 1000.0, 4000.0, 10000.0}) {
        Ladder4 lf;
        lf.init();
        lf.set_k(kQResMax);
        const int n = 96000;
        std::vector<float> y(n);
        for (int i = 0; i < n; i++) y[i] = lf.process(i == 0 ? 0.1f : 0.0f, q_G((float)fc));
        const double fr = freq_zc(y, 48000, n), p1 = peak(y, 48000, 72000), p2 = peak(y, 72000, n);
        std::printf(" %.0f Hz -> %.4f fc peak %.3f;", fc, fr / fc, p2);
        CHECK(std::fabs(fr / fc - 1) < 1e-3, "Ladder4 fc %.0f: oscillates at %.4f fc", fc, fr / fc);
        CHECK(std::fabs(p1 / p2 - 1) < 0.02 && p2 > 0.3 && p2 < 0.7, "Ladder4 fc %.0f: peak %.3f / %.3f", fc, p1, p2);
    }
    std::printf("\n");
    // solve error, swept saw at full resonance, 1x and 2x level
    double err[3] = {0, 0, 0};
    for (float drive : {1.0f, 2.0f}) {
        Ladder4 lf;
        lf.init();
        lf.set_k(kQResMax);
        SawSub src;
        const int n = 4 * 48000;
        for (int i = 0; i < n; i++) {
            const double t = (double)i / n;
            const float G = q_G((float)(50 * std::pow(360.0, t < 0.5 ? 2 * t : 2 - 2 * t)));
            const float x = drive * src.next();
            const float H = 1 - G, Gm = G * G * G * G;
            const float S = H * (((lf.s1 * G + lf.s2) * G + lf.s3) * G + lf.s4);
            const double A = (double)Gm * x + S;
            double y = A;
            for (int it = 0; it < 60; it++) {
                const double z = kQResMax * y / lf.lim;
                const double F = z >= 1 ? 2.0 / 3 : (z <= -1 ? -2.0 / 3 : z - z * z * z / 3);
                const double Fd = (z >= 1 || z <= -1) ? 0 : 1 - z * z;
                y -= (y + Gm * lf.lim * F - A) / (1 + Gm * kQResMax * Fd);
            }
            for (int s = 0; s < 3; s++)
                err[s] = std::fmax(err[s], std::fabs(lf.lim / kQResMax * OtaLadder::solve(lf.w, lf.kl * (Gm * x + S), kQResMax * Gm, s) - y));
            lf.process(x, G);
        }
    }
    std::printf("Ladder4 solve error in y: start %.1e, +1 Newton %.1e, +2 %.1e (kQNewton = %d)\n", err[0], err[1], err[2], kQNewton);
    CHECK(err[kQNewton] < 5e-4, "Ladder4 solve error %.2e", err[kQNewton]);
    // what dropping the stage saturation changes (rms difference / rms from the full model)
    std::printf("Ladder4 vs the full model (OtaLadder), saw + sub at C3:");
    for (double k : {0.0, 3.0})
        for (double fc : {300.0, 2000.0, 8000.0}) {
            Ladder4 a;
            a.init();
            a.set_k((float)k);
            OtaLadder b;
            b.init();
            b.set_k((float)k);
            SawSub src;
            double d2 = 0, r2 = 0;
            for (int i = 0; i < 9600; i++) {
                const float x = src.next();
                const double ya = a.process(x, q_G((float)fc)), yb = b.process(x, q_g((float)fc));
                if (i >= 4800) { d2 += (ya - yb) * (ya - yb); r2 += yb * yb; }
            }
            std::printf(" k%.0f/%.0f Hz %.3f;", k, fc, std::sqrt(d2 / r2));
        }
    std::printf("\n");
}

// Alias-to-signal ratio (dB) to 20 kHz of a waveform at f0, rendered at
// 48 kHz: energy outside +-6 bins of the harmonics of f0, Blackman-Harris.
static double alias_db(const std::vector<float> &y, double f0) {
    const int n = 16384;
    std::vector<double> re(n), im(n, 0.0);
    for (int i = 0; i < n; i++) {
        const double t = 2 * kPiD * i / (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        re[i] = w * y[2000 + i];
    }
    fft(re, im);
    double sig = 0, alias = 0;
    for (int b = 1; b * kFs / n < 20000; b++) {
        const double pw = re[b] * re[b] + im[b] * im[b], f = b * kFs / n;
        const double h = f / f0, dist = std::fabs(h - std::round(h)) * f0 / (kFs / n);
        if (dist <= 6 && std::round(h) >= 1) sig += pw;
        else alias += pw;
    }
    return 10 * std::log10(alias / sig);
}

static void test_oscillators() {
    // pulses: zero mean
    for (float w : {0.5f, 0.2f, 0.1f}) {
        const float dt = 311.13f / 48000.0f, idt = 1 / dt;
        double s = 0;
        float p = 0;
        for (int i = 0; i < 48000 * 3; i++) {
            s += q_pulse(p, w, idt);
            p += dt;
            if (p >= 1) p -= 1;
        }
        CHECK(std::fabs(s / (48000 * 3)) < 2e-3, "pulse w %.2f mean %.2e", w, s / (48000 * 3));
    }
    // the 4-point residual: +-1/2 at the step, continuous at |t| = 1, 0 at 2
    CHECK(std::fabs(q_blep4(-1e-7f) - 0.5f) < 1e-6 && std::fabs(q_blep4(1e-7f) + 0.5f) < 1e-6, "blep4 at 0");
    CHECK(std::fabs(q_blep4(0.99999f) - q_blep4(1.00001f)) < 1e-5 && std::fabs(q_blep4(1.99999f)) < 1e-6, "blep4 continuity");
    std::printf("saw aliasing (48 kHz, to 20 kHz):");
    for (double f0 : {261.63, 1046.5, 2093.0, 2793.83}) {  // C4, C6, C7, F7
        const float dt = (float)(f0 / kFs), idt = 1 / dt;
        std::vector<float> saw(20000), sq(20000), filt(20000);
        Ladder4 f;
        f.init();
        float p = 0;
        for (int i = 0; i < 20000; i++) {
            saw[i] = q_saw(p, idt);
            sq[i] = q_pulse(p, 0.5f, idt);
            filt[i] = f.process(kQLvlWave * saw[i], q_G(kQMaxCut));
            p += dt;
            if (p >= 1) p -= 1;
        }
        const double a = alias_db(saw, f0);
        std::printf(" %.0f Hz %.1f dB (square %.1f, through the open low-pass %.1f);", f0, a, alias_db(sq, f0), alias_db(filt, f0));
        CHECK(a < (f0 < 500 ? -55 : -46), "saw at %.0f Hz aliases at %.1f dB", f0, a);
    }
    std::printf("\n");
}

static QuartetParams base_params() {
    QuartetParams P = QuartetParams();
    P.wave = QW_SAW;
    P.pw = 0.5f;
    P.pwm = 0.0f;
    P.sub = 0;
    P.noise = false;
    P.range = 0;
    P.hp_hz = 10;
    P.lp_oct = 10;  // 20 kHz
    P.res = 0;
    P.env_vcf = 0;
    P.env_inv = false;
    P.kybd = 0.1f;
    P.a = 0.001f; P.d = 0.5f; P.s = 1; P.r = 0.2f;
    P.vca_gate = false;
    P.lfo_hz = 5;
    P.lfo_wave = QL_SINE;
    P.trig_hz = 8;
    P.level = 1;
    return P;
}

// Render one voice for n samples; gate_off: the sample the key goes up.
static std::vector<float> render(const QuartetParams &P, int note, int n, int gate_off, QuartetVoice *pv = nullptr) {
    QuartetMod m;
    m.init();
    m.restart();
    QuartetVoice local;
    QuartetVoice &v = pv ? *pv : local;
    if (!pv) v.init(1);
    v.note = (uint8_t)note;
    v.gate = true;
    v.trig++;
    std::vector<float> y(n + kBlock, 0.0f);
    for (int b = 0; b + kBlock <= n; b += kBlock) {
        if (b >= gate_off) v.gate = false;
        m.block(P);
        v.render(P, m, &y[b]);
    }
    y.resize(n);
    return y;
}

static void test_pitch() {
    QuartetParams P = base_params();
    std::vector<float> y = render(P, 69, 48000, 1 << 30);
    const double f = freq_zc(y, 9600, 48000);
    std::printf("A4 saw: %.2f Hz (drift up to %.0f cents)\n", f, kQDriftCents);
    CHECK(std::fabs(f / 440 - 1) < 0.0025, "A4 at %.2f Hz", f);
    P.wave = QW_OFF;
    P.sub = kQLvlSub;
    y = render(P, 69, 48000, 1 << 30);
    const double fs = freq_zc(y, 9600, 48000);
    CHECK(std::fabs(fs / 220 - 1) < 0.0025, "sub of A4 at %.2f Hz", fs);
    double mean = 0;
    for (int i = 9600; i < 48000; i++) mean += y[i];
    CHECK(std::fabs(mean / 38400) < 2e-3, "sub mean %.2e", mean / 38400);
    P.wave = QW_SAW;
    P.sub = 0;
    P.range = -2;  // 32'
    y = render(P, 69, 48000, 1 << 30);
    CHECK(std::fabs(freq_zc(y, 9600, 48000) / 110 - 1) < 0.0025, "32' A4 not at 110 Hz");
}

// ADSR timing per the conventions: attack reaches 1 in a; decay and
// release cover 99 % in d, r; VCA GATE edges 99 % in 2 ms.
static void test_envelope() {
    QuartetParams P = base_params();
    P.a = 0.25f; P.d = 0.5f; P.s = 0.3f; P.r = 1.0f;
    QuartetMod m;
    m.init();
    m.block(P);
    QuartetVoice v;
    v.init(1);
    v.gate = true;
    v.stage = QuartetVoice::ATTACK;
    v.env_gate = true;
    int n = 0;
    while (v.stage == QuartetVoice::ATTACK && n < 200000) { v.env_tick(m); n++; }
    CHECK(std::fabs(n / (P.a * kFs) - 1) < 0.01, "attack took %d samples, want %.0f", n, P.a * kFs);
    int nd = 0;
    while (v.env > P.s + 0.01 * (1 - P.s) && nd < 200000) { v.env_tick(m); nd++; }
    CHECK(std::fabs(nd / (P.d * kFs) - 1) < 0.01, "decay 99 %% in %d samples, want %.0f", nd, P.d * kFs);
    v.gate = false;
    int nr = 0;
    while (v.env > 0.01 * P.s && nr < 200000) { v.env_tick(m); nr++; }
    CHECK(std::fabs(nr / (P.r * kFs) - 1) < 0.01, "release 99 %% in %d samples, want %.0f", nr, P.r * kFs);
    // the shortest attack, 0.6 ms
    P.a = 0;
    m.block(P);
    v.init(1);
    v.gate = true;
    v.stage = QuartetVoice::ATTACK;
    v.env_gate = true;
    n = 0;
    while (v.stage == QuartetVoice::ATTACK && n < 1000) { v.env_tick(m); n++; }
    CHECK(std::abs(n - 29) <= 1, "shortest attack %d samples, want 29 (0.6 ms)", n);
    // VCA GATE: the output follows the key within 2 ms, with the ADSR slow
    P = base_params();
    P.vca_gate = true;
    P.a = 2.0f;
    std::vector<float> y = render(P, 57, 9600, 4800);
    CHECK(peak(y, 96, 4800) > 0.3, "VCA GATE: no sound in the first 2 ms of a slow attack");
    CHECK(peak(y, 4800 + 192, 9600) < 1e-3, "VCA GATE: sound after the key went up");
}

static void test_mod() {
    QuartetParams P = base_params();
    P.trig_hz = 8;
    P.lfo_hz = 1;
    P.lfo_delay = 2;
    QuartetMod m;
    m.init();
    m.restart();
    int changes = 0;
    float last = m.sh_val;
    const uint32_t c0 = m.trig_count;
    double amp_early = 0, amp_late = 0;
    for (int b = 0; b < 3000 * 4 + 150; b++) {  // 4.05 s
        m.block(P);
        for (int i = 0; i < kQSubs; i++) {
            if (m.sh[i] != last) { changes++; last = m.sh[i]; }
            const double t = (b * kQSubs + i) * kQSub / kFs;
            if (t < 0.5) amp_early = std::fmax(amp_early, std::fabs(m.lfo[i]));
            if (t > 2.5) amp_late = std::fmax(amp_late, std::fabs(m.lfo[i]));
        }
    }
    CHECK(m.trig_count - c0 == 32, "8 Hz trigger gave %u pulses in 4.05 s", m.trig_count - c0);
    CHECK(changes >= 31, "S&H changed %d times", changes);
    CHECK(amp_early <= 0.25 + 1e-3, "LFO delay: |lfo| %.3f in the first 0.5 s of a 2 s fade", amp_early);
    CHECK(amp_late > 0.99, "LFO after the delay: %.3f", amp_late);
    // waves at known phases
    CHECK(std::fabs(QuartetMod::wave(0.25f, QL_SINE) - 1) < 1e-5, "sine");
    CHECK(QuartetMod::wave(0.25f, QL_SQUARE) == 1 && QuartetMod::wave(0.75f, QL_SQUARE) == -1, "square");
    CHECK(std::fabs(QuartetMod::wave(0.75f, QL_SAW) - 0.5f) < 1e-6 && std::fabs(QuartetMod::wave(0.75f, QL_INVSAW) + 0.5f) < 1e-6, "saws");
}

// Random settings, notes and modulation: finite, bounded output.
static void test_bounds() {
    srand(11);
    double worst = 0;
    auto u = [] { return rand() / (float)RAND_MAX; };
    for (int t = 0; t < 300; t++) {
        QuartetParams P = base_params();
        P.wave = rand() % NUM_QW;
        P.pw = 0.1f + 0.4f * u();
        P.pwm = u();
        P.sub = kQSubLevels[rand() % 4];
        P.noise = rand() % 2;
        P.range = rand() % 4 - 2;
        P.hp_hz = 10 + 5000 * u();
        P.lp_oct = 11 * u();
        P.res = u();
        P.env_vcf = 8 * u();
        P.env_inv = rand() % 2;
        P.kybd = u();
        P.vco_lfo = 12 * u();
        P.vcf_lfo = 4 * u();
        P.vca_lfo = u();
        P.sh_vcf = 4 * u();
        P.a = 0.001f + u(); P.d = 0.01f + u(); P.s = u(); P.r = 0.01f + u();
        P.vca_gate = rand() % 2;
        P.bend_st = 24 * u() - 12;
        P.bend_oct = 4 * u() - 2;
        P.bend_db = 24 * u() - 12;
        P.lfo_hz = 0.1f + 80 * u();
        P.lfo_wave = rand() % NUM_QL;
        P.trig_hz = 1 + 24 * u();
        std::vector<float> y = render(P, 24 + rand() % 72, 9600, 4800);
        for (float s : y) {
            CHECK(std::isfinite(s), "non-finite output, trial %d", t);
            if (!std::isfinite(s)) return;
            worst = std::fmax(worst, std::fabs(s));
        }
    }
    std::printf("random settings: max |out| %.2f (gain up to +12 dB)\n", worst);
    CHECK(worst < 8, "output %.2f", worst);
}



// The Ensemble: off passes the dry signal through exactly; on, an impulse
// comes back on each side within the swept delay (86 .. 298 samples);
// output bounded by 1.4 sup|in|; the two sides decorrelate.
static void test_ensemble() {
    static QEnsemble e;
    e.init();
    float in[kBlock], l[kBlock], r[kBlock];
    // off: identity
    bool same = true;
    for (int b = 0; b < 100; b++) {
        for (int i = 0; i < kBlock; i++) in[i] = (float)std::sin(0.01 * (b * kBlock + i));
        e.process(false, in, l, r);
        for (int i = 0; i < kBlock; i++) same = same && l[i] == in[i] && r[i] == in[i];
    }
    CHECK(same, "Ensemble off is not the dry signal");
    // on, after the fade: an impulse returns within the swept delay on both sides
    e.init();
    for (int b = 0; b < 80; b++) {
        for (int i = 0; i < kBlock; i++) in[i] = 0;
        e.process(true, in, l, r);
    }
    int first_l = -1, first_r = -1;
    double peak_l = 0, peak_r = 0;
    for (int b = 0; b < 40; b++) {
        for (int i = 0; i < kBlock; i++) in[i] = (b == 0 && i == 0) ? 1.0f : 0.0f;
        e.process(true, in, l, r);
        for (int i = 0; i < kBlock; i++) {
            const int n = b * kBlock + i;
            if (n == 0) continue;
            if (std::fabs(l[i]) > peak_l) { peak_l = std::fabs(l[i]); first_l = n; }
            if (std::fabs(r[i]) > peak_r) { peak_r = std::fabs(r[i]); first_r = n; }
        }
    }
    CHECK(first_l >= 86 && first_l <= 300 && first_r >= 86 && first_r <= 300, "wet taps at %d and %d samples", first_l, first_r);
    // bounds and decorrelation on a sustained saw + sub
    e.init();
    SawSub src;
    double worst = 0, sll = 0, srr = 0, slr = 0;
    for (int b = 0; b < 3000 * 4; b++) {
        for (int i = 0; i < kBlock; i++) in[i] = src.next();
        e.process(true, in, l, r);
        for (int i = 0; i < kBlock; i++) {
            worst = std::fmax(worst, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
            if (b > 3000) { sll += l[i] * l[i]; srr += r[i] * r[i]; slr += l[i] * r[i]; }
        }
    }
    const double corr = slr / std::sqrt(sll * srr);
    std::printf("Ensemble: wet taps at %d / %d samples, saw + sub: peak %.2f (input peak 0.9), L/R correlation %.2f\n",
                first_l, first_r, worst, corr);
    CHECK(worst <= 1.4 * 0.9 + 1e-3, "Ensemble output %.3f above its bound", worst);
}

// The controller's key assignment (lib/quartet_ctl.h).
static QuartetCtl qc;
static void kon(int n) { qc.midi(0x90, (uint8_t)n, 100); qc.tick(); }
static void koff(int n) { qc.midi(0x80, (uint8_t)n, 0); qc.tick(); }
static void cc16(int n) { qc.midi(0xBF, (uint8_t)n, 127); qc.midi(0xBF, (uint8_t)n, 0); qc.tick(); }
static void pad(int n) { qc.midi(0x99, (uint8_t)n, 100); qc.midi(0x89, (uint8_t)n, 0); qc.tick(); }
static int sounding(int n) {  // voices with gate on note n
    int c = 0;
    for (int i = 0; i < kQVoices; i++) c += qbus().gate[i] && qbus().note[i] == n;
    return c;
}
static void test_keys() {
    qc.init();
    // POLY: four keys on four voices; a fifth steals the oldest (48)
    const uint32_t r0 = qbus().restart;
    kon(48); kon(52); kon(55); kon(59);
    CHECK(qbus().restart == r0 + 1, "one fresh key, restart %u", qbus().restart - r0);
    CHECK(sounding(48) == 1 && sounding(52) == 1 && sounding(55) == 1 && sounding(59) == 1, "chord not on four voices");
    kon(62);
    CHECK(sounding(62) == 1 && sounding(48) == 0, "fifth key did not steal the oldest");
    koff(52);
    CHECK(sounding(52) == 0, "released key still sounds");
    kon(64);  // takes the voice just freed (52's)
    CHECK(sounding(64) == 1 && sounding(55) == 1 && sounding(59) == 1 && sounding(62) == 1, "free voice not reused");
    for (int n : {48, 55, 59, 62, 64}) koff(n);
    for (int i = 0; i < kQVoices; i++) CHECK(!qbus().gate[i], "voice %d still on", i);
    // UNISON (bottom pad 7): all four on the lowest key, spread by DETUNE
    pad(46);
    kon(60);
    CHECK(sounding(60) == 4, "unison: %d voices on 60", sounding(60));
    CHECK(qbus().detune[0] < 0 && qbus().detune[3] > 0, "unison: no spread");
    const uint32_t t0 = qbus().trig[0];
    kon(67);  // higher: nothing changes
    CHECK(sounding(60) == 4 && qbus().trig[0] == t0, "unison: a higher key took over");
    kon(55);  // lower: retriggers on 55
    CHECK(sounding(55) == 4 && qbus().trig[0] == t0 + 1, "unison: a lower key did not take over");
    koff(55);  // back to 60, no retrigger
    CHECK(sounding(60) == 4 && qbus().trig[0] == t0 + 1, "unison: release did not return to 60 legato");
    koff(60); koff(67);
    CHECK(sounding(60) + sounding(67) == 0, "unison: still sounding");
    // HOLD (Play) in POLY: keys stay after release until HOLD goes off
    pad(45);
    cc16(115);
    kon(48); kon(52); koff(48); koff(52);
    CHECK(sounding(48) == 1 && sounding(52) == 1, "hold: released keys stopped");
    kon(55);
    cc16(115);  // hold off: 48, 52 stop; 55 is still down
    CHECK(sounding(48) == 0 && sounding(52) == 0 && sounding(55) == 1, "hold off: wrong voices");
    koff(55);
    // range and wave buttons
    qc.midi(0xB0, 11, 127); qc.tick();
    CHECK(qbus().P.wave == QW_SQUARE, "wave button");
    cc16(103);
    CHECK(qbus().P.range == 1, "track > to 4'");
    cc16(103);
    CHECK(qbus().P.range == 1, "range above 4'");
}


static void cc1(int n, int v) { qc.midi(0xB0, (uint8_t)n, (uint8_t)v); qc.tick(); }

// Presets, pickup, pages, the S&H switch.
static void test_controls() {
    qc.init();
    const QuartetParams &P = qbus().P;
    CHECK(P.wave == QW_SAW && P.sub == 0 && std::fabs(P.res - 30 / 127.0f) < 1e-6, "power-on is not preset 1");
    CHECK(P.sh_vcf == 0, "S&H on at power-on");
    // top pad 2 (note 41): sweep pad
    pad(41);
    CHECK(P.sub == kQLvlSub && !P.noise && P.wave == QW_SAW && std::fabs(P.res - 75 / 127.0f) < 1e-6 && P.a > 1.0f && qbus().ensemble, "preset 2 not loaded");
    // pickup: the LPF fader stored at 46, physically at 20: no jump
    cc1(76, 20);
    CHECK(qc.fader[QF_LPF] == 46, "LPF jumped to %d on the first move", qc.fader[QF_LPF]);
    cc1(76, 19);
    CHECK(qc.fader[QF_LPF] < 46 && qc.fader[QF_LPF] > 30, "LPF did not move smoothly down: %d", qc.fader[QF_LPF]);
    cc1(76, 0);
    CHECK(qc.fader[QF_LPF] == 0 && qc.fpicked[QF_LPF], "LPF did not catch up at the end of travel");
    cc1(76, 33);
    CHECK(qc.fader[QF_LPF] == 33, "LPF does not follow after catching up");
    // pages: MOD knob 2 (S&H depth) leaves SOUND knob 2 (DETUNE) alone
    cc16(104);
    CHECK(qc.page == QPAGE_MOD, "upper round button is not the MOD page");
    cc16(107);
    CHECK(qc.page == QPAGE_MOD, "lower left button is not the MOD page");
    const uint8_t det = qc.knob[QPAGE_SOUND][QK_DETUNE];
    cc1(22, 64);  // knob 2 on MOD: first move anchors (not picked: no jump)
    cc1(22, 127);
    CHECK(qc.knob[QPAGE_SOUND][QK_DETUNE] == det, "a MOD knob moved a SOUND value");
    CHECK(qc.knob[QPAGE_MOD][QM_SH] > 64, "MOD knob 2 did not move the S&H depth");
    // the S&H switch (bottom pad 8): on at the knob's depth, off again
    pad(47);
    CHECK(std::fabs(P.sh_vcf - q_shdepth(qc.knob[QPAGE_MOD][QM_SH])) < 1e-6 && P.sh_vcf > 0, "S&H pad did not switch on");
    pad(47);
    CHECK(P.sh_vcf == 0, "S&H pad did not switch off");
    // presets set the mode; Undo is preset 1; fader button 9 steps on
    pad(43);  // top pad 4: unison lead
    CHECK(qc.mode == QMODE_UNISON, "unison lead not in UNISON");
    cc16(77);
    CHECK(qc.preset == 0 && qc.mode == QMODE_POLY, "Undo did not reset to preset 1");
    qc.midi(0xB0, 19, 127); qc.tick();
    CHECK(qc.preset == 1, "fader button 9 did not step to preset 2");
    pad(48);  // top pad 5: s&h burble, with the S&H on
    CHECK(P.sh_vcf > 0, "s&h burble without S&H");
    // fader button 2: the sub cycles OFF, LOW, MID, HIGH, OFF (preset 5 has it at MID)
    for (int i = 0; i < 4; i++) {
        qc.midi(0xB0, 12, 127); qc.midi(0xB0, 12, 0); qc.tick();
        CHECK(P.sub == kQSubLevels[(3 + i) % 4], "sub press %d: level %.2f", i + 1, P.sub);
    }
    // knob 1 (PW): nothing on SAW (as the Jupiter-4's slider); four widths
    // on PULSE; the modulation depth on PWM
    qc.apply_preset(0);
    cc16(106);
    for (int v : {0, 40, 70, 110}) cc1(21, v);  // first move anchors (pickup), then follows
    CHECK(P.wave == QW_SAW, "preset 1 is not a saw");
    qc.midi(0xB0, 11, 127); qc.tick(); qc.midi(0xB0, 11, 127); qc.tick();  // WAVE: square, pulse
    CHECK(P.wave == QW_PULSE, "two WAVE presses: %d", P.wave);
    const float w1 = P.pw;
    cc1(21, 10);
    CHECK(P.pw != w1 || qc.knob[QPAGE_SOUND][QK_PW] == 0, "knob 1 did not change the pulse width (%.2f, knob %d)", P.pw, qc.knob[QPAGE_SOUND][QK_PW]);
    // every preset publishes finite, in-range values
    for (int i = 0; i < kNumQPresets; i++) {
        qc.apply_preset(i);
        qc.publish();
        CHECK(P.a > 0 && P.d > 0 && P.r > 0 && P.s >= 0 && P.s <= 1 && P.lp_oct >= 0 && P.res >= 0 && P.res <= 1,
              "preset %d out of range", i + 1);
    }
}

int main() {
    test_q_g();
    test_tanh();
    test_gains();
    test_ladder_linear();
    test_threshold();
    test_self_oscillation();
    test_solver();
    test_lag_vs_implicit();
    test_ladder4();
    test_oscillators();
    test_pitch();
    test_envelope();
    test_mod();
    test_bounds();
    test_ensemble();
    test_keys();
    test_controls();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("quartet: all tests passed\n");
    return 0;
}
