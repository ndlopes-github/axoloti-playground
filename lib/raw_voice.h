// raw_voice.h: the "raw" patch's voice, a monosynth after the Korg MS-20.
//
// Signal path, as on the MS-20's panel: VCO1 (triangle, saw, pulse with
// width, noise) and VCO2 (saw, square, narrow pulse, ring) and a noise
// source into a mixer, a drive stage (an addition: the MS-20 has none on the
// panel), a resonant high-pass, a resonant low-pass and the VCA. A
// modulation generator (MG: ramp down - triangle - ramp up, or sample and
// hold), EG1 (delay, attack, release) and EG2 (hold, attack, decay,
// sustain, release) modulate pitch and the two cutoffs; EG2 drives the VCA.
// One departure from the MS-20: EG1 has no decay (it stays up while the
// key is held), so on the panel's routing, EG1 -> pitch detunes held notes.
// Here the pitch starts pitch_eg1 off and reaches the note as EG1 rises:
// offset = pitch_eg1 (1 - EG1), a zap (from above) or a scoop (from below)
// whose length is EG1's attack, after EG1's delay.
//
// Filters. Both are 2-pole Sallen-Key filters after the Korg-35 (MS-20
// rev 1), in zero-delay-feedback (trapezoidal) form, with a saturator f in
// the feedback path where the Korg-35 has its diode clipper. Linear model
// (f(y) = y), with L = w/(s+w), H = s/(s+w) the two outputs of a one-pole:
//   LPF  y = L(L x + H k y)  =>  Y/X = w^2 / (s^2 + (2-k) w s + w^2),
//   HPF  y = H(H x + L k y)  =>  Y/X = s^2 / (s^2 + (2-k) w s + w^2),
// so Q = 1/(2-k) and the filters self-oscillate at w from k = 2. The PEAK
// knob sets k in [0, 2.2]. The trapezoidal one-pole with G = g/(1+g),
// g = tan(pi fc/fs), and state s gives lp = G v + (1-G) s for input v.
// Writing z = k f(y) for the fed-back signal, the output is affine in z:
//   LPF  y = A + B z,  A = G^2 x + G(1-G) s1 + (1-G) s2,
//   HPF  y = A + B z,  A = (1-G)((1-G)(x - s1) - s2),
// with B = G(1-G) in both, so each sample solves the scalar equation
//   r(y) = y - k B f(y) - A = 0.
// f is the cubic soft clip (x - x^3/3 on [-1, 1], +-2/3 outside): odd,
// nondecreasing, 0 <= f' <= 1, C^1. With the cutoff clamped below fs/4,
// g < 1, so G < 1/2 and B < 1/4; with k <= 2.2, k B < 0.55. Hence
// r' = 1 - k B f' >= 0.45 > 0: r is strictly increasing, the root exists
// and is unique (proved). It is found by kNewton = 2 Newton steps from the
// root of the secant linearisation at the previous output (Sk35::solve).
// Measured local error at every sample of a full-scale swept saw with k up
// to 2.2: at most 6e-5 (2 steps from the previous output alone: 2e-2).
// The states are then advanced with z = k f(y) by the explicit forward
// pass, so whatever error the solve leaves only perturbs the fed-back
// signal, which |f| <= 2/3 bounds by 1.47. Each one-pole stage with
// G <= 1/2 satisfies |s'| <= max(|s|, sup|v|), so every state stays
// bounded by its stage's input bound: the filters are BIBO-stable for every
// setting, self-oscillation included (proved, for exact arithmetic).
//
// Oversampling. The voice runs at 2 x 48 kHz: oscillators (PolyBLEP saw
// and pulse, PolyBLAMP triangle), drive and filters, where the clipping
// adds harmonics. A linear-phase half-band FIR (Kaiser window, 55 taps)
// then decimates to 48 kHz. Its response is measured in the tests.
//
// Rates. The envelopes run per 48 kHz sample. Pitch and the two cutoffs are
// computed every kSub = 4 samples (12 kHz) and interpolated linearly in
// between (dt and G; a convex combination of values in (0, 1/2) stays in
// it, so the bounds above hold at every sample). Settings come from the bus
// once per block.
//
// Header-only, C++11, no libm, every struct a POD.
#ifndef RAW_VOICE_H
#define RAW_VOICE_H

#include <stdint.h>

#include "freak_dsp.h"

namespace freak {

const int kRawOver = 2;
const float kRawRate = kSampleRate * kRawOver;  // 96 kHz
const int kSub = 4;                            // samples per pitch/cutoff update
const int kNewton = 2;
const float kPeakMax = 2.2f;
const float kRawMaxCut = 22000.0f;             // < 96 kHz / 4, so G < 1/2

enum { W1_TRI, W1_SAW, W1_PULSE, W1_NOISE, NUM_W1 };
enum { W2_SAW, W2_SQUARE, W2_PULSE, W2_RING, NUM_W2 };
const float kNarrowPulse = 0.15f;  // VCO2's narrow pulse width

// What the voice reads; published by raw_ctl.h.
struct RawBus {
    float note;          // MIDI note of the key that sounds (last-note priority)
    bool gate;
    uint32_t trig;       // counts (re)triggers of the envelopes
    float vel;           // 0..1
    float glide;         // s, 99 % of an interval
    float bend;          // semitones, both VCOs (wheel and pads)
    int wave1, wave2;    // W1_*, W2_*
    int oct1, oct2;      // octaves from 8': VCO1 -2..1 (32'..4'), VCO2 -1..2 (16'..2')
    float pw;            // VCO1 pulse width, 0.5 .. 0.05
    float pitch2;        // VCO2 detune, semitones
    float lvl1, lvl2, noise;  // mixer, 0..1
    float drive;         // 0.5 .. 8
    float hp_oct, lp_oct;     // cutoff = 20 Hz * 2^oct
    float hp_k, lp_k;         // feedback, 0 .. kPeakMax
    float hp_mg, lp_mg;       // octaves of MG
    float hp_eg, lp_eg;       // octaves of EG2
    float keytrack;           // 0..1 octave per octave around note 60, both filters
    float pitch_mg;           // semitones of MG
    float pitch_eg1;          // semitones: the pitch starts this far off and reaches
                              // the note as EG1 rises (offset = pitch_eg1 (1 - EG1))
    float mg_hz, mg_shape;    // shape: -1 ramp down, 0 triangle, +1 ramp up
    bool mg_sh;               // MG = sample and hold of noise, clocked at mg_hz
    float sh_lp;              // octaves of an extra S&H on the LPF (S&H pad)
    float e1_d, e1_a, e1_r;   // s
    float e2_h, e2_a, e2_d, e2_s, e2_r;  // s, s, s, level, s
    float velamp;             // 0..1: velocity -> VCA
    float level;              // output gain
};

static RawBus g_rawbus;  // one per patch (see launchkey_ctl.h on g_bus)
inline RawBus &rawbus() { return g_rawbus; }

// PolyBLEP residual of a unit upward step at t = 0, t in samples from the
// step, nonzero on (-1, 1): (1+t)^2/2 before, -(1-t)^2/2 after. Adding
// h R(t) to a naive signal with a step of height h band-limits the step.
inline float blep_r(float t) {
    if (t < 0.0f) { const float u = 1.0f + t; return 0.5f * u * u; }
    const float u = 1.0f - t;
    return -0.5f * u * u;
}
// Its integral (PolyBLAMP): (1 - |t|)^3 / 6 on (-1, 1). Adding d R1(t) to a
// naive signal whose slope (per sample) jumps by d band-limits the corner.
inline float blamp_r(float t) {
    const float u = 1.0f - (t < 0.0f ? -t : t);
    return u * u * u * (1.0f / 6.0f);
}
// The corrections for an edge where the phase p in [0, 1) wraps: within one
// sample of it means p < dt (just after; t = p/dt) or p > 1 - dt (just
// before; t = (p - 1)/dt). The two cases are disjoint because dt <= 0.45
// (note_dt clamps it). An edge at phase e is an edge at 0 of the phase
// p - e (mod 1), see shift().
inline float blep0(float p, float dt, float idt) {
    if (p < dt) return blep_r(p * idt);
    if (p > 1.0f - dt) return blep_r((p - 1.0f) * idt);
    return 0.0f;
}
inline float blamp0(float p, float dt, float idt) {
    if (p < dt) return blamp_r(p * idt);
    if (p > 1.0f - dt) return blamp_r((p - 1.0f) * idt);
    return 0.0f;
}
inline float shift(float p, float e) {  // p - e mod 1, for p, e in [0, 1)
    const float q = p - e;
    return q < 0.0f ? q + 1.0f : q;
}
// Band-limited saw 2p - 1 (falls by 2 at p = 0).
inline float raw_saw(float p, float dt, float idt) {
    return 2.0f * p - 1.0f - 2.0f * blep0(p, dt, idt);
}
// Band-limited pulse of width w, as saw(p) - saw(p - w): 2w - 2 on [0, w)
// and 2w on [w, 1), so zero mean for every w (as groove_synth.h).
inline float raw_pulse(float p, float w, float dt, float idt) {
    return raw_saw(p, dt, idt) - raw_saw(shift(p, w), dt, idt);
}
// Band-limited triangle: 1 at p = 0, -1 at p = 1/2, slope -+4 per cycle.
// The slope per sample jumps by +8 dt at p = 1/2 and by -8 dt at p = 0.
inline float raw_tri(float p, float dt, float idt) {
    const float y = 4.0f * (p < 0.5f ? 0.5f - p : p - 0.5f) - 1.0f;
    return y + 8.0f * dt * (blamp0(shift(p, 0.5f), dt, idt) - blamp0(p, dt, idt));
}

// Cubic soft clip and its derivative (see the header comment). (A
// branch-free form through clamp(x, -1, 1) measured 2 % slower per filter
// on the board: the divisions in the solve dominate, not the branches.)
inline float clip3(float x) {
    if (x >= 1.0f) return 2.0f / 3.0f;
    if (x <= -1.0f) return -2.0f / 3.0f;
    return x - x * x * x * (1.0f / 3.0f);
}
inline float clip3d(float x) {
    if (x >= 1.0f || x <= -1.0f) return 0.0f;
    return 1.0f - x * x;
}

// Sallen-Key 2-pole after the Korg-35, low- or high-pass (see the header).
struct Sk35 {
    float s1, s2, y;  // stage states, last output (Newton's start)

    void init() { s1 = s2 = y = 0.0f; }

    // Solve y = A + kB f(y) (r strictly increasing, r' >= 1 - kB). Start: the
    // root of y = A + kB sigma y, where sigma = f(yp)/yp in (0, 1] is the
    // slope of the secant through the origin at the previous output yp; the
    // divisor 1 - kB sigma >= 0.45. Then kNewton Newton steps.
    static float solve(float yp, float A, float kB) {
        const float a = yp < 0.0f ? -yp : yp;
        const float sigma = a <= 1.0f ? 1.0f - yp * yp * (1.0f / 3.0f) : (2.0f / 3.0f) / a;
        float y = A / (1.0f - kB * sigma);
        for (int i = 0; i < kNewton; i++) {
            const float r = y - kB * clip3(y) - A;
            y -= r / (1.0f - kB * clip3d(y));
        }
        return y;
    }

    // G in (0, 1/2), k in [0, kPeakMax].
    float lp(float x, float G, float k) {
        const float H = 1.0f - G;
        const float A = G * (G * x + H * s1) + H * s2;
        const float ys = solve(y, A, k * G * H);
        const float z = k * clip3(ys);
        const float l1 = G * (x - z) + H * s1;   // stage 1: x - z, referred to z
        s1 = 2.0f * l1 - s1;
        const float y1 = z + l1;
        const float out = G * y1 + H * s2;       // stage 2
        s2 = 2.0f * out - s2;
        y = out;
        return out;
    }

    float hp(float x, float G, float k) {
        const float H = 1.0f - G;
        const float A = H * (H * (x - s1) - s2);
        const float ys = solve(y, A, k * G * H);
        const float z = k * clip3(ys);
        const float l1 = G * (x - z) + H * s1;   // stage 1 low-pass of x - z
        s1 = 2.0f * l1 - s1;
        const float y1 = x - l1;                 // = H(x) + L(z)
        const float l2 = G * y1 + H * s2;        // stage 2
        s2 = 2.0f * l2 - s2;
        const float out = y1 - l2;
        y = out;
        return out;
    }
};

// One-pole TPT coefficient G = g/(1+g), g = tan x, x = pi fc/fs, for a
// cutoff in Hz at the oversampled rate. With fc <= kRawMaxCut < fs/4,
// x is in (0, pi/4), so no range reduction is needed, and
// G = sin x / (sin x + cos x): one division. Taylor polynomials, degree 7
// for sin and 8 for cos; on [0, pi/4] the remainders are below
// (pi/4)^9/9! < 3.1e-7 and (pi/4)^10/10! < 2.5e-8, and sin + cos >= 1
// there, so G is within 4e-7 (proved; float rounding adds ~1e-7).
inline float raw_G(float fc) {
    fc = clampf(fc, 5.0f, kRawMaxCut);
    const float x = fc * (kPi / kRawRate), x2 = x * x;
    const float s = x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040))));
    const float c = 1.0f + x2 * (-0.5f + x2 * (1.0f / 24 + x2 * (-1.0f / 720 + x2 * (1.0f / 40320))));
    return s / (s + c);
}

// Half-band decimator, 96 -> 48 kHz: linear-phase FIR, 2M+1 taps with
// M = 27 odd, h[M] = 1/2, and h[M +- j] = 0 for even j != 0 (half-band), so
// 14 coefficient pairs. h[M +- j] = sin(pi j/2)/(pi j) w(j), with w a Kaiser
// window of beta 7 (about 70 dB of stop-band attenuation, by Kaiser's
// formula; the tests measure it). Pass band to 20 kHz, stop band from
// 28 kHz: what aliases into 0..20 kHz must come from above 76 kHz.
const int kHbM = 27;
const int kHbPairs = (kHbM + 1) / 2;  // odd j = 1, 3, .., 27
const int kHbLen = 2 * kHbM + 1;
struct HalfbandCoefs {
    float c[kHbPairs];  // c[i] for j = 2i + 1
};
// I0 by its power series sum (x^2/4)^n / (n!)^2, in float (no soft double
// code on the board). For x <= 7 the terms are below 1e-9 of the sum
// after n = 30, so the error is float rounding (~1e-6 relative).
inline float raw_i0(float x) {
    float s = 1.0f, t = 1.0f;
    for (int n = 1; n < 30; n++) {
        t *= (x * x * 0.25f) / ((float)n * (float)n);
        s += t;
    }
    return s;
}
inline void raw_halfband_design(HalfbandCoefs &hc) {
    const float beta = 7.0f;
    const float i0b = raw_i0(beta);
    for (int i = 0; i < kHbPairs; i++) {
        const int j = 2 * i + 1;
        const float r = (float)j / (float)kHbM;
        const float w = raw_i0(beta * fast_sqrt(1.0f - r * r)) / i0b;
        const float sinc = ((i & 1) ? -1.0f : 1.0f) / (kPi * (float)j);  // sin(pi j/2) = (-1)^i
        hc.c[i] = sinc * w;
    }
}

struct Halfband {
    float x[2 * kHbLen];  // history, written twice (circular, read contiguously)
    int w;

    void init() {
        for (int i = 0; i < 2 * kHbLen; i++) x[i] = 0.0f;
        w = 0;
    }
    // Push two input samples (a then b), return one output sample.
    float process(const HalfbandCoefs &hc, float a, float b) {
        x[w] = x[w + kHbLen] = a;
        if (++w == kHbLen) w = 0;
        x[w] = x[w + kHbLen] = b;
        if (++w == kHbLen) w = 0;
        // x[w .. w + kHbLen - 1] is the history, oldest first; the centre
        // tap is at offset kHbM.
        const float *h = x + w;
        float acc = 0.5f * h[kHbM];
        for (int i = 0; i < kHbPairs; i++) {
            const int j = 2 * i + 1;
            acc += hc.c[i] * (h[kHbM - j] + h[kHbM + j]);
        }
        return acc;
    }
};

// Envelopes, per 48 kHz sample, exponential (RC-like) segments as
// freak_dsp.h's Env: the attack aims at 1.2 and ends at 1 (exactly the
// attack time from 0), decay and release cover 99 % of their way.
// EG1 (MS-20): delay, attack, release. EG2: hold, attack, decay, sustain,
// release, where hold keeps the gate on for the hold time after the key is
// released.
struct RawEnv {
    enum { IDLE, DELAY, ATTACK, DECAY, SUSTAIN, RELEASE };
    float x;
    int stage;
    uint32_t n;  // samples spent in DELAY, or since the key went up (hold)
    bool gate;

    void init() { x = 0.0f; stage = IDLE; n = 0; gate = false; }
    void trigger(bool with_delay) { stage = with_delay ? DELAY : ATTACK; n = 0; }
};

struct EnvCoefs {
    float ca, cd, cr;
    uint32_t nd, nh;  // delay, hold in samples
    float s;
};

inline void env_coefs(EnvCoefs &c, float d_or_h, float a, float dcy, float s, float r) {
    c.ca = one_pole_coef(a, kSampleRate, kLog2_6);
    c.cd = one_pole_coef(dcy, kSampleRate, kLog2_100);
    c.cr = one_pole_coef(r, kSampleRate, kLog2_100);
    c.nd = c.nh = (uint32_t)(clampf(d_or_h, 0.0f, 20.0f) * kSampleRate);
    c.s = clampf(s, 0.0f, 1.0f);
}

// EG1: delay -> attack -> held at 1 while the gate is on -> release.
inline float eg1_tick(RawEnv &e, const EnvCoefs &c, bool g) {
    if (!g && e.gate && e.stage != RawEnv::IDLE) e.stage = RawEnv::RELEASE;
    e.gate = g;
    switch (e.stage) {
    case RawEnv::DELAY:
        if (++e.n >= c.nd) e.stage = RawEnv::ATTACK;
        break;
    case RawEnv::ATTACK:
        e.x += c.ca * (1.2f - e.x);
        if (e.x >= 1.0f) { e.x = 1.0f; e.stage = RawEnv::SUSTAIN; }
        break;
    case RawEnv::SUSTAIN:
        e.x = 1.0f;
        break;
    case RawEnv::RELEASE:
        e.x -= c.cr * e.x;
        if (e.x < 1e-5f) { e.x = 0.0f; e.stage = RawEnv::IDLE; }
        break;
    default:
        e.x = 0.0f;
    }
    return e.x;
}

// EG2: attack -> decay -> sustain while the gate is on; after the key goes
// up the gate stays on for the hold time, then release.
inline float eg2_tick(RawEnv &e, const EnvCoefs &c, bool key) {
    bool g = key;
    if (key) e.n = 0;
    else if (e.n < c.nh) { e.n++; g = true; }
    if (!g && e.gate && e.stage != RawEnv::IDLE) e.stage = RawEnv::RELEASE;
    e.gate = g;
    switch (e.stage) {
    case RawEnv::ATTACK:
        e.x += c.ca * (1.2f - e.x);
        if (e.x >= 1.0f) { e.x = 1.0f; e.stage = RawEnv::DECAY; }
        break;
    case RawEnv::DECAY:
        e.x += c.cd * (c.s - e.x);
        if (absf(e.x - c.s) < 1e-4f) { e.x = c.s; e.stage = RawEnv::SUSTAIN; }
        break;
    case RawEnv::SUSTAIN:
        e.x = c.s;  // follows the sustain fader
        break;
    case RawEnv::RELEASE:
        e.x -= c.cr * e.x;
        if (e.x < 1e-5f) { e.x = 0.0f; e.stage = RawEnv::IDLE; }
        break;
    default:
        e.x = 0.0f;
    }
    return e.x;
}

struct RawRng {
    uint32_t s;
    float uni() {  // [-1, 1)
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(s >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

struct RawVoice {
    HalfbandCoefs hc;
    Halfband dec;
    Sk35 hpf, lpf;
    RawEnv eg1, eg2;
    RawRng rng, rng_sh;
    float p1, p2;          // oscillator phases
    float pitch;           // glided note, semitones
    bool primed;
    uint32_t trig_seen;
    float mg_phase, mg_held;  // MG phase; the S&H value (new each MG cycle)
    float dt1, dt2, ghp, glp;  // values at the end of the last sub-block
    float id1, id2;            // 1/dt1, 1/dt2 there (interpolated in between)
    float dc_x, dc_y;      // DC blocker
    float gain_prev;

    void init() {
        raw_halfband_design(hc);
        dec.init();
        hpf.init();
        lpf.init();
        eg1.init();
        eg2.init();
        rng.s = 0x9E3779B9u;
        rng_sh.s = 0x85EBCA6Bu;
        p1 = p2 = 0.0f;
        pitch = 60.0f;
        primed = false;
        trig_seen = 0;
        mg_phase = 0.0f;
        mg_held = 0.0f;
        dt1 = dt2 = 0.0f;
        id1 = id2 = 1.0f;
        ghp = raw_G(20.0f);
        glp = raw_G(20000.0f);
        dc_x = dc_y = 0.0f;
        gain_prev = 0.0f;
    }

    // MG value in [-1, 1] at phase p: rises over [0, a), falls over [a, 1),
    // a = (shape + 1)/2: a = 0 ramp down, 1/2 triangle, 1 ramp up.
    static float mg_wave(float p, float shape) {
        const float a = clampf(0.5f * (shape + 1.0f), 0.0f, 1.0f);
        if (p < a) return -1.0f + 2.0f * p / a;          // p < a implies a > 0
        return 1.0f - 2.0f * (p - a) / (1.0f - a);       // p >= a, and p < 1, so a < 1
    }

    static float note_dt(float note) {
        const float f = 440.0f * fast_exp2((note - 69.0f) * (1.0f / 12.0f));
        return clampf(f / kRawRate, 1e-6f, 0.45f);
    }

    // One block of kBlock samples at 48 kHz into out.
    void render(const RawBus &b, float *out) {
        // triggers and the glide (per block)
        if (b.trig != trig_seen) {
            trig_seen = b.trig;
            eg1.trigger(b.e1_d > 0.0f);
            eg2.trigger(false);
        }
        const float target = b.note;
        if (!primed || b.glide <= 0.0f) {
            pitch = target;
            primed = true;
        }
        const float pitch0 = pitch;
        if (b.glide > 0.0f) pitch += one_pole_coef(b.glide, kControlRate, kLog2_100) * (target - pitch);
        EnvCoefs c1, c2;
        env_coefs(c1, b.e1_d, b.e1_a, 0.0f, 1.0f, b.e1_r);
        env_coefs(c2, b.e2_h, b.e2_a, b.e2_d, b.e2_s, b.e2_r);
        const float w = clampf(b.pw, 0.02f, 0.5f);
        const float l1 = 0.5f * b.lvl1, l2 = 0.5f * b.lvl2, ln = 0.5f * b.noise;
        const float drv = clampf(b.drive, 0.1f, 16.0f);
        const float idrv = 1.0f / clip3(drv);  // so |x| <= 1 maps onto |y| <= 1, peaks kept
        const float kt = b.keytrack * (b.note - 60.0f) * (1.0f / 12.0f);
        const float khp = clampf(b.hp_k, 0.0f, kPeakMax), klp = clampf(b.lp_k, 0.0f, kPeakMax);
        const float gain = clampf(b.level, 0.0f, 4.0f) * (1.0f - b.velamp * (1.0f - b.vel));
        const float gstep = (gain - gain_prev) * (1.0f / kBlock);
        float gg = gain_prev;
        gain_prev = gain;
        const float dc_r = 1.0f - 2.0f * kPi * 5.0f / kSampleRate;  // DC blocker at 5 Hz

        for (int sb = 0; sb < kBlock; sb += kSub) {
            // MG and S&H, advanced by one sub-block
            const float dmg = b.mg_hz * (float)kSub / kSampleRate;
            mg_phase += dmg;
            if (mg_phase >= 1.0f) { mg_phase -= (float)(int32_t)mg_phase; mg_held = rng_sh.uni(); }
            const float mg = b.mg_sh ? mg_held : mg_wave(mg_phase, b.mg_shape);
            const float sh = mg_held;
            // pitch and cutoffs at the end of this sub-block, from the
            // envelopes' current values
            const float frac = (float)(sb + kSub) / (float)kBlock;
            const float pn = pitch0 + frac * (pitch - pitch0) + b.bend + b.pitch_mg * mg + b.pitch_eg1 * (1.0f - eg1.x);
            const float ndt1 = note_dt(pn + 12.0f * (float)b.oct1);
            const float ndt2 = note_dt(pn + 12.0f * (float)b.oct2 + b.pitch2);
            const float e2 = eg2.x;
            const float nghp = raw_G(20.0f * fast_exp2(b.hp_oct + b.hp_mg * mg + b.hp_eg * e2 + kt));
            const float nglp = raw_G(20.0f * fast_exp2(b.lp_oct + b.lp_mg * mg + b.lp_eg * e2 + kt + b.sh_lp * sh));
            const float nid1 = 1.0f / ndt1, nid2 = 1.0f / ndt2;
            if (dt1 <= 0.0f) { dt1 = ndt1; dt2 = ndt2; ghp = nghp; glp = nglp; id1 = nid1; id2 = nid2; }
            const float inv = 1.0f / (float)(kSub * kRawOver);
            const float ddt1 = (ndt1 - dt1) * inv, ddt2 = (ndt2 - dt2) * inv;
            // 1/dt interpolated too: for dt moving from d0 to d1 the error of the
            // chord of the convex 1/dt is at most (d1 - d0)^2 / (4 d0 d1) relative
            const float did1 = (nid1 - id1) * inv, did2 = (nid2 - id2) * inv;
            const float dghp = (nghp - ghp) * inv, dglp = (nglp - glp) * inv;

            for (int i = sb; i < sb + kSub; i++) {
                eg1_tick(eg1, c1, b.gate);
                const float env = eg2_tick(eg2, c2, b.gate);
                float y2[kRawOver];
                for (int o = 0; o < kRawOver; o++) {
                    dt1 += ddt1; dt2 += ddt2; ghp += dghp; glp += dglp;
                    id1 += did1; id2 += did2;
                    // an oscillator at level 0 is not computed (the ring reads
                    // VCO1's phase, not its output)
                    float v1 = 0.0f;
                    if (l1 > 0.0f) {
                        switch (b.wave1) {
                        case W1_TRI: v1 = raw_tri(p1, dt1, id1); break;
                        case W1_SAW: v1 = raw_saw(p1, dt1, id1); break;
                        case W1_PULSE: v1 = raw_pulse(p1, w, dt1, id1); break;
                        default: v1 = rng.uni(); break;
                        }
                    }
                    float v2 = 0.0f;
                    if (l2 > 0.0f) {
                        switch (b.wave2) {
                        case W2_SAW: v2 = raw_saw(p2, dt2, id2); break;
                        case W2_SQUARE: v2 = raw_pulse(p2, 0.5f, dt2, id2); break;
                        case W2_PULSE: v2 = raw_pulse(p2, kNarrowPulse, dt2, id2); break;
                        default:  // ring: the product of the two band-limited squares
                            v2 = raw_pulse(p1, 0.5f, dt1, id1) * raw_pulse(p2, 0.5f, dt2, id2);
                            break;
                        }
                    }
                    p1 += dt1;
                    if (p1 >= 1.0f) p1 -= 1.0f;
                    p2 += dt2;
                    if (p2 >= 1.0f) p2 -= 1.0f;
                    float x = l1 * v1 + l2 * v2;
                    if (ln > 0.0f) x += ln * rng.uni();
                    x = clip3(drv * x) * idrv;
                    x = hpf.hp(x, ghp, khp);
                    x = lpf.lp(x, glp, klp);
                    y2[o] = x;
                }
                float y = dec.process(hc, y2[0], y2[1]);
                gg += gstep;
                y *= gg * env;
                // DC blocker: y - y[-1] + R out[-1]
                const float o = y - dc_x + dc_r * dc_y;
                dc_x = y;
                dc_y = o;
                out[i] = o;
            }
        }
    }
};

}  // namespace freak

#endif
