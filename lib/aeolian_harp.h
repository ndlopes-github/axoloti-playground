// aeolian_harp.h: the aeolian patch's instrument, a wind harp. Eight
// strings are blown by a wind; each string sheds vortices, and the wake
// drives the string. Nothing is played directly: the keys tune the strings
// (aeolian_ctl.h), the wind plays them.
//
// Model (review points):
//  - String: a digital waveguide (velocity waves) split at the excitation
//    point y = beta L into a nut-side round trip of la samples (reflection
//    -1) and a bridge-side round trip of lb samples (reflection -1 through a
//    one-pole low-pass, the loss, its coefficient solved per string so
//    that harmonics near 4 kHz ring kappa times as long as the fundamental,
//    loss_coef), lb fractional by linear interpolation.
//    The loop is tuned exactly at the fundamental: la + lb + the low-pass's
//    phase delay at f1 = fs / f1 (atan formula, string_tuning below).
//  - Wake: a van der Pol oscillator per string (the wake-oscillator model of
//    vortex-induced vibration, Facchinetti, de Langre and Biolley, J. Fluids
//    Struct. 2004):  q'' + eps W (q^2 - 1) q' + W^2 q = K W^2 (v / L),
//    with W = 2 pi St U / d the Strouhal (shedding) frequency, St = 0.2,
//    v the string velocity at the excitation point, L the lift scale. The
//    string takes the lift force L q at that point. Near a string harmonic
//    the wake locks in to it (the harmonic sings); the coupling K sets how
//    wide the lock-in band is. Velocity coupling (not acceleration, as in
//    the paper) to avoid differentiating at audio rate.
//  - Wake integration per sample: Lie splitting with the linear part exact,
//    a rotation of (q, q'/W) by W dt (so the frequency is exact for any
//    W dt < pi), then the van der Pol damping q' *= exp(-eps W dt (q^2-1))
//    with q frozen, by 1 - x + x^2/2 (no division; x clamped to [-0.5, 1],
//    where the factor stays in [0.5, 1.625] and damps large q), then the
//    coupling. eps W dt is capped at 0.1 for high shedding frequencies;
//    the van der Pol limit cycle has amplitude 2 for any eps, so the cap
//    changes only how fast it is reached. Shedding is kept below fs/4
//    (12 kHz); above it the lift fades out.
//  - Wind: mean speed U (knob, mod wheel, pads) times 1 + g, with g steady
//    (0), an Ornstein-Uhlenbeck process of standard deviation sigma and
//    time tau (exact discretisation), or the x of the Lorenz-63 system
//    (scaled by sigma / 8, time scaled by tau). Each string also sees its
//    own slow turbulence (an OU process, 8 % of U, tau 0.3 s), so the
//    strings do not lock identically.
//  - Output: the bridge waves, panned across the stereo field, a sound box
//    (four band-passes per channel) mixed in by BODY, a DC blocker, and a
//    slow level control (peak follower, attack 5 ms, release 2 s, gain at
//    most 6) into a soft clip: the wind's level swings are ~40 dB.
//
// Every struct is a POD; the delay lines live in the object (CCM), 40 KB.
#ifndef AEOLIAN_HARP_H
#define AEOLIAN_HARP_H

#include <stdint.h>

#include "freak_dsp.h"

namespace freak {

const int kHarpStrings = 8;
const int kSegA = 256;                // nut-side round trip (la <= 254 > 0.28 * fs / 55 Hz); a power of 2
const int kMaskA = kSegA - 1;
const int kSegB = 1024;               // bridge-side round trip (lb <= fs / 55 Hz = 873); a power of 2
const int kMaskB = kSegB - 1;
const float kHarpLowHz = 55.0f;       // string fundamentals are folded into
const float kHarpHighHz = 1760.0f;    // [55, 1760] Hz by octaves
const float kStrouhal = 0.2f;
const float kWakeEps = 0.3f;
const float kWakeEpsTheta = 0.1f;     // cap on eps W dt
const float kMaxTheta = 1.5707963f;   // W dt at fs/4
const float kLiftScale = 0.01f;       // lift per (U / 5 m/s)^2 per mm of diameter
const float kCouplingNorm = 0.05f;    // c in the coupling K W dt c v / L: K = 0.01 locks in over a few % of f1 (tests)
const float kLevelTarget = 0.35f;     // output peak the level control aims at
const float kLevelMaxGain = 6.0f;
const int kHarpTurn = kHarpStrings;   // ticks between a string's parameter updates (one string per tick)

enum { WX_STEADY, WX_GUSTY, WX_LORENZ, NUM_WEATHER };

// What the harp reads; published by aeolian_ctl.h.
struct AeoBus {
    float note[kHarpStrings];  // target MIDI note of each string (fractional)
    float d[kHarpStrings];     // string diameter, mm
    bool on[kHarpStrings];
    float wind;                // mean wind, m/s (knob, mod wheel, pads)
    float sigma, tau;          // gusts: relative standard deviation, time (s)
    int weather;               // WX_*
    bool freeze;               // hold the wind where it is
    float coupling;            // K
    float t60;                 // string ring time, s
    float bright;              // kappa: T60 near 4 kHz / T60 of the fundamental (1 flat .. 0.02 dark)
    float air, body;           // 0..1
    float glide;               // retune time, s
    float bend;                // semitones, all strings
    uint32_t plucks[kHarpStrings];  // pluck counters, one per string
    float pluck_amt;
};

static AeoBus g_aeobus;  // one per patch
inline AeoBus &aeobus() { return g_aeobus; }

struct HarpRng {
    uint32_t s;
    float uni() {  // [-1, 1)
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(s >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
    // Sum of three uniforms on [-1, 1): mean 0, variance 3 * 1/3 = 1.
    float gauss() { return uni() + uni() + uni(); }
};

// The wind's generator: splitmix32 (a Weyl sequence through a mixing
// hash). Drawing the gusts from the audio noise's xorshift stream,
// interleaved with the strings' draws, biased the gusts' mean by 4
// standard errors in a 200 s test; this one does not.
struct WindRng {
    uint32_t s;
    float uni() {
        uint32_t z = (s += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        z ^= z >> 16;
        return (float)(z >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
    float gauss() { return uni() + uni() + uni(); }
};

// atan x for |x| <= 1 (odd polynomial, error < 2e-5 on [-1, 1]).
inline float atan_unit(float x) {
    const float x2 = x * x;
    return x * (0.99997726f + x2 * (-0.33262347f + x2 * (0.19354346f + x2 * (-0.11643287f +
               x2 * (0.05265332f + x2 * (-0.01172120f))))));
}

// atan x for x >= 0: atan x = pi/2 - atan(1/x) above 1.
inline float atan_pos(float x) { return x <= 1.0f ? atan_unit(x) : 1.5707963f - atan_unit(1.0f / x); }

// Phase delay, in samples, of the loss filter (1 - b)/(1 - b z^-1) at w rad
// per sample: atan(b sin w / (1 - b cos w)) / w (0 <= b < 1, 0 < w < pi/2).
inline float lowpass_delay(float b, float w) {
    if (w < 1e-4f) return b / (1.0f - b);  // the w -> 0 limit
    const float sw = fast_sin(w), cw = fast_sin(w + 1.5707963f);
    return atan_pos(b * sw / (1.0f - b * cw)) / w;
}

// Loss-filter coefficient b for a string at f1 Hz with ring time t60 s, such
// that harmonics near 4 kHz ring kappa * t60 (kappa = 1: flat). Per round
// trip the filter must then pass m = 10^(-3 (1/kappa - 1) / (f1 t60)) at
// w_r = 2 pi 4000 / fs. The magnitude condition (1 - b)^2 = m^2 (1 - 2 b
// cos w_r + b^2) is the quadratic (1 - m^2) b^2 - 2 (1 - m^2 cos w_r) b +
// (1 - m^2) = 0, whose root in [0, 1) is taken. A fixed b (the first
// version) damped a dark high string's own fundamental within milliseconds.
inline float loss_coef(float f1, float t60, float kappa) {
    const float cr = 0.86602540f;  // cos(2 pi 4000 / 48000)
    const float m = fast_exp2(-9.96578428f * (1.0f / kappa - 1.0f) / (f1 * t60));
    const float m2 = m * m, a = 1.0f - m2, bq = 1.0f - m2 * cr;
    if (a < 1e-6f) return 0.0f;
    const float b = (bq - __builtin_sqrtf(bq * bq - a * a)) / a;
    return clampf(b, 0.0f, 0.95f);
}

struct HarpString {
    float a[kSegA], b[kSegB];
    int wa, wb, la;
    float lb;
    float lp, g, bc;        // loss filter: state, gain per round trip, coefficient
    float q, p;             // wake: q, and q' / W
    float vprev;            // string velocity at the excitation point, last sample
    float c, s, et, k;      // this block: rotation, eps W dt, K W dt
    float lift, air;        // this block: lift scale, noise level
    float nlp;              // noise low-pass state
    float note;             // current note (glides)
    float target;           // note being glided to
    float beta, panl, panr;
    float level;            // on/off fade
    float pluck;            // pluck burst amplitude
    int pluck_n;            // samples left in the burst
    uint32_t plucks_seen;
    float local;            // the string's own turbulence (OU, unit variance)
    float theta;            // W dt this block (tests, logs)

    void init(float beta_, float pan, float note0) {
        for (int i = 0; i < kSegA; i++) a[i] = 0.0f;
        for (int i = 0; i < kSegB; i++) b[i] = 0.0f;
        wa = wb = 0;
        beta = beta_;
        panl = __builtin_sqrtf(1.0f - pan);
        panr = __builtin_sqrtf(pan);
        lp = 0.0f; g = 0.9f; bc = 0.3f;
        q = 0.1f; p = 0.0f;
        vprev = 0.0f;
        c = 1.0f; s = 0.0f; et = 0.0f; k = 0.0f;
        lift = air = 0.0f;
        nlp = 0.0f;
        note = target = note0;
        level = 0.0f;
        pluck = 0.0f; pluck_n = 0;
        plucks_seen = 0;
        local = 0.0f;
        theta = 0.0f;
        la = 1; lb = 2.0f;
        set_la();
    }

    static float hz(float note) {
        float f = 440.0f * fast_exp2((note - 69.0f) * (1.0f / 12.0f));
        return clampf(f, kHarpLowHz, kHarpHighHz);
    }

    // The nut-side length, from the shorter of the current and the target
    // loops, so the bridge side stays positive all through a glide.
    void set_la() {
        const float p0 = kSampleRate / hz(note), p1 = kSampleRate / hz(target);
        const float pm = p0 < p1 ? p0 : p1;
        int l = (int)(beta * pm + 0.5f);
        if (l < 1) l = 1;
        if (l > kSegA - 2) l = kSegA - 2;
        la = l;
    }

    // The state the per-sample loop works on, in locals (stores to the
    // outputs would otherwise make the compiler reload every member).
    struct Run {
        float cq, cp, v1, f, nl;
        float gg, bb, kk, cc, ss, ett, lf, ar, pl, pr, phi, pk;
        int ia, j0, iwa, iwb, pn;
        uint32_t rs;
    };

    // Samples [i0, i1) of one block. PLUCK: the pluck burst is running (a
    // separate copy of the loop, so the common one has no test for it).
    template <bool PLUCK>
    void run(Run &R, float *out_l, float *out_r, int i0, int i1) {
        float *A = a, *B = b;
        for (int i = i0; i < i1; i++) {
            // returning waves; the bridge side at delay nb + phi
            const float inA = A[R.ia];
            const float x = B[R.j0] + R.phi * (B[(R.j0 - 1) & kMaskB] - B[R.j0]);
            R.f = R.gg * x + R.bb * R.f;
            const float inB = -R.f;
            // wake: exact rotation, van der Pol damping, coupling. The damping
            // factor exp(-xd) is 1 - xd + xd^2/2 (positive for every xd).
            // xd >= -eps W dt >= -0.1, so only the top needs a clamp: at 1,
            // where the factor is 0.5 and large q stays damped.
            const float q2 = R.cq * R.cc + R.cp * R.ss;
            float p2 = R.cp * R.cc - R.cq * R.ss;
            float xd = R.ett * (q2 * q2 - 1.0f);
            xd = xd > 1.0f ? 1.0f : xd;
            p2 *= 1.0f - xd + 0.5f * xd * xd;
            p2 += R.kk * R.v1;
            R.cq = q2;
            R.cp = p2;
            // turbulence noise: an LCG's top 23 bits as a float in [1, 2)
            R.rs = R.rs * 1664525u + 1013904223u;
            union { uint32_t u; float fl; } nz;
            nz.u = (R.rs >> 9) | 0x3F800000u;
            R.nl += 0.3f * (2.0f * nz.fl - 3.0f - R.nl);
            float d = R.lf * R.cq + R.ar * R.nl;
            if (PLUCK) {
                d += R.pk * (float)R.pn;
                R.pn--;
            }
            const float outA = inB + d, outB = inA + d;
            R.v1 = inA + inB + d;
            A[R.iwa] = -outA;
            B[R.iwb] = outB;
            R.iwa = (R.iwa + 1) & kMaskA;
            R.iwb = (R.iwb + 1) & kMaskB;
            R.ia = (R.ia + 1) & kMaskA;
            R.j0 = (R.j0 + 1) & kMaskB;
            out_l[i] += R.pl * inB;
            out_r[i] += R.pr * inB;
        }
    }

    // n samples, added to out_l / out_r. Integer indices only (the
    // fractional part phi of lb is the same for the whole block, the buffers
    // are powers of two), no division and no int/float conversion in the
    // common loop: on the board the first version took 265 cycles per
    // string and sample.
    void process(float *out_l, float *out_r, int n, HarpRng &rng) {
        Run R;
        R.cq = q; R.cp = p; R.v1 = vprev; R.f = lp; R.nl = nlp;
        R.gg = g * (1.0f - bc); R.bb = bc;
        // K W dt c / L: the coupling acts on v / L (L = this string's lift
        // scale), so the loop gain wake -> string -> wake does not depend on
        // the wind speed or the diameter, as in the dimensionless model.
        R.kk = kCouplingNorm * k / (lift > 2e-4f ? lift : 2e-4f);
        R.cc = c; R.ss = s; R.ett = et; R.lf = lift; R.ar = air; R.pl = panl; R.pr = panr;
        const int nb = (int)lb;  // lb = nb + phi: (1 - phi) b[wb - nb] + phi b[wb - nb - 1]
        R.phi = lb - (float)nb;
        R.ia = (wa - la) & kMaskA;
        R.j0 = (wb - nb) & kMaskB;
        R.iwa = wa; R.iwb = wb; R.pn = pluck_n;
        R.pk = pluck * (1.0f / 96.0f);
        R.rs = rng.s;
        const int m = R.pn < n ? R.pn : n;
        if (m > 0) run<true>(R, out_l, out_r, 0, m);
        run<false>(R, out_l, out_r, m, n);
        q = R.cq; p = R.cp; vprev = R.v1; lp = R.f; nlp = R.nl;
        wa = R.iwa; wb = R.iwb; pluck_n = R.pn;
        rng.s = R.rs;
    }
};

// Lorenz-63 (sigma 10, rho 28, beta 8/3), one RK4 step of h.
inline void lorenz_step(float &x, float &y, float &z, float h) {
    float k1x = 10.0f * (y - x), k1y = x * (28.0f - z) - y, k1z = x * y - (8.0f / 3) * z;
    float x2 = x + 0.5f * h * k1x, y2 = y + 0.5f * h * k1y, z2 = z + 0.5f * h * k1z;
    float k2x = 10.0f * (y2 - x2), k2y = x2 * (28.0f - z2) - y2, k2z = x2 * y2 - (8.0f / 3) * z2;
    float x3 = x + 0.5f * h * k2x, y3 = y + 0.5f * h * k2y, z3 = z + 0.5f * h * k2z;
    float k3x = 10.0f * (y3 - x3), k3y = x3 * (28.0f - z3) - y3, k3z = x3 * y3 - (8.0f / 3) * z3;
    float x4 = x + h * k3x, y4 = y + h * k3y, z4 = z + h * k3z;
    float k4x = 10.0f * (y4 - x4), k4y = x4 * (28.0f - z4) - y4, k4z = x4 * y4 - (8.0f / 3) * z4;
    x += h * (1.0f / 6) * (k1x + 2.0f * k2x + 2.0f * k3x + k4x);
    y += h * (1.0f / 6) * (k1y + 2.0f * k2y + 2.0f * k3y + k4y);
    z += h * (1.0f / 6) * (k1z + 2.0f * k2z + 2.0f * k3z + k4z);
}

struct Harp {
    HarpString st[kHarpStrings];
    HarpRng rng;             // audio noise
    WindRng wrng;            // gusts and the strings' turbulence
    float gust;              // OU gust state, unit variance
    float lx, ly, lz;        // Lorenz state
    float U;                 // wind now, m/s (after gusts)
    float g_now;             // the gust factor now (tests)
    float local_amt;         // each string's own turbulence, fraction of U (0.08; tests set 0)
    float loc_a, loc_s;      // its OU step over kHarpTurn ticks
    int turn;                // which strings update_string runs next
    Svf bodyl[4], bodyr[4];
    float dcxl, dcyl, dcxr, dcyr;
    float env;
    float bufl[kBlock], bufr[kBlock];

    void init() {
        static const float beta[kHarpStrings] = {0.11f, 0.23f, 0.15f, 0.28f, 0.13f, 0.19f, 0.25f, 0.17f};
        static const float pan[kHarpStrings] = {0.15f, 0.85f, 0.3f, 0.7f, 0.42f, 0.58f, 0.05f, 0.95f};
        for (int i = 0; i < kHarpStrings; i++) st[i].init(beta[i], pan[i], 45.0f);
        rng.s = 0x2545F491u;
        wrng.s = 0x6A09E667u;
        gust = 0.0f;
        lx = 1.0f; ly = 1.0f; lz = 20.0f;
        U = 0.0f; g_now = 0.0f;
        local_amt = 0.08f;
        loc_a = 1.0f - one_minus_exp2_neg(1.44269504f * (float)kHarpTurn / (kControlRate * 0.3f));
        loc_s = __builtin_sqrtf(1.0f - loc_a * loc_a);
        turn = 0;
        static const float fl[4] = {190.0f, 460.0f, 1050.0f, 2400.0f}, fr[4] = {215.0f, 520.0f, 1180.0f, 2700.0f};
        for (int k = 0; k < 4; k++) {
            bodyl[k].init(); bodyl[k].set(fl[k], 5.0f);
            bodyr[k].init(); bodyr[k].set(fr[k], 5.0f);
        }
        dcxl = dcyl = dcxr = dcyr = 0.0f;
        env = 0.0f;
    }

    // Once per control tick: wind, then each string's block parameters.
    // A string's tuning and wind, the expensive part (exp, sqrt, atan,
    // sines: ~1,300 cycles on the board), run for one string per tick in
    // turn, so each string every kHarpTurn ticks (375 Hz).
    void update_string(int i, const AeoBus &bus) {
        HarpString &s = st[i];
        const float f1 = HarpString::hz(s.note + bus.bend);
        const float w1 = 2.0f * kPi * f1 / kSampleRate;
        const float t60 = bus.t60 > 0.01f ? bus.t60 : 0.01f;
        s.bc = loss_coef(f1, t60, clampf(bus.bright, 0.01f, 1.0f));
        const float loop = kSampleRate / f1;
        float lb = loop - (float)s.la - lowpass_delay(s.bc, w1);
        if (lb < 2.0f) lb = 2.0f;
        if (lb > (float)(kSegB - 2)) lb = (float)(kSegB - 2);
        s.lb = lb;
        // ring time: g per round trip = 10^(-3 loop / (fs T60))
        s.g = fast_exp2(-9.96578428f * loop / (kSampleRate * t60));
        // wind on this string: its own turbulence (OU, tau 0.3 s, stepped
        // over kHarpTurn ticks), the Strouhal frequency, the lift
        s.local = loc_a * s.local + loc_s * wrng.gauss();
        float u = U * (1.0f + local_amt * s.local);
        if (u < 0.0f) u = 0.0f;
        const float dmm = clampf(bus.d[i], 0.05f, 10.0f);
        float th = 2.0f * kPi * kStrouhal * u / (dmm * 1e-3f) / kSampleRate;
        float hi = 1.0f;  // lift fades out above fs/4
        if (th > kMaxTheta) {
            hi = clampf(2.0f - th / kMaxTheta, 0.0f, 1.0f);
            th = kMaxTheta;
        }
        s.theta = th;
        s.c = fast_sin(th + 1.5707963f);
        s.s = fast_sin(th);
        s.et = kWakeEps * th < kWakeEpsTheta ? kWakeEps * th : kWakeEpsTheta;
        s.k = bus.coupling * th;
        const float u5 = u * 0.2f;
        s.lift = kLiftScale * u5 * u5 * dmm * hi * s.level;
        s.air = 0.004f * bus.air * u5 * u5 * s.level;
    }

    // Once per control tick: the wind, every string's glide, fade and
    // plucks, and one string's tuning and wind (update_string).
    void tick(const AeoBus &bus) {
        const float dt = 1.0f / kControlRate;
        if (!bus.freeze) {
            const float tau = bus.tau > 0.05f ? bus.tau : 0.05f;
            if (bus.weather == WX_GUSTY) {
                // exact OU step: g <- a g + sqrt(1 - a^2) N(0, 1), a = e^{-dt/tau}
                const float a = 1.0f - one_minus_exp2_neg(1.44269504f * dt / tau);
                gust = a * gust + __builtin_sqrtf(1.0f - a * a) * wrng.gauss();
                g_now = bus.sigma * gust;
            } else if (bus.weather == WX_LORENZ) {
                // the attractor's own time scale is ~1; tau seconds per unit
                lorenz_step(lx, ly, lz, dt * 0.5f / tau);
                g_now = bus.sigma * lx * (1.0f / 8.0f);
            } else {
                g_now = 0.0f;
            }
            U = bus.wind * (1.0f + g_now);
            if (U < 0.0f) U = 0.0f;
        }
        const float c_glide = one_pole_coef(bus.glide, kControlRate, kLog2_100);
        const float fade = one_pole_coef(0.02f, kControlRate, kLog2_100);
        for (int i = 0; i < kHarpStrings; i++) {
            HarpString &s = st[i];
            if (bus.note[i] != s.target) {
                s.target = bus.note[i];
                s.set_la();
            }
            s.note += c_glide * (s.target - s.note);
            s.level += fade * ((bus.on[i] ? 1.0f : 0.0f) - s.level);
            if (bus.plucks[i] != s.plucks_seen) {
                s.plucks_seen = bus.plucks[i];
                s.pluck = 0.06f * bus.pluck_amt * s.level;
                s.pluck_n = 96;
            }
        }
        update_string(turn, bus);
        turn = turn + 1 < kHarpStrings ? turn + 1 : 0;
    }

    // One block of stereo output, nominally within [-2/3, 2/3].
    void render(const AeoBus &bus, float *outl, float *outr) {
        for (int i = 0; i < kBlock; i++) bufl[i] = bufr[i] = 0.0f;
        for (int k = 0; k < kHarpStrings; k++)
            if (st[k].level > 1e-4f || st[k].pluck_n > 0 || bus.on[k]) st[k].process(bufl, bufr, kBlock, rng);
        const float body = clampf(bus.body, 0.0f, 1.0f);
        const float ra = 1.0f - 0.99583749f;  // attack 5 ms: 1 - e^{-1/240}
        const float rr = 1.0f - 0.99998958f;  // release 2 s: 1 - e^{-1/96000}
        for (int i = 0; i < kBlock; i++) {
            float l = bufl[i], r = bufr[i];
            if (body > 0.0f) {
                float bl = 0.0f, br = 0.0f;
                for (int k = 0; k < 4; k++) {
                    bl += bodyl[k].process(l, 1);
                    br += bodyr[k].process(r, 1);
                }
                l += body * (0.6f * bl - l);
                r += body * (0.6f * br - r);
            }
            // DC blocker, pole at 0.9995 (2.4 Hz)
            const float yl = l - dcxl + 0.9995f * dcyl;
            dcxl = l; dcyl = yl;
            const float yr = r - dcxr + 0.9995f * dcyr;
            dcxr = r; dcyr = yr;
            const float pk = absf(yl) > absf(yr) ? absf(yl) : absf(yr);
            env += (pk > env ? ra : rr) * (pk - env);
            float gain = kLevelTarget / (env > 1e-5f ? env : 1e-5f);
            if (gain > kLevelMaxGain) gain = kLevelMaxGain;
            outl[i] = soft_clip(gain * yl);
            outr[i] = soft_clip(gain * yr);
        }
    }
};

}  // namespace freak

#endif
