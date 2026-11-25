// west_voice.h: the "westcoast" patch's voices: a complex oscillator into a
// low-pass gate, after Buchla's 259 (complex oscillator), 292 (low-pass
// gate), 281 (function generator) and 266 (source of uncertainty).
//
// Per voice (at 2 x 48 kHz, see Rates):
//  - Modulation oscillator (MO): a sine morphing into a triangle and then a
//    square; its frequency is a ratio of the principal's (it follows the
//    keys) or a free rate.
//  - Principal oscillator (PO): a sine, its phase increment modulated by the
//    MO, linearly and through zero: inc = dt (1 + I m), m in [-1, 1], so for
//    an index I > 1 the phase runs backwards for part of the MO's cycle (the
//    spectrum is symmetric about 0 and folds into the positive side).
//  - Wavefolder (TIMBRE, SYMMETRY): y = (1 - a) x + a sin(beta x + c) for
//    the PO's sine x. With a = min(1, 8 t) and beta = (pi/2)(1 + 8 max(0,
//    t - 1/8)) for TIMBRE t in [0, 1]: t = 0 is the plain sine, t = 1/8 the
//    first fold (sin(pi/2 sin)), t = 1 beta = 4 pi, four folds each way.
//    sin(beta sin(theta) + c) = sum_n J_n(beta) sin(n theta + c) (the
//    Jacobi-Anger expansion): the harmonics are Bessel weights, odd only for
//    c = 0, and SYMMETRY c in [-pi/2, pi/2] brings in the even ones. |y| <= 1
//    (convex combination of two values in [-1, 1]).
//  - A one-pole high-pass at 15 Hz removes the DC that SYMMETRY makes before
//    the gate, so the gate does not pump it.
//  - Low-pass gate: two trapezoidal one-poles (12 dB/oct, no resonance, as
//    the 292's) and a gain, both driven by the vactrol's light v in [0, 1]:
//    cutoff = 20 Hz 2^(10 v) (20 Hz .. 20 kHz), gain = v^2. LP mode: the
//    filter only; VCA: the gain only; COMBO: both.
//  - Vactrol: v follows its drive u, fast up (one pole, 1.5 ms) and slowly
//    down along dd/dt = -(6 d^2 + d)/T for d = v - u: a fast start and a long
//    tail, as a vactrol's resistance recovers (T = LPG DECAY). The drive u is
//    a strike (for 2 ms after each key, strong enough that the light
//    reaches the key's velocity: the gate "pings") or the function
//    generator, plus a bias. Explicit Euler, stable because the
//    step factor (6 d + 1)/(T fs) <= 7/(0.02 s 48 kHz) < 1 for T >= 20 ms
//    and d <= 1, and it keeps d >= 0 (clamped), so v stays in [0, 1].
//  - Function generator: rise and fall (linear in an inner ramp r, shaped
//    log 1 - (1 - r)^2, linear, or exp r^2), TRANSIENT (rise then fall),
//    SUSTAIN (holds at 1 while the key is down) or CYCLE (rises and falls
//    while the key is down). It moves the timbre, the FM index and, if the
//    gate does not ping, the gate.
//  - Uncertainty: each note draws a value in [-1, 1] that offsets its
//    timbre and FM index (stored random); a smooth random drift (shared)
//    moves the timbre.
//
// Rates. The oscillators, folder and gates of every voice run at 96 kHz
// (folding and FM spread the spectrum: sin(beta sin) at beta = 4 pi has
// its energy within about beta + 1 = 14 harmonics, Carson's rule), and the
// voices are summed before one half-band decimator (raw_voice.h's: 55 taps,
// -79 dB from 28 kHz), so the decimation is paid once. Function generator
// and vactrol per 48 kHz sample; pitch, timbre and gate coefficients every
// kWSub = 8 samples (6 kHz), interpolated linearly at 96 kHz (every 4: 80.5 %
// DSP with four voices held; every 8: 74.5 %).
//
// Header-only, C++11, no libm, every struct a POD.
#ifndef WEST_VOICE_H
#define WEST_VOICE_H

#include <stdint.h>

#include "raw_voice.h"  // Halfband, raw_halfband_design, raw_G (96 kHz), RawRng

namespace freak {

const int kWVoices = 4;
const int kWSub = 8;                                  // 48 kHz samples per sub-block (6 kHz updates)
const int kWSubs = kBlock / kWSub;
const float kWRate = kRawRate;                        // 96 kHz
const float kWOn = 0.01379f;                          // 1 - exp(-1/(1.5 ms * 48 kHz))
const uint32_t kWStrike = 96;                         // 2 ms at 48 kHz
// A strike drives the vactrol past full, as a hard-driven LED: in kWStrike
// steps v reaches u (1 - (1 - kWOn)^96) = 0.7364 u from 0, so a drive of
// vel / 0.7364 brings a closed gate to v = vel (v is clamped to 1).
const float kWStrikeDrive = 1.358f;
const float kWMinDecay = 0.02f;                       // s: keeps the vactrol's Euler step stable
const float kWMaxInc = 0.45f;                         // phase increment clamp (|inc| < 1/2)
const float kWHpG = 0.000490633f;                     // one-pole TPT G for 15 Hz at 96 kHz: g/(1 + g), g = tan(pi 15/96000)

enum { WF_TRANSIENT, WF_SUSTAIN, WF_CYCLE, NUM_WF };
enum { WR_LOG, WR_LIN, WR_EXP, NUM_WR };
enum { WL_LP, WL_COMBO, WL_VCA, NUM_WL };

// sin(2 pi p) for p in [0, 1): reduced to a quarter wave, Taylor degree 9
// on [0, pi/2] (error <= (pi/2)^11/11! < 3.6e-6, as fast_sin), no division.
inline float w_psin(float p) {
    float s = 1.0f;
    if (p >= 0.5f) { p -= 0.5f; s = -1.0f; }  // sin(2 pi (p + 1/2)) = -sin(2 pi p)
    if (p > 0.25f) p = 0.5f - p;              // sin(pi - u) = sin u
    const float u = 6.28318531f * p, u2 = u * u;
    return s * u * (1.0f + u2 * (-1.0f / 6 + u2 * (1.0f / 120 + u2 * (-1.0f / 5040 + u2 * (1.0f / 362880)))));
}
// sin(u) for any u (the folder's argument stays within |beta| + |c| < 15).
inline float w_sin(float u) {
    float p = u * 0.159154943f;  // 1/(2 pi)
    const int32_t i = (int32_t)p;
    p -= (float)i;               // in (-1, 1)
    if (p < 0.0f) p += 1.0f;
    return w_psin(p);
}

// The voices' sines come from a table: kWTab points of one period (and the
// first again at the end), linear interpolation. Error: interpolation
// <= h^2/8 with h = 2 pi/kWTab (4.7e-6), the entries' own <= 3.6e-6
// (w_psin), float rounding of the index <= 1.5e-6: below 1e-5 (proved;
// measured in the tests). One table for all voices, filled at init (bss,
// not code memory). It replaced three polynomial sines with branches per
// oversampled sample.
const int kWTab = 1024;
struct WestTables {
    float s[kWTab + 1];
    void init() {
        for (int i = 0; i < kWTab; i++) s[i] = w_psin((float)i / (float)kWTab);
        s[kWTab] = s[0];
    }
};
static WestTables g_wtab;  // one per patch
inline WestTables &wtab() { return g_wtab; }
// sin(2 pi p), p in [0, 1)
inline float w_tsin(const float *s, float p) {
    const float q = p * (float)kWTab;
    const int32_t i = (int32_t)q;
    return s[i] + (q - (float)i) * (s[i + 1] - s[i]);
}
// sin(u) for |u| < 25 (4 periods): shifted by 4 periods so the index is
// positive, wrapped with a mask (kWTab is a power of 2)
inline float w_tsin_u(const float *s, float u) {
    const float q = (u * 0.159154943f + 4.0f) * (float)kWTab;
    const int32_t i = (int32_t)q;
    const float f = q - (float)i;
    const int32_t j = i & (kWTab - 1);
    return s[j] + f * (s[j + 1] - s[j]);
}

// Triangle in phase with w_psin: 0 at p = 0, 1 at 1/4, -1 at 3/4.
inline float w_tri_wave(float p) {
    float q = p + 0.25f;
    if (q >= 1.0f) q -= 1.0f;
    return 1.0f - 4.0f * absf(q - 0.5f);
}
// The folder (see the header): a in [0, 1] blends the plain sine x into
// sin(beta x + c).
inline float w_fold(float x, float a, float beta, float c) { return x + a * (w_sin(beta * x + c) - x); }
// TIMBRE t in [0, 1] -> the blend a and the fold beta.
inline void w_timbre(float t, float &a, float &beta) {
    a = t < 0.125f ? 8.0f * t : 1.0f;
    beta = 1.5707963f * (1.0f + 8.0f * (t > 0.125f ? t - 0.125f : 0.0f));
}

// What the voices read, set by the controller once per block.
struct WestParams {
    float ratio;      // MO frequency / PO frequency when the MO follows the keys
    bool mo_track;    // false: the MO runs at mo_hz
    float mo_hz;
    float mo_shape;   // 0 sine .. 1/2 triangle .. 1 square
    float fm;         // FM index I (through zero above 1)
    float timbre;     // 0..1
    float sym;        // -1..1
    int range;        // octaves from 8'
    float bend;       // semitones
    float glide;      // s for 99 % of an interval, 0 = off
    float rise, fall; // function generator, s
    int fmode;        // WF_*
    int fresp;        // WR_*
    float f_timbre;   // function -> timbre (0..1 of the range)
    float f_fm;       // function -> FM index
    int lpg_mode;     // WL_*
    bool ping;        // the gate strikes on each key (else it follows the function)
    float lpg_decay;  // vactrol fall time T, s
    float lpg_bias;   // added to the vactrol's drive, 0..1
    float rnd_note;   // stored random per note: 0..1
    float drift;      // the shared drift's depth on the timbre, 0..1
    float level;      // output gain
};

// The shared smooth random: a new target every 1/hz seconds, approached by
// a one-pole (per block).
struct WestDrift {
    RawRng rng;
    float target, value;
    uint32_t n;
    void init() { rng.s = 0x6C8E9CF5u; target = value = 0.0f; n = 0; }
    float tick(float hz) {
        if (n == 0) {
            target = rng.uni();
            n = (uint32_t)(kControlRate / clampf(hz, 0.05f, 50.0f));
        }
        n--;
        value += clampf(hz * 2.0f / kControlRate, 0.0f, 1.0f) * (target - value);
        return value;
    }
};

struct WestVoice {
    enum { F_IDLE, F_RISE, F_FALL, F_HOLD };
    float phi, theta;        // PO, MO phases
    float r;                 // function generator ramp
    int fstage;
    float v;                 // vactrol light
    uint32_t strike;         // 48 kHz samples left of the strike
    float hp, s1, s2;        // DC high-pass state, gate one-poles
    float pitch;
    bool primed;
    float dtp, dtm, beta, amix, G, gain;  // values at the end of the last sub-block
    float rnd;               // this note's stored random
    RawRng rng;
    // set by the allocator
    uint8_t note;
    bool gate;
    uint32_t trig, trig_seen;
    float vel;               // 0..1

    void init(uint32_t seed) {
        phi = theta = 0.0f;
        r = 0.0f;
        fstage = F_IDLE;
        v = 0.0f;
        strike = 0;
        hp = s1 = s2 = 0.0f;
        pitch = 60.0f;
        primed = false;
        dtp = dtm = 0.0f;
        beta = 1.5707963f; amix = 0.0f;
        G = 0.0f; gain = 0.0f;
        rnd = 0.0f;
        rng.s = 0x2545F491u ^ (seed * 0x9E3779B9u);
        if (!rng.s) rng.s = 1;
        note = 60; gate = false; trig = trig_seen = 0; vel = 0.8f;
    }

    bool idle() const { return !gate && fstage == F_IDLE && strike == 0 && v < 1e-4f; }

    // the function's shaped output
    static float shape(float x, int resp) {
        if (resp == WR_LOG) { const float y = 1.0f - x; return 1.0f - y * y; }
        if (resp == WR_EXP) return x * x;
        return x;
    }
    // one 48 kHz step of the function generator (state passed in, so the
    // render loop keeps it in registers)
    static void fstep(int &st, float &r, bool gate, int fmode, float up, float down) {
        switch (st) {
        case F_RISE:
            r += up;
            if (r >= 1.0f) {
                r = 1.0f;
                st = (fmode == WF_SUSTAIN && gate) ? F_HOLD : F_FALL;
            }
            break;
        case F_HOLD:
            if (!gate || fmode != WF_SUSTAIN) st = F_FALL;
            break;
        case F_FALL:
            r -= down;
            if (r <= 0.0f) {
                r = 0.0f;
                st = (fmode == WF_CYCLE && gate) ? F_RISE : F_IDLE;
            }
            break;
        default:
            break;
        }
    }
    // one 48 kHz step of the vactrol towards its drive u
    static void vstep(float &v, float u, float koff, float klin) {
        if (u > v) {
            v += kWOn * (u - v);
            if (v > 1.0f) v = 1.0f;
        } else {
            float d = v - u;
            d -= d * (koff * d + klin);
            v = u + (d > 0.0f ? d : 0.0f);
        }
    }
    void func_step(const WestParams &P, float up, float down) { fstep(fstage, r, gate, P.fmode, up, down); }
    void vactrol_step(float u, float koff, float klin) { vstep(v, u, koff, klin); }

    static float note_dt(float n) {  // per 96 kHz sample
        return 440.0f * fast_exp2((n - 69.0f) * (1.0f / 12.0f)) * (1.0f / kWRate);
    }

    // Add one block (2 kBlock samples at 96 kHz) of this voice to out96.
    // The state the loops touch lives in locals (stores to out96 would
    // otherwise force member reloads).
    void render(const WestParams &P, float drift, float *out96) {
        if (trig != trig_seen) {
            trig_seen = trig;
            fstage = F_RISE;
            strike = kWStrike;
            rnd = rng.uni();
        }
        if (idle()) return;

        const float target = (float)note;
        if (!primed || P.glide <= 0.0f) { pitch = target; primed = true; }
        const float pitch0 = pitch;
        if (P.glide > 0.0f) pitch += one_pole_coef(P.glide, kControlRate, kLog2_100) * (target - pitch);

        const float up = 1.0f / (clampf(P.rise, 0.0005f, 30.0f) * kSampleRate);
        const float down = 1.0f / (clampf(P.fall, 0.0005f, 30.0f) * kSampleRate);
        const float T = P.lpg_decay < kWMinDecay ? kWMinDecay : P.lpg_decay;
        const float klin = 1.0f / (T * kSampleRate), koff = 6.0f * klin;
        const float bias = clampf(P.lpg_bias, 0.0f, 1.0f);
        const float poff = 12.0f * (float)P.range + P.bend;
        const float mo_free = P.mo_hz * (1.0f / kWRate);
        const float ms = clampf(P.mo_shape, 0.0f, 1.0f);
        // MO morph weights: sine -> triangle on [0, 1/2], triangle -> square on [1/2, 1]
        const float w_sine = ms < 0.5f ? 1.0f - 2.0f * ms : 0.0f;
        const float w_sq = ms > 0.5f ? 2.0f * ms - 1.0f : 0.0f;
        const float w_tri = 1.0f - w_sine - w_sq;
        const float sym = 1.5707963f * clampf(P.sym, -1.0f, 1.0f);
        const int mode = P.lpg_mode, fmode = P.fmode, fresp = P.fresp;
        const bool ping = P.ping, key = gate;
        const float level = clampf(P.level, 0.0f, 4.0f), velo = vel;
        const float inv = 1.0f / (float)(kWSub * kRawOver);
        const float *S = wtab().s;

        float ph = phi, th = theta, h = hp, a1 = s1, a2 = s2, rr = r, vv = v;
        float cdtp = dtp, cdtm = dtm, cb = beta, ca = amix, cG = G, cg = gain;
        int st = fstage;
        uint32_t sk = strike;

        for (int sb = 0; sb < kWSubs; sb++) {
            // targets at the end of this sub-block, from the current
            // function and vactrol values
            const float frac = (float)(sb + 1) * (1.0f / kWSubs);
            const float pg = pitch0 + frac * (pitch - pitch0);
            const float ndtp = note_dt(pg + poff);
            const float ndtm = P.mo_track ? ndtp * P.ratio : mo_free;
            const float f = shape(rr, fresp);
            const float t = clampf(P.timbre + P.f_timbre * f + 0.4f * P.rnd_note * rnd + 0.4f * P.drift * drift, 0.0f, 1.0f);
            float na, nb;
            w_timbre(t, na, nb);
            const float nfm = P.fm + P.f_fm * f + 2.0f * P.rnd_note * rnd;
            const float nG = mode == WL_VCA ? raw_G(kRawMaxCut) : raw_G(20.0f * fast_exp2(10.0f * vv));
            const float ng = mode == WL_LP ? 1.0f : vv * vv;
            if (cdtp <= 0.0f) { cdtp = ndtp; cdtm = ndtm; cb = nb; ca = na; cG = nG; cg = ng; }
            const float ddtp = (ndtp - cdtp) * inv, ddtm = (ndtm - cdtm) * inv;
            const float db = (nb - cb) * inv, da = (na - ca) * inv;
            const float dG = (nG - cG) * inv, dg = (ng - cg) * inv;
            const float fmi = nfm > 0.0f ? nfm : 0.0f;

            for (int i = sb * kWSub; i < (sb + 1) * kWSub; i++) {
                fstep(st, rr, key, fmode, up, down);
                const float drive = ping ? (sk > 0 ? kWStrikeDrive * velo : 0.0f) : shape(rr, fresp);
                if (sk > 0) sk--;
                vstep(vv, clampf(drive + bias, 0.0f, kWStrikeDrive + 1.0f), koff, klin);
                for (int o = 0; o < kRawOver; o++) {
                    cdtp += ddtp; cdtm += ddtm; cb += db; ca += da; cG += dG; cg += dg;
                    // MO: sine, triangle and square in phase, weighted
                    float q = th + 0.25f;
                    q = q >= 1.0f ? q - 1.0f : q;
                    const float m = w_sine * w_tsin(S, th) + w_tri * (1.0f - 4.0f * absf(q - 0.5f)) +
                                    w_sq * (th < 0.5f ? 1.0f : -1.0f);
                    th += cdtm;
                    th = th >= 1.0f ? th - 1.0f : th;
                    // PO, through-zero FM
                    const float inc = clampf(cdtp * (1.0f + fmi * m), -kWMaxInc, kWMaxInc);
                    ph += inc;
                    ph = ph >= 1.0f ? ph - 1.0f : (ph < 0.0f ? ph + 1.0f : ph);
                    const float x = w_tsin(S, ph);
                    // folder (w_fold with the table sine)
                    const float y = x + ca * (w_tsin_u(S, cb * x + sym) - x);
                    // DC high-pass, then the gate
                    const float l0 = kWHpG * (y - h) + h;
                    h = 2.0f * l0 - h;
                    const float l1 = cG * (y - l0 - a1) + a1;
                    a1 = 2.0f * l1 - a1;
                    const float l2 = cG * (l1 - a2) + a2;
                    a2 = 2.0f * l2 - a2;
                    out96[2 * i + o] += level * cg * l2;
                }
            }
        }
        phi = ph; theta = th; hp = h; s1 = a1; s2 = a2; r = rr; v = vv; fstage = st; strike = sk;
        dtp = cdtp; dtm = cdtm; beta = cb; amix = ca; G = cG; gain = cg;
    }
};

// The voices' sum at 96 kHz -> 48 kHz, then a DC blocker at 5 Hz.
struct WestMix {
    HalfbandCoefs hc;
    Halfband dec;
    float dc_x, dc_y;
    void init() {
        raw_halfband_design(hc);
        dec.init();
        dc_x = dc_y = 0.0f;
    }
    void process(const float *in96, float *out) {
        const float r = 1.0f - 2.0f * kPi * 5.0f / kSampleRate;
        for (int i = 0; i < kBlock; i++) {
            const float y = dec.process(hc, in96[2 * i], in96[2 * i + 1]);
            const float o = y - dc_x + r * dc_y;
            dc_x = y;
            dc_y = o;
            out[i] = o;
        }
    }
};

}  // namespace freak

#endif
