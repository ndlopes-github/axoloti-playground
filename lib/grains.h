// grains.h: granular engine of the "grains" patch, freak's sibling on the SD
// card. Mono voice; a fixed pool of grains read from a 16-bit source in
// memory (SDRAM on the board), mixed to stereo.
//
// Modes, after the MicroFreak's grain engines:
//   SCAN  : grains at a regular rate from a position that moves through the
//           source at SCAN SPEED times real time (0 = frozen, < 0 backwards):
//           a time-stretch.
//   CLOUD : grains at random times (mean rate DENSITY) around POSITION:
//           a texture.
//   HIT   : a burst of grains on each note, then nothing: percussive.
//
// The cost is bounded by the pool: at most kMaxGrains grains sound at once.
// When the pool is full, new grains are skipped rather than stealing, so the
// worst case per sample is kMaxGrains grain updates, whatever the settings.
//
// Header-only, C++11, float, no libm, every struct a POD (see freak_dsp.h).
#ifndef GRAINS_H
#define GRAINS_H

#include <stdint.h>

#include "freak_dsp.h"

namespace freak {

struct GrainSource {
    const int16_t *data;  // mono samples
    int32_t len;          // samples; < kMinSource = nothing to play (not loaded)
    float ratio;          // source sample rate / 48 kHz
};

enum { GM_SCAN, GM_CLOUD, GM_HIT, NUM_GMODES };
// Pool size: the cost on the board is linear in the live grains (README:
// measurements), so this bounds the engine's DSP load.
#ifndef GRAINS_MAX
#define GRAINS_MAX 20
#endif
const int kMaxGrains = GRAINS_MAX;
// A source needs at least this many samples. A grain moves less than
// 17 samples per output sample (ratio <= 2, pitch <= +36 st, so
// 2 x 8 x (1 + 1.6e-4) with the error of fast_exp2), so with len >= 64 one
// wrap per step keeps a position in [0, len).
const int32_t kMinSource = 64;

// Per-block settings, in physical units (filled from the control bus).
struct GrainParams {
    int mode;
    float position;      // 0..1 of the source (SCAN: where a new scan starts)
    float size;          // s, grain length
    float density;       // SCAN/CLOUD: grains per second; HIT: grains per burst
    float spray;         // s, random position offset, +-
    float scatter;       // semitones, random pitch offset, +-
    float octave_prob;   // 0..1, chance of a grain an octave up or down
    float reverse_prob;  // 0..1, chance of a grain playing backwards
    float stereo;        // 0..1, random pan spread
    float shape;         // 0 smooth .. 1 percussive grain window
    float scan_speed;    // SCAN: x real time (negative: backwards)
    float pitch;         // semitones relative to the source's own pitch
    bool freeze;         // SCAN position stops moving
    bool active;         // spawn new grains (the voice is sounding)
};

// Source positions are 32.32 fixed point (integer sample, 32-bit fraction).
// A float position would lose sub-sample precision on long sources (its ulp
// is 1/64 sample at 4 s) and makes the wrap-around inexact. In fixed point
// the steps and wraps are exact integer arithmetic.
typedef int64_t q32_t;
// A 32-bit word that may alias the int16 samples (strict aliasing).
typedef uint32_t __attribute__((may_alias)) word_t;
const float kQ32Frac = 2.3283064365e-10f;  // 2^-32

// x in 32.32, for |x| < 2^23. floor(x) is exact. For x >= 0 so is the
// fraction x - floor(x) (Sterbenz), so positions convert exactly. For x < 0
// it is rounded to float, and one that rounds up to 1 is kept just below 1.
// A float fraction below 1 has at most 24 significant bits, so times 2^32 it
// is exact and below 2^32.
inline q32_t to_q32(float x) {
    float fl = (float)(int32_t)x;
    if (fl > x) fl -= 1.0f;  // floor for negative x
    const float fr = x - fl;
    const uint32_t frac = fr < 1.0f ? (uint32_t)(fr * 4294967296.0f) : 0xFFFFFFFFu;
    return (q32_t)(int32_t)fl * (q32_t)4294967296LL + (q32_t)frac;
}

struct Grain {
    q32_t pos, inc;   // source position and step per output sample, 32.32
    int32_t age, len; // output samples; age < 0 = starts later in this block
    float inv_len, gl, gr;    // gains include the 1/32768 of the int16 samples
    float a, ia, ifa, shape;  // window: peak at t = a, ia = 1/a, ifa = 1/(1-a)
    float wn;                 // window at the start of the next run
    bool on;
};

// Grain window at t in [0, 1). The peak sits at t = a, with a = 0.5 - 0.46
// shape: symmetric at shape 0, a 4 % attack at shape 1.
//  - Rise: smoothstep x^2 (3 - 2x) of x = t/a.
//  - Fall: of y = (1-t)/(1-a), smoothstep at shape 0 morphing to y^2 (a
//    percussive decay) at shape 1.
// Both branches are 1 at t = a, 0 at the ends, monotone and in [0, 1]. So
// every shape peaks at exactly 1 (unlike mixing two windows whose peaks sit
// at different t), and grains start and end at zero (no clicks).
// grain_window_r takes the reciprocals ia = 1/a and ifa = 1/(1-a), set once
// per grain. a lies in [0.04, 0.5]; x and y may exceed 1 by a rounding
// error, which moves w by O(1e-7).
inline float grain_attack(float shape) { return 0.5f - 0.46f * clampf(shape, 0.0f, 1.0f); }
inline float grain_window_r(float t, float a, float ia, float ifa, float shape) {
    if (t < a) {
        const float x = t * ia;
        return x * x * (3.0f - 2.0f * x);
    }
    const float y = (1.0f - t) * ifa;
    const float ss = y * y * (3.0f - 2.0f * y);
    return ss + shape * (y * y - ss);
}
inline float grain_window(float t, float shape) {
    const float a = grain_attack(shape);
    return grain_window_r(t, a, 1.0f / a, 1.0f / (1.0f - a), clampf(shape, 0.0f, 1.0f));
}

struct GrainEngine {
    Grain g[kMaxGrains];
    float scan;         // SCAN position, source samples
    float acc;          // spawn accumulator, grains
    int hit_left;       // HIT: grains still to start in this burst
    uint32_t rng;
    uint32_t spawned;   // grains started so far (tests and the log)
    const int16_t *cur_data;  // the source the live grains read
    int32_t cur_len;
    // One run's samples, copied from SDRAM (see render). A run of 16 samples
    // spans 15 |inc| + 2 indices, so up to |inc| ~ 4 (two octaves up) fits.
    static const int32_t kSpan = 64;
    alignas(4) int16_t span[kSpan + 2];

    void init(uint32_t seed) {
        for (int i = 0; i < kMaxGrains; i++) g[i].on = false;
        scan = 0.0f;
        acc = 0.0f;
        hit_left = 0;
        rng = seed ? seed : 1u;
        spawned = 0;
        cur_data = 0;
        cur_len = 0;
    }

    float rnd() {  // xorshift32, uniform in [-1, 1)
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float)(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
    float rnd01() { return 0.5f * (rnd() + 1.0f); }

    int active_count() const {
        int n = 0;
        for (int i = 0; i < kMaxGrains; i++) n += g[i].on;
        return n;
    }

    // Restart the scan at POSITION (a new note in SCAN mode).
    void restart(const GrainSource &s, const GrainParams &p) {
        if (s.len >= kMinSource) scan = clampf(p.position, 0.0f, 1.0f) * (float)(s.len - 1);
    }

    // HIT: start a burst of n grains.
    void hit(int n) { hit_left = n < 1 ? 1 : (n > kMaxGrains ? kMaxGrains : n); }

    // x mod n, for |x| well inside the int32 range. The two reductions are
    // exact up to rounding (a few ulp of x); the last line puts a rounded
    // result that lands outside back into [0, n), so the result is in [0, n)
    // for every input (NaN gives 0).
    static float wrap(float x, float n) {
        if (x >= n) x -= n * (float)(int32_t)(x / n);
        if (x < 0.0f) x += n * (float)(int32_t)(1.0f - x / n);
        return (x >= 0.0f && x < n) ? x : 0.0f;
    }

    // Start one grain, offset samples (0..kBlock-1) into the block. False if
    // the pool is full.
    bool spawn(const GrainSource &s, const GrainParams &p, float overlap, int offset) {
        int i = 0;
        while (i < kMaxGrains && g[i].on) i++;
        if (i == kMaxGrains) return false;
        Grain &q = g[i];
        const float n = (float)s.len;
        const float base = p.mode == GM_SCAN ? scan : clampf(p.position, 0.0f, 1.0f) * (n - 1.0f);
        const float ratio = clampf(s.ratio, 0.125f, 2.0f);  // see kMinSource
        q.pos = to_q32(wrap(base + p.spray * kSampleRate * ratio * rnd(), n));  // in [0, len)
        float st = p.pitch + p.scatter * rnd();
        if (rnd01() < p.octave_prob) st += rnd() < 0.0f ? -12.0f : 12.0f;
        float inc = ratio * fast_exp2(clampf(st, -48.0f, 36.0f) * (1.0f / 12.0f));
        if (rnd01() < p.reverse_prob) inc = -inc;
        q.inc = to_q32(inc);
        int32_t len = (int32_t)(p.size * kSampleRate);
        q.len = len < 16 ? 16 : len;
        q.inv_len = 1.0f / (float)q.len;
        q.age = -(offset < 0 ? 0 : (offset >= kBlock ? kBlock - 1 : offset));
        q.shape = clampf(p.shape, 0.0f, 1.0f);
        q.a = grain_attack(q.shape);
        q.ia = 1.0f / q.a;
        q.ifa = 1.0f / (1.0f - q.a);
        q.wn = 0.0f;  // the window at t = 0
        // Equal-power pan; gain 1/sqrt(overlap) keeps the level of many
        // uncorrelated grains about that of one (powers add). The pool caps
        // the real overlap at kMaxGrains, so the estimate is capped too.
        const float pan = clampf(p.stereo, 0.0f, 1.0f) * rnd();  // -1 left .. +1 right
        const float ang = 0.25f * kPi * (pan + 1.0f);
        const float gain = (1.0f / 32768.0f) / fast_sqrt(clampf(overlap, 1.0f, (float)kMaxGrains));
        q.gl = gain * fast_sin(0.5f * kPi - ang);
        q.gr = gain * fast_sin(ang);
        q.on = true;
        spawned++;
        return true;
    }

    // One block: spawn, advance the scan, then mix all grains into l, r
    // (added to whatever is there).
    //
    // Invariant: every live grain has pos in [0, cur_len) of cur_data, in
    // 32.32. A new source (or none) ends all grains, and the scan is moved
    // inside it.
    //
    // Per grain and block, the samples it plays form one run. The window is
    // evaluated exactly at the run's two ends and linearly in between, as
    // freak ramps its gains across a block. Off the peak, the window is C^2
    // on the run, so the linear-interpolation bound h^2/8 max|w''| with
    // h <= 16 samples gives an error <= 192 / (min(a, 1-a) L)^2 (L the grain
    // length in samples; |w''| <= 6/(aL)^2 on the rise): 1e-4 for a 50 ms
    // smooth grain, 0.02 during the 2 ms attack of a 50 ms percussive one.
    //
    // Reading: if every position of the run stays in [0, len - 1) the fast
    // loop needs no wrap and no bounds check. Positions are exact and linear
    // in the sample index, so the extreme ones are the run's first and last,
    // and checking those two is exact. Otherwise (a run that crosses the
    // source's end) the careful loop wraps after every step.
    void render(const GrainSource &s, const GrainParams &p, float *l, float *r) {
        if (s.data != cur_data || s.len != cur_len) {
            for (int i = 0; i < kMaxGrains; i++) g[i].on = false;
            cur_data = s.data;
            cur_len = s.len;
            if (s.len >= kMinSource) scan = wrap(scan, (float)s.len);
        }
        if (s.len < kMinSource || !s.data) return;
        const float dt = (float)kBlock / kSampleRate;
        if (p.mode == GM_HIT) {
            const float overlap = (float)(hit_left > 0 ? hit_left : 1);
            for (int k = 0; k < 4 && hit_left > 0; k++, hit_left--)  // a burst over a few blocks
                spawn(s, p, overlap, (int)(rnd01() * kBlock));
        } else if (p.active) {
            const float overlap = p.density * p.size;
            // CLOUD: random intervals, uniform in [0.5, 1.5] x the mean.
            acc += p.density * dt * (p.mode == GM_CLOUD ? 0.5f + rnd01() : 1.0f);
            int guard = 0;
            while (acc >= 1.0f && guard++ < kMaxGrains) {
                spawn(s, p, overlap, (int)(rnd01() * kBlock));
                acc -= 1.0f;
            }
            if (acc > 1.0f) acc = 1.0f;  // pool full: do not build up a backlog
        }
        if (p.mode == GM_SCAN && !p.freeze)
            scan = wrap(scan + p.scan_speed * dt * kSampleRate * clampf(s.ratio, 0.125f, 2.0f), (float)s.len);

        const int16_t *d = s.data;
        const int32_t len = s.len;
        const q32_t n64 = (q32_t)len * (q32_t)4294967296LL;
        const bool aligned = ((uintptr_t)d & 3) == 0;  // word reads need it
        for (int i = 0; i < kMaxGrains; i++) {
            Grain &q = g[i];
            if (!q.on) continue;
            int j0 = 0;
            if (q.age < 0) { j0 = -q.age; q.age = 0; }  // starts j0 samples in
            int run = kBlock - j0;
            if (q.len - q.age < run) run = q.len - q.age;
            if (run <= 0) { q.on = false; continue; }

            // w0 is the previous run's w1 (the same expression), so one
            // evaluation per run; full runs avoid the division.
            const float w0 = q.wn;
            const float w1 = grain_window_r((float)(q.age + run) * q.inv_len, q.a, q.ia, q.ifa, q.shape);
            q.wn = w1;
            const float dw = (w1 - w0) * (run == kBlock ? 1.0f / kBlock : 1.0f / (float)run);
            // Samples are interpolated in integers with a 14-bit fraction:
            // s0 2^14 + f (s1 - s0), |.| <= 2^29 + 2^30 < 2^31, so no overflow;
            // the error is 2^-15 of one sample step. The 2^-14 goes in the gains.
            const float gl = q.gl * (1.0f / 16384.0f), gr = q.gr * (1.0f / 16384.0f);
            float wl = gl * w0, wr = gr * w0;
            const float dwl = gl * dw, dwr = gr * dw;
            q32_t pos = q.pos;
            const q32_t inc = q.inc;
            float *lj = l + j0, *rj = r + j0;

            const q32_t last = pos + (q32_t)(run - 1) * inc;
            const q32_t lo = inc >= 0 ? pos : last, hi = inc >= 0 ? last : pos;
            if (lo >= 0 && (int32_t)(hi >> 32) <= len - 2) {
                // Every index i0 is in [i_lo, i_hi - 1] with i_hi <= len - 1.
                const int32_t i_lo = (int32_t)(lo >> 32), i_hi = (int32_t)(hi >> 32) + 1;
                // Single 16-bit SDRAM reads cost ~20 cycles each on the board.
                // When the run's samples fit, copy them first with aligned
                // 32-bit reads (half as many accesses, sequential) into
                // internal RAM. Words from a0 = i_lo rounded down to even
                // through i_hi lie inside the source: (i_hi - a0)/2 + 1 words
                // end at index a0 + 2 nw - 1 <= i_hi + 1, and that index is
                // < len unless i_hi = len - 1 with i_hi - a0 even; then the
                // last word is read as two halves instead.
                const int32_t a0 = i_lo & ~1;
                const int16_t *base = d;
                int32_t shift = 0;
                if (aligned && i_hi - a0 < kSpan) {
                    const word_t *src = (const word_t *)(d + a0);
                    word_t *dst = (word_t *)span;
                    const int32_t nw = (i_hi - a0) / 2 + 1;
                    const bool tail = a0 + 2 * nw > len;  // last word would pass the end
                    for (int32_t w = 0; w < nw - (tail ? 1 : 0); w++) dst[w] = src[w];
                    if (tail) span[2 * nw - 2] = d[a0 + 2 * nw - 2];
                    base = span;
                    shift = a0;
                }
                // Two samples per iteration, so the FPU latencies overlap.
                auto step = [&](int k) {
                    const int16_t *pp = base + ((int32_t)(pos >> 32) - shift);
                    const int32_t s0 = pp[0], fr = (int32_t)((uint32_t)pos >> 18);
                    const float x = (float)(s0 * 16384 + fr * (pp[1] - s0));
                    lj[k] += wl * x;
                    rj[k] += wr * x;
                    wl += dwl;
                    wr += dwr;
                    pos += inc;
                };
                int k = 0;
                for (; k + 1 < run; k += 2) {
                    step(k);
                    step(k + 1);
                }
                if (k < run) step(k);
                // pos = last + inc, with last in [0, (len-1) 2^32) and
                // |inc| < 17 2^32 < n64: one wrap brings it into [0, n64).
                if (pos >= n64) pos -= n64;
                else if (pos < 0) pos += n64;
            } else {
                for (int k = 0; k < run; k++) {
                    const int32_t i0 = (int32_t)(pos >> 32);  // in [0, len): invariant
                    const int32_t i1 = i0 + 1 < len ? i0 + 1 : 0;
                    const int32_t s0 = d[i0], fr = (int32_t)((uint32_t)pos >> 18);
                    const float x = (float)(s0 * 16384 + fr * (d[i1] - s0));
                    lj[k] += wl * x;
                    rj[k] += wr * x;
                    wl += dwl;
                    wr += dwr;
                    pos += inc;  // |inc| < n64, so one wrap suffices
                    if (pos >= n64) pos -= n64;
                    else if (pos < 0) pos += n64;
                }
            }
            q.pos = pos;
            q.age += run;
            if (q.age >= q.len) q.on = false;
        }
    }
};

}  // namespace freak

#endif
