// Host tests for lib/grains.h: make test
// Window bounds, memory safety at extreme settings (exact-size heap sources
// under AddressSanitizer), spawn rates, level normalisation, scan motion,
// hit bursts, inactive and missing sources.
#include <cmath>
#include <cstdio>
#include <vector>

#include "../lib/grains.h"

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

static GrainParams defaults() {
    GrainParams p;
    p.mode = GM_CLOUD; p.position = 0.5f; p.size = 0.05f; p.density = 40.0f;
    p.spray = 0.0f; p.scatter = 0.0f; p.octave_prob = 0.0f; p.reverse_prob = 0.0f;
    p.stereo = 0.0f; p.shape = 0.0f; p.scan_speed = 1.0f; p.pitch = 0.0f;
    p.freeze = false; p.active = true;
    return p;
}

// A heap source of exactly n samples (ASan traps any read outside it).
struct HeapSource {
    int16_t *d;
    GrainSource s;
    HeapSource(int32_t n, float ratio, int kind) {
        d = new int16_t[n];
        uint32_t r = 12345;
        for (int32_t i = 0; i < n; i++) {
            if (kind == 0) d[i] = (int16_t)(32767.0 * std::sin(2 * M_PI * 220.0 * i / 48000.0));
            else { r = r * 1664525u + 1013904223u; d[i] = (int16_t)(r >> 16); }
        }
        s.data = d; s.len = n; s.ratio = ratio;
    }
    ~HeapSource() { delete[] d; }
};

static void test_window() {
    const float shapes[] = {0.0f, 0.3f, 0.5f, 1.0f};
    for (float sh : shapes) {
        float peak = 0;
        bool bounded = true;
        for (int i = 0; i < 10000; i++) {
            const float w = grain_window(i / 10000.0f, sh);
            bounded = bounded && w >= 0.0f && w <= 1.0f;
            peak = std::fmax(peak, w);
        }
        CHECK(bounded, "window out of [0,1] for shape %g", sh);
        CHECK(grain_window(0.0f, sh) == 0.0f && grain_window(0.9999f, sh) < 1e-3f, "window ends not 0 (shape %g)", sh);
        CHECK(peak > 0.999f, "window peak %g (shape %g)", peak, sh);
    }
}

static void test_memory_safety() {
    const int32_t lens[] = {kMinSource, 64 + 37, 48000};
    const float ratios[] = {0.125f, 1.0f, 2.0f, 5.0f};
    for (int32_t n : lens)
        for (float ratio : ratios)
            for (int mode = 0; mode < NUM_GMODES; mode++) {
                HeapSource src(n, ratio, 1);
                GrainEngine e;
                e.init(7);
                GrainParams p = defaults();
                p.mode = mode; p.size = (mode == 1) ? 2.0f : 0.001f; p.density = 1000.0f;
                p.spray = 10.0f; p.scatter = 24.0f; p.octave_prob = 1.0f; p.reverse_prob = 0.5f;
                p.stereo = 1.0f; p.shape = 1.0f; p.scan_speed = -7.3f; p.position = 1.0f;
                bool finite = true;
                for (int b = 0; b < 3000; b++) {
                    p.pitch = (b % 3 == 0) ? 36.0f : -48.0f;
                    if (mode == GM_HIT && b % 50 == 0) e.hit(32);
                    float l[kBlock] = {0}, r[kBlock] = {0};
                    e.render(src.s, p, l, r);
                    for (int j = 0; j < kBlock; j++) finite = finite && std::isfinite(l[j]) && std::isfinite(r[j]);
                    CHECK(e.active_count() <= kMaxGrains, "pool overflow");
                }
                CHECK(finite, "non-finite output (n %d ratio %g mode %d)", n, ratio, mode);
            }
}

static void test_spawn_rates() {
    HeapSource src(48000, 1.0f, 0);
    for (int mode : {GM_SCAN, GM_CLOUD}) {
        GrainEngine e;
        e.init(3);
        GrainParams p = defaults();
        p.mode = mode; p.density = 50.0f; p.size = 0.01f;  // overlap 0.5: pool never full
        const int blocks = (int)(10.0f * kControlRate);
        for (int b = 0; b < blocks; b++) {
            float l[kBlock] = {0}, r[kBlock] = {0};
            e.render(src.s, p, l, r);
        }
        const double tol = mode == GM_SCAN ? 0.01 : 0.05;  // CLOUD: random intervals, mean exact
        CHECK(std::fabs(e.spawned - 500.0) <= 500.0 * tol, "mode %d: %u grains in 10 s at 50/s", mode, e.spawned);
    }
    // inactive: no new grains
    GrainEngine e;
    e.init(3);
    GrainParams p = defaults();
    p.active = false;
    for (int b = 0; b < 3000; b++) { float l[kBlock] = {0}, r[kBlock] = {0}; e.render(src.s, p, l, r); }
    CHECK(e.spawned == 0, "inactive voice spawned %u grains", e.spawned);
}

// Level: 1/sqrt(overlap) keeps the RMS of a dense cloud near that of a thin one.
static void test_level() {
    HeapSource src(96000, 1.0f, 1);  // noise: grains are uncorrelated
    double rms[3];
    const float overlaps[3] = {1.0f, 4.0f, 16.0f};
    for (int k = 0; k < 3; k++) {
        GrainEngine e;
        e.init(11);
        GrainParams p = defaults();
        p.size = 0.04f; p.density = overlaps[k] / p.size; p.spray = 0.5f;
        double sum = 0;
        int n = 0;
        for (int b = 0; b < 6000; b++) {
            float l[kBlock] = {0}, r[kBlock] = {0};
            e.render(src.s, p, l, r);
            if (b > 1000) for (int j = 0; j < kBlock; j++) { sum += l[j] * l[j]; n++; }
        }
        rms[k] = std::sqrt(sum / n);
    }
    CHECK(rms[1] / rms[0] > 0.6 && rms[1] / rms[0] < 1.6 && rms[2] / rms[0] > 0.6 && rms[2] / rms[0] < 1.6,
          "RMS at overlap 1/4/16: %g %g %g", rms[0], rms[1], rms[2]);
}

static void test_scan_and_hit() {
    HeapSource src(480000, 1.0f, 0);  // 10 s
    GrainEngine e;
    e.init(5);
    GrainParams p = defaults();
    p.mode = GM_SCAN; p.position = 0.0f; p.scan_speed = 1.0f;
    e.restart(src.s, p);
    for (int b = 0; b < (int)kControlRate; b++) { float l[kBlock] = {0}, r[kBlock] = {0}; e.render(src.s, p, l, r); }
    CHECK(std::fabs(e.scan - 48000.0f) < 2.0f * kBlock, "scan after 1 s at x1: %g", e.scan);
    p.freeze = true;
    const float frozen = e.scan;
    for (int b = 0; b < 300; b++) { float l[kBlock] = {0}, r[kBlock] = {0}; e.render(src.s, p, l, r); }
    CHECK(e.scan == frozen, "freeze moved the scan");
    p.freeze = false; p.scan_speed = -2.0f;
    for (int b = 0; b < (int)(0.25f * kControlRate); b++) { float l[kBlock] = {0}, r[kBlock] = {0}; e.render(src.s, p, l, r); }
    CHECK(std::fabs(e.scan - (frozen - 24000.0f)) < 4.0f * kBlock, "scan backwards: %g", e.scan);

    GrainEngine h;
    h.init(9);
    GrainParams q = defaults();
    q.mode = GM_HIT; q.active = false;
    h.hit(10);
    for (int b = 0; b < 100; b++) { float l[kBlock] = {0}, r[kBlock] = {0}; h.render(src.s, q, l, r); }
    CHECK(h.spawned == 10, "hit(10) started %u grains", h.spawned);

    // no source: silence, no grains
    GrainSource none = {nullptr, 0, 1.0f};
    GrainEngine z;
    z.init(1);
    float l[kBlock] = {0}, r[kBlock] = {0};
    z.render(none, p, l, r);
    bool silent = true;
    for (int j = 0; j < kBlock; j++) silent = silent && l[j] == 0.0f && r[j] == 0.0f;
    CHECK(silent && z.spawned == 0, "missing source must be silent");
}

static void test_q32() {
    CHECK(to_q32(0.0f) == 0, "to_q32(0)");
    CHECK(to_q32(5.25f) == (int64_t)21 << 30, "to_q32(5.25)");
    CHECK(to_q32(-3.75f) == -((int64_t)15 << 30), "to_q32(-3.75)");
    CHECK(to_q32(47999.5f) == ((int64_t)95999 << 31), "to_q32(47999.5)");
    const int64_t t = to_q32(-1e-9f);  // fraction rounds to 1: kept below
    CHECK(t < 0 && t > -((int64_t)1 << 32) / 1000, "to_q32(-1e-9) = %lld", (long long)t);
}

// One grain over a ramp and the same grain over a constant source: same seed
// and settings, so same position, step, window and gains. Their ratio gives
// back the interpolated sample the engine read, compared with the exact
// 32.32 position. The grain crosses the source's end (the careful loop) and
// spends most runs inside it (the fast loop).
static void test_positions() {
    const int32_t n = 1000;
    std::vector<int16_t> ramp(n), dc(n, 10000);
    for (int32_t i = 0; i < n; i++) ramp[i] = (int16_t)(30 * i);
    GrainSource sr = {ramp.data(), n, 1.0f}, sd = {dc.data(), n, 1.0f};
    for (int rev = 0; rev < 2; rev++) {
        GrainEngine er, ed;
        er.init(42);
        ed.init(42);
        GrainParams p = defaults();
        p.mode = GM_CLOUD; p.active = false; p.size = 0.01f; p.position = rev ? 0.05f : 0.95f;
        p.pitch = 7.0f; p.reverse_prob = rev ? 1.0f : 0.0f; p.shape = 0.3f;
        float l[kBlock] = {0}, r[kBlock] = {0};
        er.render(sr, p, l, r);  // binds the source (no grains yet)
        ed.render(sd, p, l, r);
        const int off = 3;
        er.spawn(sr, p, 1.0f, off);
        ed.spawn(sd, p, 1.0f, off);
        const int64_t p0 = er.g[0].pos, inc = er.g[0].inc, n64 = (int64_t)n << 32;
        CHECK(p0 == ed.g[0].pos && inc == ed.g[0].inc, "grains differ");
        double worst = 0;
        int checked = 0;
        bool wrapped = false;
        for (int b = 0; b < 31; b++) {
            float lr[kBlock] = {0}, rr[kBlock] = {0}, ld[kBlock] = {0}, rd[kBlock] = {0};
            er.render(sr, p, lr, rr);
            ed.render(sd, p, ld, rd);
            for (int j = 0; j < kBlock; j++) {
                const int m = b * kBlock + j - off;  // sample index within the grain
                if (m < 0 || std::fabs(ld[j]) < 1e-5f) continue;
                int64_t pm = (p0 + (int64_t)m * inc) % n64;
                if (pm < 0) pm += n64;
                wrapped = wrapped || (rev ? p0 + (int64_t)m * inc < 0 : p0 + (int64_t)m * inc >= n64);
                const int32_t i0 = (int32_t)(pm >> 32), i1 = (i0 + 1) % n;
                const double f = (double)(uint32_t)pm / 4294967296.0;
                const double want = ramp[i0] + f * (ramp[i1] - ramp[i0]);
                const double got = (double)lr[j] / ld[j] * 10000.0;
                // The engine truncates the fraction to 14 bits: an error up
                // to |s1 - s0| 2^-14, plus float rounding.
                const double allowed = 0.05 + std::fabs((double)ramp[i1] - ramp[i0]) / 16384.0;
                worst = std::fmax(worst, std::fabs(got - want) / allowed);
                checked++;
            }
        }
        CHECK(wrapped && checked > 400, "grain did not cross the end (%d samples)", checked);
        CHECK(worst <= 1.0, "position error %g x allowed (reverse %d)", worst, rev);
        std::printf("info: max position error %.3g x allowed, reverse %d\n", worst, rev);
        CHECK(er.active_count() == 0, "grain outlived its length");
    }
}

// The block-linear window against the exact one, on a constant source.
static void test_window_runs() {
    const int32_t n = 48000;
    std::vector<int16_t> dc(n, 10000);
    GrainSource sd = {dc.data(), n, 1.0f};
    const float shapes[2] = {0.0f, 1.0f};
    const double tol[2] = {1e-3, 0.03};  // 192/(aL)^2 with L = 2400: 1.3e-4, 0.021
    for (int k = 0; k < 2; k++) {
        GrainEngine e;
        e.init(5);
        GrainParams p = defaults();
        p.active = false; p.size = 0.05f; p.shape = shapes[k];
        float l[kBlock] = {0}, r[kBlock] = {0};
        e.render(sd, p, l, r);
        const int off = 5;
        e.spawn(sd, p, 1.0f, off);
        const double g = e.g[0].gl * 10000.0;
        const int32_t len = e.g[0].len;
        double worst = 0;
        for (int b = 0; b < len / kBlock + 2; b++) {
            float lo[kBlock] = {0}, ro[kBlock] = {0};
            e.render(sd, p, lo, ro);
            for (int j = 0; j < kBlock; j++) {
                const int m = b * kBlock + j - off;
                if (m < 0 || m >= len) continue;
                worst = std::fmax(worst, std::fabs(lo[j] / g - grain_window((float)m / len, shapes[k])));
            }
        }
        CHECK(worst < tol[k], "block-linear window error %g (shape %g)", worst, shapes[k]);
        std::printf("info: block-linear window error %.3g at shape %g\n", worst, shapes[k]);
    }
}

// Switching to a shorter source ends the live grains (they would read past it).
static void test_source_switch() {
    HeapSource a(48000, 1.0f, 0), b(100, 1.0f, 1);
    GrainEngine e;
    e.init(8);
    GrainParams p = defaults();
    p.size = 0.5f; p.density = 60.0f; p.position = 0.9f;
    for (int i = 0; i < 3000; i++) { float l[kBlock] = {0}, r[kBlock] = {0}; e.render(a.s, p, l, r); }
    CHECK(e.active_count() > 10, "no grains on source a");
    p.active = false;
    float l[kBlock] = {0}, r[kBlock] = {0};
    e.render(b.s, p, l, r);
    CHECK(e.active_count() == 0, "grains survived a source switch");
    p.active = true;
    for (int i = 0; i < 300; i++) { float l2[kBlock] = {0}, r2[kBlock] = {0}; e.render(b.s, p, l2, r2); }
    CHECK(e.scan >= 0.0f && e.scan < 100.0f, "scan outside the new source: %g", e.scan);
}

int main() {
    test_window();
    test_q32();
    test_positions();
    test_window_runs();
    test_source_switch();
    test_memory_safety();
    test_spawn_rates();
    test_level();
    test_scan_and_hit();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("grains: all tests passed\n");
    return 0;
}
