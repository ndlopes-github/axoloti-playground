// quartet_voice.h: the "quartet" patch's voice, after the Roland Jupiter-4.
//
// Signal path (the Jupiter-4 owner's manual; Nuno's unit where the manual's
// scan is unclear): VCO (saw, square, pulse in four widths, pulse with PWM
// from the LFO, or off) + a square sub-oscillator an octave down + noise ->
// high-pass (1-pole, no resonance) -> low-pass (4-pole OTA cascade,
// resonance up to self-oscillation) -> VCA. One ADSR per voice, with
// settings shared by all voices, drives the low-pass (either polarity) and
// the VCA; VCA GATE gives the VCA the key's gate instead (Nuno's
// simplification: the Jupiter-4 has two ADSRs). Shared modulators
// (QuartetMod): the LFO (sine, square, saw, inverted saw, 0.1-80 Hz, fading
// in over the delay time after a fresh key) and the S&H: noise sampled at
// the trigger generator's rate (1-25 Hz) moves the low-pass cutoff, as the
// manual's "SAMPLE AND HOLD with NOISE as the sample source".
//
// Low-pass. The Jupiter-4's is a cascade of four OTA one-poles with
// resonance feedback, limited by diodes (AMSynths, who sell a clone; BA662
// OTAs on early voice cards, the IR3109 on later ones). Model, per stage i,
// with y_0 = u:
//   dy_i/dt = wc T(c (y_{i-1} - y_i)) / c,   u = x - F(y_4),
// where T is the OTA's tanh-shaped transfer, c sets how hard the stages are
// driven, and F(y) = L f(k y / L) is the resonance feedback k y limited at
// +-2L/3 (f the cubic soft clip x - x^3/3, +-2/3 outside [-1, 1]: odd,
// nondecreasing, C^1). T here is the rational stand-in
//   T(z) = z (27 + z^2) / (27 + 9 z^2) on |z| <= 3, sign(z) outside,
// odd, C^1, nondecreasing (T'(z) = 9 (z^2 - 9)^2 / (27 + 9 z^2)^2 >= 0)
// and bounded by 1; sigma(z) = T(z)/z is in (0, 1]. For small signals
// (c -> 0, L -> inf) the model is the linear ladder Y/X = P/(1 + k P),
// P = (wc/(s + wc))^4, which self-oscillates at wc from k = 4.
//
// Discretisation. The trapezoidal rule (TPT) gives, per stage with input v
// and state s, the implicit equation y = s + (g/c) T(c (v - y)), i.e.
// y = s + g sigma(z) (v - y) with z = c (v - y), g = tan(pi fc/fs)
// prewarped; then s' = 2y - s. For a given sigma the stage is the linear
// one-pole y = G v + (1 - G) s with G = a/(1 + a), a = g sigma. Each sample
// takes two passes over the cascade with sigma fixed:
//  - predictor: sigma_i at the previous sample's differences z_i (each OTA
//    at the gain its last input difference allowed it: the "cheap
//    non-linear zero-delay filters" shortcut, Teemu Voipio, KVR forum);
//  - corrector: sigma_i at the differences the predictor gives for this
//    sample, i.e. one fixed-point step on the implicit equations.
// The lag-one predictor alone drifts with frequency: self-oscillating at
// RESONANCE 1 it ran at 0.88 fc at 300 Hz but 0.96 fc at 4 kHz, where the
// exact implicit solve runs at 0.866-0.870 fc (and 0.878 at 10 kHz) with a
// steady level. With the corrector: 0.866-0.877 fc to 4 kHz, 0.898 fc at
// 10 kHz, and waveforms within 0.3 % rms of the exact solve (measured
// against a double-precision implicit solve, tests/test_quartet.cpp; not
// a proof of convergence of the fixed-point step).
// With the sigma_i fixed the cascade is linear: y_4 = Gm u + S, Gm = prod
// G_i, S its output for u = 0. The feedback u = x - F(y_4) leaves one
// scalar equation per pass,
//   r(y) = y + Gm F(y) - Gm x - S = 0.
// F is nondecreasing, so r' = 1 + Gm F' >= 1: r is strictly increasing and
// onto R, and the root exists and is unique for every setting (proved).
// Solve. With w = k y/L, b = k A/L (A = Gm x + S) and beta = k Gm, the
// equation is h(w) = w + beta f(w) = b, h odd and increasing; beta <=
// kQResMax Gmax^4 < 1.8. (i) If |b| >= h(1) = 1 + 2 beta/3, then |w| >= 1,
// f(w) = +-2/3 and w = b -+ 2 beta/3 exactly. (ii) Otherwise |w| < 1, where
// h(w) = (1 + beta) w - beta w^3/3 is concave on [0, 1] (convex on
// [-1, 0]); so for b > 0 (the case b < 0 is symmetric)
// (1 + 2 beta/3) w <= h(w) <= (1 + beta) w there, and the root is
// w = b/(1 + beta s) with s = f(w)/w in [2/3, 1]. The start takes s from
// a nearby w (the previous sample's for the predictor, the predictor's for
// the corrector), clamped to [2/3, 1], so it lies in that bracket. Newton on
// the concave increasing cubic rises monotonically to the root from a start
// left of it; from a start right of it the first step lands left of the
// root but at or above b (1 - beta/3)/(1 + 2 beta/3) > 0 (f' >= 1 bounds
// the step by h(w0) - b <= b (beta/3)/(1 + 2 beta/3)), and the monotone
// case follows. So the iterates stay in [0, 1], where the cubic form holds
// (proved). The predictor takes the start alone (its root only places the
// corrector's sigma); the corrector takes kQNewton = 2 steps, which leave
// at most 6e-6 in y on a saw swept 50 Hz - 18 kHz at RESONANCE 1, at 1x
// and 2x the voice's level (measured). Divisions per sample: 2 for the
// stage gains, 1 + 1 + 2 for the solves.
//
// Stability. sigma <= 1 and the cutoff is clamped to kQMaxCut, so every
// stage has G in (0, Gmax], Gmax = gmax/(1 + gmax), gmax = tan(pi kQMaxCut
// / fs) = 3.73. For one stage with input |v| <= V and |s| <= rho V, where
// rho = max(1, Gmax/(1 - Gmax)) = 3.73: s' = 2 G v + (1 - 2G) s, so for
// G <= 1/2 |s'| <= 2GV + (1 - 2G) rho V <= rho V, and for G > 1/2
// |s'| <= 2GV + (2G - 1) rho V <= rho V because rho >= G/(1 - G); and
// |y| <= G V + (1 - G) rho V <= rho V. Whatever the solve returns, the
// fed-back F is bounded by 2L/3, so |u| <= |x| + 2L/3 and, stage by stage,
// |y_4| <= rho^4 (sup|x| + 2L/3), for every setting and every time-varying
// sigma_i and cutoff (proved, exact arithmetic; the bound is loose).
//
// Oscillators, at 48 kHz without oversampling (4 voices; raw's 2x would not
// fit): the saw 2p - 1, the pulse saw(p) - saw(p - w) (zero mean for every
// width w), the square as the pulse at w = 1/2, and the sub as a flip-flop
// that toggles at each saw reset, so it is phase-locked an octave down as
// on the Jupiter-4. Every edge gets a 4-point BLEP residual (integrated
// cubic B-spline, two samples each side); the sub's edges fall where the
// saw's do. Measured alias-to-signal ratio of the saw to 20 kHz: -57 dB at
// C4, -50 dB at C6, -48 dB at C7 (the 2-point PolyBLEP of raw_voice.h at
// 48 kHz: -43, -36, -34 dB). The B-spline also rolls the top octave off
// a little (sinc^4: -6.6 dB at 16 kHz, against sinc^2's -3.3 dB).
//
// Additions, for the character of an analog voice (Nuno can veto them):
// every voice drifts slowly in pitch, up to kQDriftCents, as analog
// oscillators do; the allocator can offset each voice (DETUNE, for unison).
//
// Rates. The envelope and the gate ramp run per sample. Pitch and the
// low-pass cutoff are computed every kQSub = 4 samples (12 kHz) and
// interpolated linearly in between (dt, 1/dt and g; an interpolated g
// stays within (0, gmax], so the bounds above hold at every sample). The
// LFO and the S&H are computed per sub-block, once for all voices.
//
// Header-only, C++11, no libm, every struct a POD.
#ifndef QUARTET_VOICE_H
#define QUARTET_VOICE_H

#include <stdint.h>

#include "freak_dsp.h"

namespace freak {

const int kQSub = 4;                     // samples per pitch/cutoff update
const int kQSubs = kBlock / kQSub;       // sub-blocks per block
const float kQMaxCut = 20000.0f;         // Hz, low-pass cutoff clamp
const float kQMaxDt = 0.24f;             // oscillator: < 1/4 for the 4-point BLEP (11.5 kHz)
const float kQResMax = 4.6f;             // feedback at RESONANCE 1 (self-oscillates from 4)
// c: OTA drive. With c = 1 a saw + sub at C3 through the low-pass (no
// resonance) differs from the same filter with linear stages by 12 % rms
// at a 300 Hz cutoff, 3.7 % at 2 kHz and 1.2 % at 8 kHz: the stages
// slew-limit at low cutoffs, as OTAs do. L: the resonance limiter's level;
// with L = 3 self-oscillation at RESONANCE 1 peaks at ~0.45, 0.87-0.90 of
// the cutoff in pitch (a saw + sub through the open filter peaks at 0.82).
// Both chosen by measurement (tests/test_quartet.cpp), not from the circuit.
const float kQOta = 1.0f;
const float kQLim = 3.0f;
const int kQNewton = 2;                  // the corrector's Newton steps after the bracketed start
const float kQDriftCents = 3.0f;         // analog pitch drift, peak
const float kQGateTime = 0.002f;         // VCA GATE: 99 % of the edge in 2 ms
const float kQMinAttack = 0.0006f;       // the Jupiter-4's shortest attack
// mix into the filter: the oscillator and noise are switched, as on the
// Jupiter-4; the sub has four levels (Nuno asked), kQLvlSub is the third
const float kQLvlWave = 0.5f, kQLvlSub = 0.4f, kQLvlNoise = 0.35f;
static const float kQSubLevels[4] = {0.0f, 0.2f, kQLvlSub, 0.6f};  // OFF, LOW, MID, HIGH
// A noise floor at -80 dB re 1 at the filter input, always on: as circuit
// noise does, it lets the low-pass start self-oscillating with every
// source switched off.
const float kQFloor = 1e-4f;

enum { QW_SAW, QW_SQUARE, QW_PULSE, QW_PWM, QW_OFF, NUM_QW };
enum { QL_SINE, QL_SQUARE, QL_SAW, QL_INVSAW, NUM_QL };

// What the voices read, set by the controller once per block.
struct QuartetParams {
    int wave;            // QW_*
    float pw;            // QW_PULSE: width, 0.1 .. 0.5
    float pwm;           // QW_PWM: depth 0..1, width = 0.5 - 0.45 pwm (1 + lfo)/2
    float sub;           // sub-oscillator level into the filter (0 = off; the saw is kQLvlWave)
    bool noise;
    int range;           // octaves from 8': -2 (32') .. +1 (4')
    float hp_hz;         // high-pass cutoff
    float lp_oct;        // low-pass cutoff = 20 Hz 2^lp_oct at note 60, before modulation
    float res;           // 0..1: feedback k = kQResMax res
    float env_vcf;       // octaves at envelope 1
    bool env_inv;        // envelope polarity on the low-pass
    float kybd;          // keyboard follow: 0.1, 0.4, 0.7, 1.0
    float vco_lfo;       // semitones at LFO +-1
    float vcf_lfo;       // octaves at LFO +-1
    float vca_lfo;       // 0..1: gain dips to 1 - vca_lfo
    float sh_vcf;        // octaves at S&H +-1
    float a, d, s, r;    // ADSR: s, s, level, s
    bool vca_gate;       // VCA follows the gate, not the ADSR
    float glide;         // s for 99 % of an interval, 0 = off
    float bend_st;       // pitch wheel: semitones
    float bend_oct;      // pitch wheel: octaves on the low-pass
    float bend_db;       // pitch wheel: dB on the VCA
    float vib;           // mod wheel: extra semitones of LFO on pitch
    float lfo_hz;
    int lfo_wave;        // QL_*
    float lfo_delay;     // s: the LFO fades in over this time after a fresh key
    float trig_hz;       // trigger generator (S&H; the arpeggiator)
    float level;         // output gain
};

struct QRng {
    uint32_t s;
    float uni() {  // xorshift32, uniform in [-1, 1)
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(s >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

// tan(pi fc / 48 kHz) for fc in [5, kQMaxCut], so x = pi fc/fs <= 1.309.
// Taylor polynomials of degree 11 (sin) and 12 (cos); for x <= 1.309 their
// remainders are below x^13/13! < 5.3e-9 and x^14/14! < 5e-10, and
// sin x >= 0.714 x, cos x >= 0.258 there, so the relative error of
// g = s/c is below 8e-9 (proved; float rounding adds ~1e-7).
inline void q_sincos(float fc, float &s, float &c) {
    fc = clampf(fc, 5.0f, kQMaxCut);
    const float x = fc * (kPi / kSampleRate), x2 = x * x;
    s = x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 +
        x2 * (1.0f / 362880 + x2 * (-1.0f / 39916800))))));
    c = 1.0f + x2 * (-0.5f + x2 * (1.0f / 24 + x2 * (-1.0f / 720 + x2 * (1.0f / 40320 +
        x2 * (-1.0f / 3628800 + x2 * (1.0f / 479001600))))));
}
inline float q_g(float fc) {
    float s, c;
    q_sincos(fc, s, c);
    return s / c;
}
// G = g/(1 + g) = s/(s + c), with s + c = sqrt(2) sin(x + pi/4) >= 1 on
// [0, pi/2], so G is within ~1e-8 (proved) and in (0, Gmax].
inline float q_G(float fc) {
    float s, c;
    q_sincos(fc, s, c);
    return s / (s + c);
}

// The tanh stand-in T (see the header) and sigma = T(z)/z as N/D.
inline float q_tanh(float z) {
    if (z >= 3.0f) return 1.0f;
    if (z <= -3.0f) return -1.0f;
    const float z2 = z * z;
    return z * (27.0f + z2) / (27.0f + 9.0f * z2);
}
inline void q_sigma(float z, float &N, float &D) {
    const float a = z < 0.0f ? -z : z;
    if (a >= 3.0f) { N = 1.0f; D = a; return; }
    const float z2 = z * z;
    N = 27.0f + z2;
    D = 27.0f + 9.0f * z2;
}

// Cubic soft clip and its derivative.
inline float q_clip3(float x) {
    if (x >= 1.0f) return 2.0f / 3.0f;
    if (x <= -1.0f) return -2.0f / 3.0f;
    return x - x * x * x * (1.0f / 3.0f);
}
inline float q_clip3d(float x) {
    if (x >= 1.0f || x <= -1.0f) return 0.0f;
    return 1.0f - x * x;
}

// The 4-pole OTA low-pass (see the header).
struct OtaLadder {
    float s1, s2, s3, s4;  // TPT states
    float z1, z2, z3, z4;  // c (stage input - stage output), last sample
    float w;               // last k y / L (the solve's start)
    float c, lim;          // kQOta, kQLim (the tests linearise with c -> 0, lim -> inf)
    float k, kl, lk;       // feedback, k/L, L/k (set_k, once per block)

    void init() {
        s1 = s2 = s3 = s4 = 0.0f;
        z1 = z2 = z3 = z4 = 0.0f;
        w = 0.0f;
        c = kQOta;
        lim = kQLim;
        set_k(0.0f);
    }

    void set_k(float kk) {
        k = kk > 0.0f ? kk : 0.0f;
        kl = k / lim;
        lk = k > 0.0f ? lim / k : 0.0f;
    }

    // Stage gains for the differences e1..e4, and Gm = prod G_i, S (the
    // cascade's output for u = 0). One division for the four: with
    // d_i = D_i + a_i, 1/d_1 = d_2 d_3 d_4 / (d_1 d_2 d_3 d_4), etc.
    void gains(float g, float e1, float e2, float e3, float e4, float G[4], float &Gm, float &S) const {
        float N1, D1, N2, D2, N3, D3, N4, D4;
        q_sigma(e1, N1, D1);
        q_sigma(e2, N2, D2);
        q_sigma(e3, N3, D3);
        q_sigma(e4, N4, D4);
        const float a1 = g * N1, a2 = g * N2, a3 = g * N3, a4 = g * N4;
        const float d1 = D1 + a1, d2 = D2 + a2, d3 = D3 + a3, d4 = D4 + a4;
        const float d12 = d1 * d2, d34 = d3 * d4;
        const float inv = 1.0f / (d12 * d34);
        G[0] = a1 * d2 * d34 * inv;
        G[1] = a2 * d1 * d34 * inv;
        G[2] = a3 * d4 * d12 * inv;
        G[3] = a4 * d3 * d12 * inv;
        Gm = (a1 * a2) * (a3 * a4) * inv;
        S = (((1.0f - G[0]) * s1 * G[1] + (1.0f - G[1]) * s2) * G[2] + (1.0f - G[2]) * s3) * G[3] +
            (1.0f - G[3]) * s4;
    }

    // The root w of w + beta f(w) = b (see the header), by n Newton steps
    // from the bracketed start at the previous root wp.
    static float solve(float wp, float b, float beta, int n) {
        const float sat = beta * (2.0f / 3.0f);
        if (b >= 1.0f + sat) return b - sat;
        if (b <= -1.0f - sat) return b + sat;
        const float ap = wp < 0.0f ? -wp : wp;
        float s = ap <= 1.0f ? 1.0f - wp * wp * (1.0f / 3.0f) : (2.0f / 3.0f) / ap;
        if (s < 2.0f / 3.0f) s = 2.0f / 3.0f;
        float w = b / (1.0f + beta * s);
        for (int i = 0; i < n; i++) {
            const float w2 = w * w;
            w -= ((1.0f + beta) * w - beta * (1.0f / 3.0f) * w2 * w - b) / (1.0f + beta - beta * w2);
        }
        return w;
    }

    // The predictor pass (gains at the last sample's differences, the
    // solve's start alone): this sample's predicted differences e[] and root.
    void predict(float x, float g, float e[4], float &w0) const {
        float G[4], Gm, S;
        gains(g, z1, z2, z3, z4, G, Gm, S);
        w0 = solve(w, kl * (Gm * x + S), k * Gm, 0);
        const float u = x - lim * q_clip3(w0);  // u = x - F(y4), F(y4) = lim f(w)
        const float y1 = G[0] * (u - s1) + s1;
        const float y2 = G[1] * (y1 - s2) + s2;
        const float y3 = G[2] * (y2 - s3) + s3;
        const float y4 = G[3] * (y3 - s4) + s4;
        e[0] = c * (u - y1);
        e[1] = c * (y1 - y2);
        e[2] = c * (y2 - y3);
        e[3] = c * (y3 - y4);
    }

    float process(float x, float g) {
        float e[4], w0;
        predict(x, g, e, w0);
        // the corrector pass, which advances the states
        float G[4], Gm, S;
        gains(g, e[0], e[1], e[2], e[3], G, Gm, S);
        w = solve(w0, kl * (Gm * x + S), k * Gm, kQNewton);
        const float u = x - lim * q_clip3(w);
        const float y1 = G[0] * (u - s1) + s1;
        s1 = 2.0f * y1 - s1;
        const float y2 = G[1] * (y1 - s2) + s2;
        s2 = 2.0f * y2 - s2;
        const float y3 = G[2] * (y2 - s3) + s3;
        s3 = 2.0f * y3 - s3;
        const float y4 = G[3] * (y3 - s4) + s4;
        s4 = 2.0f * y4 - s4;
        z1 = c * (u - y1);
        z2 = c * (y1 - y2);
        z3 = c * (y2 - y3);
        z4 = c * (y3 - y4);
        return y4;
    }
};

// The board's low-pass: the same cascade and limiter with linear stages
// (sigma = 1). Per sample one scalar solve (start + kQNewton steps, as in
// OtaLadder) and no stage gains to compute: G is the same for the four
// stages and comes interpolated from the sub-block. The limiter is
// memoryless, so the loop's phase condition is the linear ladder's and the
// self-oscillation sits at fc for every fc (measured: within 0.1 %).
struct Ladder4 {
    float s1, s2, s3, s4;  // TPT states
    float w;               // last k y / L (the solve's start)
    float lim, k, kl;      // kQLim, feedback, k/L

    void init() {
        s1 = s2 = s3 = s4 = 0.0f;
        w = 0.0f;
        lim = kQLim;
        set_k(0.0f);
    }
    void set_k(float kk) {
        k = kk > 0.0f ? kk : 0.0f;
        kl = k / lim;
    }
    // G = g/(1 + g) of the four stages
    float process(float x, float G) {
        const float H = 1.0f - G, G2 = G * G, Gm = G2 * G2;
        const float S = H * (((s1 * G + s2) * G + s3) * G + s4);
        w = OtaLadder::solve(w, kl * (Gm * x + S), k * Gm, kQNewton);
        const float u = x - lim * q_clip3(w);
        const float y1 = G * (u - s1) + s1;
        s1 = 2.0f * y1 - s1;
        const float y2 = G * (y1 - s2) + s2;
        s2 = 2.0f * y2 - s2;
        const float y3 = G * (y2 - s3) + s3;
        s3 = 2.0f * y3 - s3;
        const float y4 = G * (y3 - s4) + s4;
        s4 = 2.0f * y4 - s4;
        return y4;
    }
};

// 4-point BLEP: the residual R = S - H of a unit upward step H at t = 0,
// where S is the integral of the cubic B-spline (Valimaki, Pekonen, Nam,
// JASA 2012); t in samples from the step, support (-2, 2). With a = |t|:
// R = sign(-t) tail(a), tail(a) = 1/2 - (2a/3 - a^3/3 + a^4/8) on [0, 1)
// and (2 - a)^4/24 on [1, 2): continuous, R(0-) = 1/2, R(0+) = -1/2.
inline float q_blep4(float t) {
    const float a = t < 0.0f ? -t : t;
    float r;
    if (a < 1.0f) {
        r = 0.5f - a * (2.0f / 3.0f - a * a * (1.0f / 3.0f - a * (1.0f / 8.0f)));
    } else {
        const float u = 2.0f - a, u2 = u * u;
        r = u2 * u2 * (1.0f / 24.0f);
    }
    return t < 0.0f ? r : -r;
}
// The residuals of the steps at phase 0 within two samples: the one just
// passed (t = p/dt in [0, 2)) and the next (t = (p - 1)/dt in (-2, 0)).
// They are different steps because dt <= kQMaxDt < 1/4.
inline float q_edge(float p, float idt) {
    const float ta = p * idt, tb = (p - 1.0f) * idt;
    float r = 0.0f;
    if (ta < 2.0f) r += q_blep4(ta);
    if (tb > -2.0f) r += q_blep4(tb);
    return r;
}
// Band-limited saw 2p - 1 (falls by 2 at p = 0) and pulse of width w,
// saw(p) - saw(p - w): 2w - 2 on [0, w), 2w after, zero mean for every w.
inline float q_saw(float p, float idt) { return 2.0f * p - 1.0f - 2.0f * q_edge(p, idt); }
inline float q_pulse(float p, float w, float idt) {
    float q = p - w;
    if (q < 0.0f) q += 1.0f;
    return q_saw(p, idt) - q_saw(q, idt);
}

// The shared modulators and the per-block values every voice reads.
struct QuartetMod {
    float lfo_phase, trig_phase, sh_val, fade_t;
    uint32_t trig_count;  // trigger pulses so far (the arpeggiator's clock)
    QRng rng;
    float lfo[kQSubs];    // LFO after the delay fade, -1..1, per sub-block
    float sh[kQSubs];     // S&H, -1..1, per sub-block
    float ca, cd, cr, cg, sus;  // ADSR and gate-ramp coefficients
    float ghp0, ghp1;     // high-pass G at the start and end of the block

    void init() {
        lfo_phase = trig_phase = sh_val = 0.0f;
        fade_t = 1e6f;
        trig_count = 0;
        rng.s = 0x85EBCA6Bu;
        for (int i = 0; i < kQSubs; i++) lfo[i] = sh[i] = 0.0f;
        ca = cd = cr = cg = 1.0f;
        sus = 1.0f;
        const float g = q_g(10.0f);
        ghp0 = ghp1 = g / (1.0f + g);
    }

    // A key played after all keys were up: the LFO delay starts over.
    void restart() { fade_t = 0.0f; }

    static float wave(float p, int w) {
        switch (w) {
        case QL_SINE: return fast_sin(2.0f * kPi * p);
        case QL_SQUARE: return p < 0.5f ? 1.0f : -1.0f;
        case QL_SAW: return 2.0f * p - 1.0f;
        default: return 1.0f - 2.0f * p;  // inverted saw
        }
    }

    void block(const QuartetParams &p) {
        ca = one_pole_coef(p.a < kQMinAttack ? kQMinAttack : p.a, kSampleRate, kLog2_6);
        cd = one_pole_coef(p.d, kSampleRate, kLog2_100);
        cr = one_pole_coef(p.r, kSampleRate, kLog2_100);
        cg = one_pole_coef(kQGateTime, kSampleRate, kLog2_100);
        sus = clampf(p.s, 0.0f, 1.0f);
        ghp0 = ghp1;
        const float gh = q_g(p.hp_hz);
        ghp1 = gh / (1.0f + gh);
        const float step = (float)kQSub / kSampleRate;
        const float dl = clampf(p.lfo_hz, 0.0f, 200.0f) * step;
        const float dg = clampf(p.trig_hz, 0.0f, 100.0f) * step;
        const float idelay = p.lfo_delay > 0.0f ? 1.0f / p.lfo_delay : 1e9f;
        for (int i = 0; i < kQSubs; i++) {
            lfo_phase += dl;
            if (lfo_phase >= 1.0f) lfo_phase -= (float)(int32_t)lfo_phase;
            trig_phase += dg;
            if (trig_phase >= 1.0f) {
                trig_phase -= (float)(int32_t)trig_phase;
                sh_val = rng.uni();
                trig_count++;
            }
            if (fade_t < 1e6f) fade_t += step;
            const float fade = clampf(fade_t * idelay, 0.0f, 1.0f);
            lfo[i] = fade * wave(lfo_phase, p.lfo_wave);
            sh[i] = sh_val;
        }
    }
};

// The Ensemble: the Jupiter-4's is a stereo chorus on two MN3004
// bucket-brigade delays with LFO-modulated delay times (G-Storm Electro's
// clone; no rates are published). Here: one delay line fed by the mono
// voice sum, read twice by a triangle LFO in opposite phase for left and
// right, the wet signal low-passed as a BBD's filters do (one pole at ~7 kHz
// before the line and one after each tap: 12 dB/oct on the wet path), mixed
// with the dry signal: on, L = 0.7 (dry + wet L), R = 0.7 (dry + wet R).
// Switching crossfades over 20 ms. Rate and depth are guesses (a clone
// maker calls the original's swirl "lazy"): 0.6 Hz, 4 ms +- 2.2 ms, so the
// wet taps are detuned by at most +-9 cents (delay slope 4 depth rate =
// 5.3e-3); Nuno can tune them by ear against his unit. Output bounded:
// |L|, |R| <= 0.7 (sup|in| + sup|in|) = 1.4 sup|in| (the one-poles have gain
// <= 1 and the crossfade is convex).
const int kQEnsLen = 512;                             // 10.7 ms
const float kQEnsHz = 0.6f;
const float kQEnsCentre = 4.0e-3f * kSampleRate;      // samples
const float kQEnsDepth = 2.2e-3f * kSampleRate;       // samples, so 86 .. 298 of 512
const float kQEnsLp = 0.600f;                         // 1 - exp(-2 pi 7 kHz / 48 kHz)
const float kQEnsFadeStep = 1.0f / 60.0f;             // per block: 20 ms

struct QEnsemble {
    float buf[kQEnsLen];
    int w;
    float phase, wg, lpi, lpl, lpr;  // LFO phase, wet gain, the three one-poles

    void init() {
        for (int i = 0; i < kQEnsLen; i++) buf[i] = 0.0f;
        w = 0;
        phase = wg = lpi = lpl = lpr = 0.0f;
    }
    float read(float d) const {  // d samples ago, 1 <= d <= kQEnsLen - 2
        float rp = (float)w - d;
        if (rp < 0.0f) rp += (float)kQEnsLen;
        const int i = (int)rp;
        const float f = rp - (float)i;
        const int j = i + 1 < kQEnsLen ? i + 1 : 0;
        return buf[i] + f * (buf[j] - buf[i]);
    }
    void process(bool on, const float *in, float *l, float *r) {
        const float target = on ? 1.0f : 0.0f;
        const float g0 = wg;
        wg += clampf(target - wg, -kQEnsFadeStep, kQEnsFadeStep);
        const float dg = (wg - g0) * (1.0f / kBlock);
        const float dp = kQEnsHz / kSampleRate;
        float g = g0, ph = phase, li = lpi, ll = lpl, lr = lpr;
        int wp = w;
        if (g0 == 0.0f && wg == 0.0f) {  // off: keep the line fed, no reads
            for (int i = 0; i < kBlock; i++) {
                li += kQEnsLp * (in[i] - li);
                buf[wp] = li;
                if (++wp >= kQEnsLen) wp = 0;
                l[i] = r[i] = in[i];
            }
            ph += dp * kBlock;
            if (ph >= 1.0f) ph -= 1.0f;
        } else {
            for (int i = 0; i < kBlock; i++) {
                li += kQEnsLp * (in[i] - li);
                buf[wp] = li;
                w = wp;
                const float tri = 4.0f * absf(ph - 0.5f) - 1.0f;
                ll += kQEnsLp * (read(kQEnsCentre + kQEnsDepth * tri) - ll);
                lr += kQEnsLp * (read(kQEnsCentre - kQEnsDepth * tri) - lr);
                g += dg;
                const float dry = in[i] * (1.0f - 0.3f * g);
                l[i] = dry + 0.7f * g * ll;
                r[i] = dry + 0.7f * g * lr;
                ph += dp;
                if (ph >= 1.0f) ph -= 1.0f;
                if (++wp >= kQEnsLen) wp = 0;
            }
        }
        w = wp; phase = ph; lpi = li; lpl = ll; lpr = lr;
    }
};

// One ADSR step (shared by env_tick and the render loop, where the state
// lives in locals).
inline void q_adsr(int &stage, float &x, float ca, float cd, float cr, float sus) {
    enum { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
    switch (stage) {
    case ATTACK:
        x += ca * (1.2f - x);
        if (x >= 1.0f) { x = 1.0f; stage = DECAY; }
        break;
    case DECAY:
        x += cd * (sus - x);
        if (absf(x - sus) < 1e-4f) { x = sus; stage = SUSTAIN; }
        break;
    case SUSTAIN:
        x = sus;
        break;
    case RELEASE:
        x -= cr * x;
        if (x < 1e-5f) { x = 0.0f; stage = IDLE; }
        break;
    default:
        x = 0.0f;
    }
}

struct QuartetVoice {
    enum { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };  // as q_adsr
    Ladder4 lpf;
    float hs;               // high-pass state
    float env;              // ADSR output
    int stage;
    bool env_gate;
    float gate_x;           // VCA GATE ramp
    QRng rng, drift_rng;
    float p;                // oscillator phase
    float sub_sign;         // the flip-flop
    float pitch;            // glided note, semitones
    bool primed;
    float dt, idt, G;       // values at the end of the last sub-block
    float drift, drift_target;
    uint32_t drift_n;
    float amp_prev;
    // set by the allocator
    uint8_t note;
    bool gate;
    uint32_t trig, trig_seen;  // a new trig retriggers the ADSR
    float detune;           // semitones (unison spread)

    void init(uint32_t seed) {
        lpf.init();
        hs = 0.0f;
        env = 0.0f;
        stage = IDLE;
        env_gate = false;
        gate_x = 0.0f;
        rng.s = 0x9E3779B9u ^ (seed * 0x85EBCA6Bu);
        drift_rng.s = 0x27D4EB2Fu + seed * 0x165667B1u;
        if (!rng.s) rng.s = 1;
        p = 0.0f;
        sub_sign = 1.0f;
        pitch = 60.0f;
        primed = false;
        dt = 0.0f;
        idt = 1.0f;
        G = 0.0f;
        drift = drift_target = 0.0f;
        drift_n = 0;
        amp_prev = 0.0f;
        note = 60;
        gate = false;
        trig = trig_seen = 0;
        detune = 0.0f;
    }

    bool idle() const { return !gate && stage == IDLE && gate_x < 1e-5f; }

    static float note_dt(float n) {
        const float f = 440.0f * fast_exp2((n - 69.0f) * (1.0f / 12.0f));
        return clampf(f * (1.0f / kSampleRate), 1e-6f, kQMaxDt);
    }

    float env_tick(const QuartetMod &m) {
        if (!gate && env_gate && stage != IDLE) stage = RELEASE;
        env_gate = gate;
        q_adsr(stage, env, m.ca, m.cd, m.cr, m.sus);
        return env;
    }

    // Add one block (kBlock samples) of this voice to out. The state the
    // sample loop touches is copied to locals and back: stores to out[]
    // would otherwise force the compiler to reload every member.
    void render(const QuartetParams &P, const QuartetMod &m, float *out) {
        // a new note: attack from the current level (no click). A key that
        // went up within the same block still gets its attack, then release.
        if (trig != trig_seen) {
            trig_seen = trig;
            stage = ATTACK;
            env_gate = true;
        }
        if (idle()) { amp_prev = 0.0f; return; }
        // the gate is constant over the block
        if (!gate && env_gate && stage != IDLE) stage = RELEASE;
        env_gate = gate;

        // glide and drift, per block
        const float target = (float)note;
        if (!primed || P.glide <= 0.0f) { pitch = target; primed = true; }
        const float pitch0 = pitch;
        if (P.glide > 0.0f) pitch += one_pole_coef(P.glide, kControlRate, kLog2_100) * (target - pitch);
        if (drift_n == 0) {
            drift_target = drift_rng.uni() * (kQDriftCents * 0.01f);
            drift_n = (uint32_t)(kControlRate * 1.5f);
        }
        drift_n--;
        drift += 0.002f * (drift_target - drift);  // ~0.17 s time constant at 3 kHz

        const int wave = P.wave;
        const float lw = wave == QW_OFF ? 0.0f : kQLvlWave;
        const float ls = clampf(P.sub, 0.0f, 1.0f), ln = (P.noise ? kQLvlNoise : 0.0f) + kQFloor;
        lpf.set_k(kQResMax * clampf(P.res, 0.0f, 1.0f));
        const float poff = 12.0f * (float)P.range + P.bend_st + drift + detune;
        const float coff = P.lp_oct + P.bend_oct;
        const float evcf = P.env_inv ? -P.env_vcf : P.env_vcf;
        const float level = clampf(P.level, 0.0f, 4.0f) * fast_exp2(clampf(P.bend_db, -24.0f, 24.0f) * (1.0f / 6.0206f));
        const float vib = P.vco_lfo + P.vib;
        const float vca_lfo = 0.5f * clampf(P.vca_lfo, 0.0f, 1.0f);
        const float pwm = 0.225f * clampf(P.pwm, 0.0f, 1.0f), pw = clampf(P.pw, 0.05f, 0.5f);
        const bool vgate = P.vca_gate;
        const float ca = m.ca, cd = m.cd, cr = m.cr, sus = m.sus, cg = m.cg;
        const float gtarget = gate ? 1.0f : 0.0f;
        const float dgh = (m.ghp1 - m.ghp0) * (1.0f / kBlock);
        const float inv = 1.0f / (float)kQSub;

        // locals
        float ph = p, sgn = sub_sign, h = hs, e = env, gx = gate_x, gh = m.ghp0, amp = amp_prev;
        float cdt = dt, cidt = idt, cG = G;
        int st = stage;
        Ladder4 L = lpf;
        QRng r = rng;

        for (int sb = 0; sb < kQSubs; sb++) {
            const float lfo = m.lfo[sb], sh = m.sh[sb];
            const float frac = (float)(sb + 1) * (1.0f / kQSubs);
            const float pg = pitch0 + frac * (pitch - pitch0);
            const float ndt = note_dt(pg + poff + vib * lfo);
            const float nid = 1.0f / ndt;
            const float oct = coff + P.kybd * (pg - 60.0f) * (1.0f / 12.0f) + evcf * e +
                              P.vcf_lfo * lfo + P.sh_vcf * sh;
            const float nG = q_G(20.0f * fast_exp2(oct));
            if (cdt <= 0.0f) { cdt = ndt; cidt = nid; cG = nG; }
            const float ddt = (ndt - cdt) * inv, did = (nid - cidt) * inv, dG = (nG - cG) * inv;
            const float w = wave == QW_PWM ? 0.5f - pwm * (1.0f + lfo) : pw;
            const float amp1 = level * (1.0f - vca_lfo * (1.0f - lfo));
            const float damp = (amp1 - amp) * inv;

            for (int i = sb * kQSub; i < (sb + 1) * kQSub; i++) {
                cdt += ddt; cidt += did; cG += dG; gh += dgh; amp += damp;
                q_adsr(st, e, ca, cd, cr, sus);
                gx += cg * (gtarget - gx);
                float v = 0.0f;
                switch (wave) {
                case QW_SAW: v = q_saw(ph, cidt); break;
                case QW_SQUARE: v = q_pulse(ph, 0.5f, cidt); break;
                case QW_PULSE:
                case QW_PWM: v = q_pulse(ph, w, cidt); break;
                default: break;
                }
                float x = lw * v;
                if (ls > 0.0f) {
                    // the flip-flop stepped by 2 sgn at the last reset and
                    // steps by -2 sgn at the next one
                    const float ta = ph * cidt, tb = (ph - 1.0f) * cidt;
                    float sq = sgn;
                    if (ta < 2.0f) sq += 2.0f * sgn * q_blep4(ta);
                    if (tb > -2.0f) sq -= 2.0f * sgn * q_blep4(tb);
                    x += ls * sq;
                }
                x += ln * r.uni();  // noise, or the circuit's own floor
                ph += cdt;
                if (ph >= 1.0f) { ph -= 1.0f; sgn = -sgn; }
                // high-pass: x minus the TPT one-pole low-pass
                const float lp1 = gh * (x - h) + h;
                h = 2.0f * lp1 - h;
                const float y = L.process(x - lp1, cG);
                out[i] += y * amp * (vgate ? gx : e);
            }
        }
        p = ph; sub_sign = sgn; hs = h; env = e; gate_x = gx; amp_prev = amp;
        dt = cdt; idt = cidt; G = cG;
        stage = st;
        lpf = L;
        rng = r;
    }
};

}  // namespace freak

#endif
