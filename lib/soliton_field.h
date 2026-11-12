// soliton_field.h: the oscillator of the soliton patch. Each voice holds a
// periodic field on y in [0, 2 pi) that evolves under a nonlinear wave
// equation; one period in y is one cycle of the waveform, played at the
// note's pitch. Pitch is therefore independent of the equation (exact tuning,
// free key tracking), and the equation's time t moves the timbre.
//
// Engines (u real, psi complex; s = 2 pi / L rescales a domain of length L
// onto [0, 2 pi); K = 42 retained modes):
//   SHOCK    viscous Burgers   u_t + u u_y = nu u_yy + F sin y
//   SOLITON  KdV               u_t + u u_y + delta^2 u_yyy = 0
//   CHAOS    Kuramoto-Sivashinsky (with Kawahara dispersion beta)
//            u_t + s u u_y + s^2 u_yy + beta s^3 u_yyy + s^4 u_yyyy = 0
//   ROGUE    focusing NLS      i psi_t + s^2 psi_yy + 2 |psi|^2 psi = 0,
//            heard as Re(psi e^{i m y}) (a wave group on a carrier m)
// Every engine also takes a linear damping -gamma u (DAMP). SHOCK and
// SOLITON add a hyperviscosity -mu (k/K)^16 u_k that acts on the top modes
// only (see kShockMu).
//
// Numerics (review points):
//  - Fourier pseudo-spectral. Real engines: 128 points, modes |k| <= 42.
//    3 * 42 < 128, so the 2/3 rule removes the aliasing of the quadratic term
//    exactly: the scheme is the Fourier-Galerkin truncation, which for
//    SOLITON conserves sum |u_k|^2 exactly in continuous time (the
//    nonlinear term is orthogonal to u: int u (u^2/2)_y = 0), apart from
//    the hyperviscosity.
//  - Real engines use Strang splitting: half a step of the linear flow,
//    exactly in Fourier space (nu k^2, delta^2 k^3, k^4, the
//    hyperviscosity), a classical RK4 step of the nonlinear flow
//    u_t = -s (u^2/2)_y (+ F sin y), and another half linear step. The step
//    is then limited only by the nonlinear term: |h| s K max|u| <= kCfl.
//    (Integrating-factor RK4 was tried first: for KdV at delta = 0.5 it
//    is unstable, because RK4 then samples the fast dispersive phases of
//    the top modes too coarsely.) For KdV this splitting converges
//    (Holden, Karlsen, Risebro, Tao 2011).
//  - ROGUE: Strang splitting on 128 points: half a linear step (exact, in
//    Fourier space), the nonlinear rotation psi *= (1 + i q)/(1 - i q),
//    q = h |psi|^2 (Cayley: modulus exactly 1, angle 2 atan q = 2 q + O(q^3),
//    second order like the splitting), another half linear step, then
//    truncation to |k| <= kr = kRogueKappa / s. Each substep is unitary and
//    the truncation is a projection, so sum |psi_k|^2 cannot grow
//    (gamma = 0). The step also keeps the top mode's linear phase per
//    substep below kRoguePhase < pi: past pi the split-step method has a
//    known resonance instability (Weideman and Herbst 1986), seen here as
//    a 75 % mass loss before the bound was added.
//  - One step is cut into 12 work items (ITEM_*), one per turn of the
//    voice: the 4 RK stages, two items each (or for ROGUE 2 Strang
//    substeps of h/2, four items each), the step boundary, and the
//    playback table with its 256-point inverse FFT in pieces. Each item is
//    at most about 8,000 cycles on the board, so the work per tick is
//    bounded (README).
//  - Playback: a 256-point table (twice the grid), built by an inverse FFT
//    from the modes with j f0 below 16 kHz, fading to zero at 20 kHz, so the
//    table is band-limited for its pitch. Linear interpolation, and a
//    crossfade from the previous table over one step, so the waveform
//    never jumps.
//
// Every struct is a POD (the voices live in a polyphonic subpatch).
#ifndef SOLITON_FIELD_H
#define SOLITON_FIELD_H

#include <stdint.h>

#include "freak_dsp.h"
#include "pde_fft.h"

namespace freak {

enum { ENG_SHOCK, ENG_SOLITON, ENG_CHAOS, ENG_ROGUE, NUM_ENGINES };

const int kGrid = 128;              // grid points (real engines and ROGUE)
const int kModes = 42;              // retained |k|, 3 * 42 < 128 (2/3 rule)
const int kHalf = kGrid / 2 + 1;    // stored modes of a real field, k = 0..64
const int kTableLen = 256;          // playback table
const int kMaxHarm = 42 + 5;        // highest harmonic in a table: kModes, or kr + carrier (ROGUE)

const float kCfl = 1.25f;           // |h| s K max|u|: RK4 is stable to 2.83 on the imaginary axis, but its damping ~ (h k u)^6 cost KdV 30 % of its energy over a long note at 2, 2.6 % at 1.25 (tests)
const float kRogueTheta = 0.5f;     // max nonlinear rotation per substep, rad
const float kRogueKappa = 14.0f;    // ROGUE keeps wavenumbers k s <= 14 (breather tails ~ e^{-ks/2a})
const float kRoguePhase = 2.5f;     // max linear phase of the top mode per substep, rad: below pi, where the split-step method resonates (scan in the tests: stable at 2 and 2.5)
// Hyperviscosity -mu (k/K)^16, by engine: it acts on the top modes only
// (at k = 0.6 K it is 3e-4 mu). SHOCK needs it to hold the shock on the
// grid; SOLITON against the slow pile-up of energy at the truncation
// (the truncated KdV is not integrable and thermalises). Values from the
// resolution scan in tests/test_soliton.cpp.
const float kShockMu = 10000.0f;
const float kSolitonMu = 100.0f;
const float kOutRms = 0.3f;         // table RMS after normalisation
const float kBandPass = 16000.0f;   // harmonics below: full level
const float kBandStop = 20000.0f;   // harmonics above: removed

// e^x, accurate where |x| < 0.5 (Taylor to degree 6: error < 0.5^7/7! e^0.5
// < 3e-6), else fast_exp2 (1.6e-4 relative). For -0.5 < x < 0 the result is
// below 1, so a damped mode can never be amplified by rounding the factor.
inline float exp_acc(float x) {
    if (x > -0.5f && x < 0.5f)
        return 1.0f + x * (1.0f + x * (0.5f + x * (1.0f / 6 + x * (1.0f / 24 + x * (1.0f / 120 + x * (1.0f / 720))))));
    return fast_exp2(1.44269504f * x);
}

// Knob (0..1) to physical parameters. Kept here so the tests can sweep them.
// SHOCK: viscosity 0.3 .. 0.0047 (exponential), drive F = 2 c^2.
inline float shock_nu(float t) { return 0.3f * fast_exp2(-6.0f * clampf(t, 0.0f, 1.0f)); }
inline float shock_drive(float c) { c = clampf(c, 0.0f, 1.0f); return 2.0f * c * c; }
// SOLITON: delta from 0.5 (dispersive: one or two solitons) down to the
// smallest width the 42 modes resolve at amplitude A: soliton width ~
// delta / sqrt(A), so delta_min = 0.1 sqrt(A) (from the scan in
// tests/test_soliton.cpp: energy in the top modes stays below 1e-5).
inline float kdv_delta_min(float A) { return 0.1f * __builtin_sqrtf(A > 0.0f ? A : 0.0f); }
// 0.5 * 0.2^t (log2 0.2 = -2.3219), geometric from 0.5 to 0.1, then held
// at the resolution limit for louder notes.
inline float kdv_delta(float t, float A) {
    const float d = 0.5f * fast_exp2(-2.32192809f * clampf(t, 0.0f, 1.0f));
    const float lo = kdv_delta_min(A);
    return d > lo ? d : lo;
}
// CHAOS: L / 2 pi = 1.1 .. 12, the number of linearly unstable modes
// (k s < 1); beta = 4 c^2.
inline float ks_s(float t) { return 1.0f / (1.1f + 10.9f * clampf(t, 0.0f, 1.0f)); }
inline float ks_beta(float c) { c = clampf(c, 0.0f, 1.0f); return 4.0f * c * c; }
// ROGUE: s = 1.9 / (1 + 3 t). Modes with k s < 2a are modulationally
// unstable: at a = 1 that is 1 mode at t = 0 and 4 at t = 1. Carrier 1..5.
inline float rogue_s(float t) { return 1.9f / (1.0f + 3.0f * clampf(t, 0.0f, 1.0f)); }
inline int rogue_carrier(float c) { return 1 + (int)(clampf(c, 0.0f, 1.0f) * 4.99f); }
// Amplitude of the initial field from AMOUNT (0..1).
inline float engine_amp(int engine, float amt) {
    amt = clampf(amt, 0.0f, 1.0f);
    switch (engine) {
    case ENG_SHOCK: return 0.25f * fast_exp2(3.0f * amt);    // 0.25 .. 2
    case ENG_SOLITON: return 0.25f * fast_exp2(2.3f * amt);  // 0.25 .. 1.23
    case ENG_CHAOS: return 0.1f * fast_exp2(4.0f * amt);     // 0.1 .. 1.6 (seed only)
    default: return 0.35f * fast_exp2(amt);                  // 0.35 .. 0.7 (x velocity, at most 1)
    }
}
inline float engine_amp_max(int engine) {
    // ROGUE: above a = 1 the breathers need wavenumbers beyond kRogueKappa
    // (the scan in tests/test_soliton.cpp loses 6 % of the mass at a = 1.2).
    return engine == ENG_SHOCK ? 3.0f : (engine == ENG_SOLITON ? 2.0f : (engine == ENG_CHAOS ? 2.0f : 1.0f));
}

// What the voice sets each tick.
struct FieldCtl {
    int engine;
    float timbre, color;  // 0..1, modulation included
    float damp;           // gamma >= 0, per time unit
    float rate;           // time units per second; < 0 runs backwards
    float f0;             // Hz, for the band limit
};

// Work items of a step, one per control tick, each at most about 8,000
// cycles (measured on the board, README): items 0-7 the four RK stages, two
// items each (inverse FFT and square / forward FFT and update) or, for
// ROGUE, two Strang substeps of four items; 8 the step boundary (kick, new
// settings, the first half of the linear-flow factors); 9 the rest of the
// factors and the table's spectrum; 10-11 the table's 256-point inverse
// FFT, in pieces.
enum { ITEM_BOUNDARY = 8, ITEM_SPECTRUM = 9, kItemsPerStep = 12 };

struct Field {
    Cplx v[kGrid];        // state: real engines k = 0..K (v_-k = conj v_k); ROGUE all k, FFT order
    Cplx acc[kHalf], in[kHalf], r[kHalf];  // RK4 accumulator, stage input, stage output
    Cplx E[kModes + 1];   // half a step (ROGUE: half a substep) of the linear flow, by |k|
    Cplx z[kGrid];        // FFT scratch (n/2 for the real transforms), ROGUE grid
    Cplx X[kTableLen / 2 + 1];             // table spectrum
    float grid[kGrid];
    float tab[3][kTableLen + 1];           // guard sample at the end
    int tprev, tcur, tnext;
    float alpha, dalpha, phase;
    // step state, latched at the step boundary
    int engine;           // engine of the current step
    int item;             // next work item, 0..kItemsPerStep-1
    float h;              // time step of the current step (0: frozen)
    float umax, umax_last;  // max |u| (ROGUE: max |psi|^2) over the step's stages
    float nu, delta, s, beta, F, gamma;
    int carrier;
    int kr;               // ROGUE: retained |k| (kRogueKappa / s, at most kModes)
    float amp;            // amplitude of the note (A, or a for ROGUE)
    float ref0, rms_s, rms_c;  // loudness reference (SHOCK), smoothed RMS, its coefficient
    float kick;           // pending kick amplitude (0 = none), applied at the next step boundary
    float kick_pos;       // y of the kick
    float t_step;         // seconds per step
    uint32_t steps;       // steps taken since the note began (tests, logs)

    // ticks_per_item: the voice runs one work item every that many ticks.
    void init(int ticks_per_item) {
        fft_init();
        for (int k = 0; k < kGrid; k++) v[k].re = v[k].im = 0.0f;
        for (int t = 0; t < 3; t++)
            for (int i = 0; i <= kTableLen; i++) tab[t][i] = 0.0f;
        for (int j = 0; j <= kTableLen / 2; j++) X[j].re = X[j].im = 0.0f;
        tprev = 0; tcur = 1; tnext = 2;
        const float samples_per_step = (float)(kItemsPerStep * ticks_per_item * kBlock);
        dalpha = 1.0f / samples_per_step;
        alpha = 1.0f;
        phase = 0.0f;
        t_step = samples_per_step / kSampleRate;
        rms_c = one_pole_coef(0.03f, kSampleRate / samples_per_step, 1.44269504f);  // tau = 30 ms
        engine = ENG_SOLITON;
        item = ITEM_BOUNDARY;
        h = 0.0f;
        umax = umax_last = 1.0f;
        nu = 0.1f; delta = 0.2f; s = 1.0f; beta = 0.0f; F = 0.0f; gamma = 0.0f;
        carrier = 1;
        kr = kModes;
        amp = 1.0f;
        ref0 = rms_s = 0.1f;
        kick = 0.0f; kick_pos = 0.0f;
        steps = 0;
        for (int k = 0; k <= kModes; k++) { E[k].re = 1.0f; E[k].im = 0.0f; }
    }

    static bool is_real(int e) { return e != ENG_ROGUE; }
    static bool reversible(int e) { return e == ENG_SOLITON || e == ENG_ROGUE; }

    // ---------------------------------------------------------------- note on
    // A: amplitude; shape (SOLITON only): 0 cosine .. 1 localised bump;
    // seed: 0..1 size of the perturbation in modes 2..4 (ROGUE: of mode 1);
    // ph[3]: phases of the perturbation (SPICE makes them random);
    // fresh: the voice was silent, so the old tables are cleared (else the
    // new sound fades in from the old one).
    // The first table is ready three ticks later (items 8-10), and the field
    // starts moving after the step boundary (item 11): no tick does more
    // than one item's work, even when a chord strikes three voices at once.
    void note_on(const FieldCtl &c, float A, float shape, float seed, const float *ph, bool fresh) {
        engine = c.engine;
        amp = A;
        for (int k = 0; k < kGrid; k++) v[k].re = v[k].im = 0.0f;
        float cs[3], sn[3];
        for (int i = 0; i < 3; i++) sincos_unit(ph[i], cs[i], sn[i]);
        if (engine == ENG_ROGUE) {
            // psi = a (1 + eps cos(y + ph0)) plus a little of mode 2
            const float eps = 1e-3f + 0.2f * seed * seed;
            v[0].re = A;
            v[1].re = 0.5f * A * eps * cs[0];
            v[1].im = 0.5f * A * eps * sn[0];
            v[kGrid - 1].re = v[1].re;
            v[kGrid - 1].im = -v[1].im;
            v[2].re = 0.1f * A * eps * cs[1];
            v[2].im = 0.1f * A * eps * sn[1];
        } else {
            if (engine == ENG_SHOCK) {
                v[1].im = -0.5f * A;  // A sin y
            } else if (engine == ENG_SOLITON) {
                v[1].re = 0.5f * A * (1.0f - shape);  // A (1 - shape) cos y
                if (shape > 0.0f) {
                    // A shape * bump: a Gaussian of width 0.4 rad at y = pi,
                    // mean removed, scaled to the RMS of A cos y.
                    // g_k = exp(-(0.4 k)^2/2) (-1)^k (e^{ik pi}), by the
                    // recurrence g_k / g_{k-1} = -exp(-0.08 (2k - 1)).
                    float e2 = 0.0f, g = 1.0f, step = exp_acc(-0.08f);
                    const float q = step * step;  // exp(-0.16)
                    for (int k = 1; k <= kModes; k++) {
                        g *= -step;
                        step *= q;
                        X[k].re = g;  // X as scratch: the table spectrum is rebuilt at item 8
                        e2 += 2.0f * g * g;
                    }
                    const float sc = A * shape * 0.70710678f / __builtin_sqrtf(e2);
                    for (int k = 1; k <= kModes; k++) v[k].re += sc * X[k].re;
                }
            } else {
                v[1].re = 0.5f * A;  // A cos y
            }
            static const float w[3] = {0.25f, 0.15f, 0.1f};
            for (int i = 0; i < 3; i++) {
                v[2 + i].re += A * seed * w[i] * cs[i];
                v[2 + i].im += A * seed * w[i] * sn[i];
            }
        }
        steps = 0;
        kick = 0.0f;
        umax_last = 2.0f * A;
        umax = 0.0f;
        h = 0.0f;
        rms_s = __builtin_sqrtf(energy());
        ref0 = rms_s;
        params(c);
        if (fresh) {
            for (int i = 0; i <= kTableLen; i++) tab[tcur][i] = tab[tprev][i] = 0.0f;
        }
        item = ITEM_BOUNDARY;
    }

    // Mean square of the field with its mean removed (Parseval); for ROGUE
    // of Re(psi e^{imy}).
    float energy() {
        float e = 0.0f;
        if (engine == ENG_ROGUE) {
            // <|Re(psi e^{imy})|^2> = <|psi|^2>/2 + Re <psi^2 e^{2imy}>/2.
            // The second term is not conserved but small; use the first.
            for (int k = 0; k < kGrid; k++) e += v[k].re * v[k].re + v[k].im * v[k].im;
            return 0.5f * e;
        }
        for (int k = 1; k <= kModes; k++) e += v[k].re * v[k].re + v[k].im * v[k].im;
        return 2.0f * e;
    }

    // Kick: add a localised bump at a random position (the voice draws it).
    void add_kick(float a, float pos) { kick = a; kick_pos = pos; }

    // ------------------------------------------------------- step boundary
    // Engine parameters from the knobs (cheap; also at note on).
    void params(const FieldCtl &c) {
        gamma = c.damp;
        switch (engine) {
        case ENG_SHOCK: nu = shock_nu(c.timbre); F = shock_drive(c.color); s = 1.0f; break;
        case ENG_SOLITON: delta = kdv_delta(c.timbre, amp); s = 1.0f; break;
        case ENG_CHAOS: s = ks_s(c.timbre); beta = ks_beta(c.color); break;
        default: {
            s = rogue_s(c.timbre);
            carrier = rogue_carrier(c.color);
            const int q = 1 + (int)(kRogueKappa / s);
            kr = q < kModes ? q : kModes;
            break;
        }
        }
    }

    // Take the new settings: engine, parameters and time step, then the
    // linear-flow factors for that step.
    void latch(const FieldCtl &c) {
        const int e = c.engine;
        if (e != engine) {
            if (is_real(e) != is_real(engine)) {
                // ROGUE <-> a real field: incompatible states, start over
                float ph[3] = {0, 0, 0};
                engine = e;
                note_on_keep(c, ph);
                return;
            }
            engine = e;
            if (engine == ENG_SHOCK) ref0 = rms_s;
        }
        params(c);
        // Time step: the requested rate, limited by stability/accuracy.
        // Backwards only for the reversible engines, and then without damping.
        float hr = c.rate * t_step;
        if (hr < 0.0f) {
            if (!reversible(engine)) hr = 0.0f;
            else gamma = 0.0f;
        }
        const float um = umax_last > 1e-3f ? umax_last : 1e-3f;
        float hmax;
        if (engine == ENG_ROGUE) {
            // per substep (h/2): nonlinear rotation 2 (h/2) max|psi|^2 <= theta,
            // and linear phase of the top mode (h/2) (s kr)^2 <= kRoguePhase
            const float kmax = s * (float)kr;
            const float h1 = kRogueTheta / um, h2 = 2.0f * kRoguePhase / (kmax * kmax);
            hmax = h1 < h2 ? h1 : h2;
        } else {
            hmax = kCfl / (s * (float)kModes * um);
        }
        if (hr > hmax) hr = hmax;
        if (hr < -hmax) hr = -hmax;
        h = hr;
        if (h != 0.0f) factors(0, kModes / 2);  // the rest at ITEM_SPECTRUM
    }

    // Restart the field at the current amplitude (engine change across
    // the real/complex divide).
    void note_on_keep(const FieldCtl &c, const float *ph) {
        FieldCtl cc = c;
        cc.engine = engine;
        note_on(cc, amp > engine_amp_max(engine) ? engine_amp_max(engine) : amp, 0.0f, 0.3f, ph, false);
    }

    // E_k = exp(lambda_k h/2), k0 <= k < k1: half a step of the linear flow
    // (ROGUE: h/4, half of a substep of h/2).
    void factors(int k0, int k1) {
        const float hh = engine == ENG_ROGUE ? 0.25f * h : 0.5f * h;
        const float K = (float)kModes;
        for (int k = k0; k < k1; k++) {
            const float q = (float)k;
            float lr = -gamma, li = 0.0f;
            switch (engine) {
            case ENG_SHOCK: {
                const float r = q / K, r2 = r * r, r4 = r2 * r2, r8 = r4 * r4;
                lr += -nu * q * q - kShockMu * r8 * r8;
                break;
            }
            case ENG_SOLITON: {
                const float r = q / K, r2 = r * r, r4 = r2 * r2, r8 = r4 * r4;
                lr -= kSolitonMu * r8 * r8;
                li = delta * delta * q * q * q;
                break;
            }
            case ENG_CHAOS: {
                const float sq = s * q, sq2 = sq * sq;
                lr += sq2 - sq2 * sq2;
                li = beta * sq2 * sq;
                break;
            }
            default: li = -s * s * q * q; break;
            }
            // Backwards (h < 0, reversible engines only) without the dissipative
            // terms: run backwards they would amplify.
            const float m = h < 0.0f ? 1.0f : exp_acc(lr * hh);
            float c, sn;
            sincos_unit(li * hh, c, sn);
            E[k].re = m * c;
            E[k].im = m * sn;
        }
    }

    // v_k *= E_k, k = 0..K (real fields).
    void half_linear() {
        for (int k = 0; k <= kModes; k++) {
            const float a = v[k].re, b = v[k].im;
            v[k].re = E[k].re * a - E[k].im * b;
            v[k].im = E[k].re * b + E[k].im * a;
        }
    }

    // --------------------------------------------------------- real stages
    // One stage of a Strang step: half the linear flow (exact), a classical
    // RK4 step of the nonlinear flow u_t = N(u), N(u) = -s (u^2/2)_y
    // (+ F sin y), half the linear flow:
    //   stage 0: v = E v;  k1 = h N(v),          acc = v + k1/6
    //   stage 1:           k2 = h N(v + k1/2),   acc += k2/3
    //   stage 2:           k3 = h N(v + k2/2),   acc += k3/3
    //   stage 3:           k4 = h N(v + k3),     v = E (acc + k4/6)
    // Part a: the stage input, its inverse FFT, u^2/2 on the grid.
    void stage_a(int st) {
        const int n = kModes + 1;
        if (st == 0) {
            half_linear();
            for (int k = 0; k < n; k++) in[k] = v[k];
        } else {
            const float w = st == 3 ? 1.0f : 0.5f;  // r holds the previous stage
            for (int k = 0; k < n; k++) {
                in[k].re = v[k].re + w * r[k].re;
                in[k].im = v[k].im + w * r[k].im;
            }
        }
        for (int k = n; k < kHalf; k++) in[k].re = in[k].im = 0.0f;
        irfft(in, grid, kGrid, z);
        float m = 0.0f;
        for (int j = 0; j < kGrid; j++) {
            const float u = grid[j];
            const float a = u < 0.0f ? -u : u;
            if (a > m) m = a;
            grid[j] = 0.5f * u * u;
        }
        if (m > umax) umax = m;
    }
    // Part b: forward FFT, r = h N(in), and the RK4 bookkeeping.
    void stage_b(int st) {
        const int n = kModes + 1;
        rfft(grid, r, kGrid, z);
        const float sh = s * h;
        for (int k = 0; k < n; k++) {  // (a + ib)(-i k s h) = b ksh - i a ksh
            const float c = sh * (float)k, a = r[k].re, b = r[k].im;
            r[k].re = b * c;
            r[k].im = -a * c;
        }
        for (int k = n; k < kHalf; k++) r[k].re = r[k].im = 0.0f;
        if (engine == ENG_SHOCK) r[1].im -= 0.5f * h * F;  // F sin y = F/(2i) e^{iy} + c.c.
        if (st == 0) {
            for (int k = 0; k < n; k++) {
                acc[k].re = v[k].re + r[k].re * (1.0f / 6);
                acc[k].im = v[k].im + r[k].im * (1.0f / 6);
            }
        } else if (st < 3) {
            for (int k = 0; k < n; k++) {
                acc[k].re += r[k].re * (1.0f / 3);
                acc[k].im += r[k].im * (1.0f / 3);
            }
        } else {
            for (int k = 0; k < n; k++) {
                v[k].re = acc[k].re + r[k].re * (1.0f / 6);
                v[k].im = acc[k].im + r[k].im * (1.0f / 6);
            }
            half_linear();
            v[0].re = v[0].im = 0.0f;  // the mean stays 0 (N_0 = 0; forcing has no mean)
        }
    }

    // ---------------------------------------------------------------- ROGUE
    // ROGUE: multiply mode k (FFT order) by E_|k|, and truncate to |k| <= kr.
    void rogue_linear() {
        for (int k = 0; k < kGrid; k++) {
            const int q = k <= kGrid / 2 ? k : kGrid - k;
            if (q > kr) { v[k].re = v[k].im = 0.0f; continue; }
            const float a = v[k].re, b = v[k].im;
            v[k].re = E[q].re * a - E[q].im * b;
            v[k].im = E[q].re * b + E[q].im * a;
        }
    }

    // ROGUE: a Strang substep of h/2 in four items. The inverse transform is
    // conj(DFT(conj v)), the conjugations folded into the loops.
    //   0: half linear step, z = conj v, 128-point FFT pass and half 0
    //   1: FFT half 1 and bit reversal: z = conj psi on the grid
    //   2: psi *= (1 + i q)/(1 - i q), q = (h/2) |psi|^2 (half the rotation
    //      angle th = 2 (h/2) |psi|^2), into z; FFT pass and half 0
    //   3: FFT half 1, bit reversal, v = z/128, half linear step (truncating)
    void rogue_item(int q) {
        switch (q) {
        case 0:
            rogue_linear();
            for (int k = 0; k < kGrid; k++) {
                z[k].re = v[k].re;
                z[k].im = -v[k].im;
            }
            cfft128_pass(z);
            cfft128_half(z, 0);
            break;
        case 1:
            cfft128_half(z, 1);
            cfft128_bitrev(z);
            break;
        case 2: {
            const float hs = 0.5f * h;
            float m = 0.0f;
            for (int j = 0; j < kGrid; j++) {
                const float a = z[j].re, b = -z[j].im;  // psi_j
                const float p2 = a * a + b * b;
                if (p2 > m) m = p2;
                const float qq = hs * p2;
                const float d = 1.0f / (1.0f + qq * qq);
                const float cr = (1.0f - qq * qq) * d, ci = 2.0f * qq * d;
                z[j].re = cr * a - ci * b;
                z[j].im = cr * b + ci * a;
            }
            if (m > umax) umax = m;
            cfft128_pass(z);
            cfft128_half(z, 0);
            break;
        }
        default: {
            cfft128_half(z, 1);
            cfft128_bitrev(z);
            const float sc = 1.0f / (float)kGrid;
            for (int k = 0; k < kGrid; k++) {
                v[k].re = z[k].re * sc;
                v[k].im = z[k].im * sc;
            }
            rogue_linear();
            break;
        }
        }
    }

    // ---------------------------------------------------------------- table
    // The table's spectrum from the current state, normalised to kOutRms and
    // band-limited for f0; then the inverse FFT is set up (item 8).
    // X[j] for j > kMaxHarm stays 0 from init: no engine writes there.
    void table_spectrum(const FieldCtl &c) {
        int jmax;
        if (engine == ENG_ROGUE) {
            // eta = Re(psi e^{imy}): eta_j = (psi_{j-m} + conj psi_{-j-m}) / 2
            const int m = carrier;
            jmax = kr + m;
            for (int j = 0; j <= jmax; j++) {
                X[j].re = X[j].im = 0.0f;
                const int a = j - m, b = -j - m;
                if (a >= -kr && a <= kr) {
                    const Cplx &p = v[a >= 0 ? a : kGrid + a];
                    X[j].re += 0.5f * p.re;
                    X[j].im += 0.5f * p.im;
                }
                if (b >= -kr && b <= kr) {
                    const Cplx &p = v[b >= 0 ? b : kGrid + b];
                    X[j].re += 0.5f * p.re;
                    X[j].im -= 0.5f * p.im;
                }
            }
        } else {
            jmax = kModes;
            for (int j = 1; j <= kModes; j++) X[j] = v[j];
        }
        for (int j = jmax + 1; j <= kMaxHarm; j++) X[j].re = X[j].im = 0.0f;  // left by another engine
        X[0].re = X[0].im = 0.0f;  // no DC
        float e = 0.0f;
        for (int j = 1; j <= jmax; j++) e += X[j].re * X[j].re + X[j].im * X[j].im;
        const float rms = __builtin_sqrtf(2.0f * e);
        rms_s += rms_c * (rms - rms_s);
        float ref;
        if (engine == ENG_SHOCK) {
            // Free decay is the envelope: keep the note-on reference. With
            // drive F the inviscid steady state has RMS sqrt(2F).
            const float d = __builtin_sqrtf(2.0f * F);
            ref = ref0 > d ? ref0 : d;
        } else {
            ref = rms_s;
        }
        const float g = kOutRms / (ref > 1e-4f ? ref : 1e-4f);
        const float f0 = c.f0 > 1.0f ? c.f0 : 1.0f;
        for (int j = 1; j <= jmax; j++) {
            const float w = clampf((kBandStop - (float)j * f0) * (1.0f / (kBandStop - kBandPass)), 0.0f, 1.0f);
            X[j].re *= g * w;
            X[j].im *= g * w;
        }
    }
    // Item 10: finish the inverse FFT into the free table; the tables rotate
    // and the new one fades in over one step.
    void table_finish() {
        cfft128_half(z, 1);
        cfft128_bitrev(z);
        float *t = tab[tnext];
        irfft_post(z, t, kTableLen);
        t[kTableLen] = t[0];
        const int old = tprev;
        tprev = tcur;
        tcur = tnext;
        tnext = old;
        alpha = 0.0f;
    }

    // ---------------------------------------------------------------- items
    // One item of work (see kItemsPerStep).
    void run_item(const FieldCtl &c) {
        const int it = item;
        item = it + 1 < kItemsPerStep ? it + 1 : 0;  // before the work: a restart may set item
        if (it < ITEM_BOUNDARY) {
            if (h == 0.0f) return;  // frozen
            if (engine == ENG_ROGUE) rogue_item(it & 3);
            else if (it & 1) stage_b(it >> 1);
            else stage_a(it >> 1);
            return;
        }
        switch (it) {
        case ITEM_BOUNDARY:
            if (h != 0.0f) {
                steps++;
                umax_last = umax;
            }
            umax = 0.0f;
            if (kick != 0.0f) apply_kick();
            latch(c);
            break;
        case ITEM_SPECTRUM:
            if (h != 0.0f) factors(kModes / 2, kModes + 1);
            table_spectrum(c);
            break;
        case ITEM_SPECTRUM + 1:
            irfft_pre(X, kTableLen, z);
            cfft128_pass(z);
            cfft128_half(z, 0);
            break;
        default:
            table_finish();
            break;
        }
    }

    // Kick: a Gaussian bump of width 0.3 rad at kick_pos, mean removed, of
    // peak about kick * amp. ROGUE: the same bump multiplies the background
    // (a local focusing seed). g_k = exp(-(0.3 k)^2/2) by the recurrence
    // g_k / g_{k-1} = exp(-0.045 (2k - 1)), and e^{-ik pos} by rotation.
    void apply_kick() {
        float g = 1.0f, step = exp_acc(-0.045f), sum = 0.0f;
        const float q = step * step;
        for (int k = 1; k <= kModes; k++) {
            g *= step;
            step *= q;
            X[k].re = g;  // scratch: rebuilt at the next table
            sum += 2.0f * g;
        }
        const float sc = kick * amp / sum;  // peak of sum_k g_k e^{ik(y - pos)} + c.c. is sum
        float c1, s1;
        sincos_unit(-kick_pos, c1, s1);
        float cr = 1.0f, ci = 0.0f;  // e^{-ik pos}
        for (int k = 1; k <= kModes; k++) {
            const float t = cr * c1 - ci * s1;
            ci = cr * s1 + ci * c1;
            cr = t;
            const float a = sc * X[k].re * cr, b = sc * X[k].re * ci;
            if (engine == ENG_ROGUE) {
                // psi += kick * bump * psi_0 (psi_0 = mean of psi), on both sides
                const float pr = v[0].re, pi = v[0].im;
                v[k].re += a * pr - b * pi;
                v[k].im += a * pi + b * pr;
                v[kGrid - k].re += a * pr + b * pi;
                v[kGrid - k].im += a * pi - b * pr;  // conj(bump_k) * psi_0
            } else {
                v[k].re += a;
                v[k].im += b;
            }
        }
        kick = 0.0f;
    }

    // Audio: n samples at f0, crossfading from the previous table.
    void play(float *out, int n, float f0) {
        const float inc = f0 * (1.0f / kSampleRate);
        const float *tp = tab[tprev], *tc = tab[tcur];
        float ph = phase, al = alpha;
        for (int i = 0; i < n; i++) {
            const float pos = ph * (float)kTableLen;
            int j = (int)pos;
            if (j >= kTableLen) j = kTableLen - 1;
            const float fr = pos - (float)j;
            const float a = tp[j] + fr * (tp[j + 1] - tp[j]);
            const float b = tc[j] + fr * (tc[j + 1] - tc[j]);
            out[i] = a + al * (b - a);
            al += dalpha;
            if (al > 1.0f) al = 1.0f;
            ph += inc;
            if (ph >= 1.0f) ph -= 1.0f;
        }
        phase = ph;
        alpha = al;
    }
};

}  // namespace freak

#endif
