// freak_dsp.h: DSP blocks for the "freak" synth on the Axoloti.
//
// Header-only, C++11, float32, no allocation and no libm. The patch must fit
// in 44 KB of SRAM, and newlib's tanf/expf/sinf would add several KB, so
// fast_exp2 and fast_sin below replace them, with error bounds checked in
// tests/test_freak.cpp. Every struct is a POD (trivially destructible), so it
// can live inside a polyphonic voice (see README: atexit).
//
// Rates: audio 48 kHz in blocks of 16; envelopes, LFO and glide run once per
// block, at 3 kHz.
#ifndef FREAK_DSP_H
#define FREAK_DSP_H

#include <stdint.h>

namespace freak {

const float kSampleRate = 48000.0f;
const int kBlock = 16;
const float kControlRate = kSampleRate / kBlock;  // 3000 Hz
const float kPi = 3.14159265358979f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float absf(float x) { return x < 0.0f ? -x : x; }

// 2^x. Write x = n + f with n = floor(x) and f in [0, 1). Then 2^x = 2^n * 2^f,
// with 2^f from its Taylor series in f ln 2 up to degree 5. The truncation
// error is at most (ln 2)^6 / 720 * 2^1 < 3.1e-4 absolute on [0, 1), i.e.
// < 1.6e-4 relative. Relative to 2^n the error does not depend on n. That is
// 0.0028 semitone in a cutoff frequency.
inline float fast_exp2(float x) {
    x = clampf(x, -126.0f, 126.0f);
    float fl = (float)(int32_t)x;
    if (fl > x) fl -= 1.0f;  // floor for negative x
    const float f = x - fl;
    const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f +
                    f * (0.00961813f + f * 0.00133336f))));
    union { float f; uint32_t u; } s;
    s.u = (uint32_t)((int32_t)fl + 127) << 23;  // 2^n, with n in [-126, 126]
    return p * s.f;
}

// sin(x) for any x. Reduce to [-pi, pi], then to [-pi/2, pi/2] using
// sin(pi - x) = sin x, and use the Taylor polynomial of degree 9. For
// |x| <= pi/2 the error is at most (pi/2)^11 / 11! < 3.6e-6. The polynomial
// can exceed 1 near pi/2 by up to that much, so the result is clamped to
// [-1, 1]. That cannot increase the error, because |sin x| <= 1.
inline float fast_sin(float x) {
    const float twopi = 2.0f * kPi;
    float k = (float)(int32_t)(x / twopi);
    x -= k * twopi;
    if (x > kPi) x -= twopi;
    if (x < -kPi) x += twopi;
    if (x > 0.5f * kPi) x = kPi - x;
    if (x < -0.5f * kPi) x = -kPi - x;
    const float x2 = x * x;
    const float s = x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
    return clampf(s, -1.0f, 1.0f);
}

// tan(pi x) for x in (0, 0.5), computed as sin / cos.
inline float fast_tan_pi(float x) {
    const float t = kPi * x;
    return fast_sin(t) / fast_sin(0.5f * kPi - t);
}

// 1 - 2^(-y) for y >= 0.
// A relative error d in the step factor 1 - c compounds over a segment of
// n = log2(ratio)/y steps to about n d. So 1 - fast_exp2(-y) alone, with
// d ~ 1e-4, puts a 1 s envelope several per cent off.
//  - For y < 0.5: use the series 1 - e^-z = z - z^2/2 + z^3/6 - z^4/24 + z^5/120
//    with z = y ln 2 < 0.35. Its error is at most z^6/720 < 2.5e-6, and since
//    1 - c >= e^-0.35 = 0.70, n d <= log2(ratio) (ln 2)^6 y^5 / 504. For
//    ratio 100 and y = 0.5 that is under 5e-5, and it vanishes as y -> 0.
//  - For y >= 0.5 a segment is at most log2(ratio)/0.5 < 14 steps long, so
//    n d < 14 * 1.6e-4 = 2.2e-3.
inline float one_minus_exp2_neg(float y) {
    if (y < 0.5f) {
        const float z = 0.69314718f * y;
        return z * (1.0f - z * (0.5f - z * (1.0f / 6 - z * (1.0f / 24 - z * (1.0f / 120)))));
    }
    return 1.0f - fast_exp2(-y);
}

// Coefficient c of x += c (target - x) such that n = t fs steps leave the
// fraction 1/ratio of the distance: (1 - c)^n = 1/ratio, so
// c = 1 - ratio^(-1/(t fs)) = 1 - 2^(-log2(ratio)/(t fs)).
inline float one_pole_coef(float t, float fs, float log2_ratio) {
    if (t <= 0.0f) return 1.0f;
    return one_minus_exp2_neg(log2_ratio / (t * fs));
}
const float kLog2_100 = 6.64385619f;  // 99 % of the way
const float kLog2_6 = 2.58496250f;    // attack: target 1.2, 1/(1 - 1/1.2) = 6

// ADSR with exponential (RC-like) segments, at the control rate.
//  - Attack aims at 1.2 and switches to decay on reaching 1; from 0 that takes
//    exactly the attack time (ratio 6, see kLog2_6). A retrigger starts from
//    the current level, so it is shorter.
//  - Decay and release cover 99 % of their distance in the set time.
//  - cycle: decay falls towards 0 instead of the sustain level, and the attack
//    restarts below 0.001 while the gate is held (a looping AD envelope).
struct Env {
    enum { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
    float x;
    int stage;
    bool gate;

    void init() { x = 0.0f; stage = IDLE; gate = false; }

    // a, d, r in seconds; s in [0, 1].
    float tick(bool g, float a, float d, float s, float r, bool cycle) {
        if (g && !gate) stage = ATTACK;
        if (!g && gate) stage = RELEASE;
        gate = g;
        switch (stage) {
        case ATTACK:
            x += one_pole_coef(a, kControlRate, kLog2_6) * (1.2f - x);
            if (x >= 1.0f) { x = 1.0f; stage = DECAY; }
            break;
        case DECAY: {
            const float target = cycle ? 0.0f : s;
            x += one_pole_coef(d, kControlRate, kLog2_100) * (target - x);
            if (cycle) {
                if (x < 1e-3f) stage = ATTACK;
            } else if (absf(x - s) < 1e-4f) {
                x = s;
                stage = SUSTAIN;
            }
            break;
        }
        case SUSTAIN:
            x = s;
            if (cycle) stage = DECAY;
            break;
        case RELEASE:
            x += one_pole_coef(r, kControlRate, kLog2_100) * (0.0f - x);
            if (x < 1e-5f) { x = 0.0f; stage = IDLE; }
            break;
        default:
            x = 0.0f;
        }
        return x;
    }
};

// State-variable filter, trapezoidal (TPT) form, after Zavalishin and
// A. Simper, "Linear trapezoidal integrated SVF". For g > 0 and k > 0 it is
// the bilinear transform of a stable analog 2-pole, and the bilinear
// transform maps the open left half-plane into the open unit disc, so the
// filter is stable for every setting. Coefficients are set once per block.
// The band-pass output is normalised (k * bp, unit gain at the peak), so its
// level does not jump with resonance.
struct Svf {
    float ic1, ic2, a1, a2, a3, k;

    void init() { ic1 = ic2 = 0.0f; set(1000.0f, 0.7071f); }

    void set(float fc, float q) {
        fc = clampf(fc, 10.0f, 0.45f * kSampleRate);  // tan(pi/2) is the pole
        q = clampf(q, 0.5f, 25.0f);
        const float g = fast_tan_pi(fc / kSampleRate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // mode 0 low-pass, 1 band-pass, 2 high-pass
    float process(float v0, int mode) {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (mode == 0) return v2;
        if (mode == 1) return k * v1;
        return v0 - k * v1 - v2;
    }
};

// Low-frequency oscillator at the control rate, output in [-1, 1].
// Waves: 0 sine, 1 triangle, 2 saw up, 3 square, 4 sample & hold.
struct Lfo {
    float phase, held;
    uint32_t rng;

    void init() { phase = 0.0f; held = 0.0f; rng = 0x2545F491u; }

    float random() {  // xorshift32, uniform in [-1, 1)
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float)(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    float tick(float hz, int wave) {
        phase += hz / kControlRate;
        if (phase >= 1.0f) {
            phase -= (float)(int32_t)phase;
            held = random();
        }
        switch (wave) {
        case 0: return fast_sin(2.0f * kPi * phase);
        case 1: return 4.0f * absf(phase - 0.5f) - 1.0f;
        case 2: return 2.0f * phase - 1.0f;
        case 3: return phase < 0.5f ? 1.0f : -1.0f;
        default: return held;
        }
    }
};

// Portamento: one pole on pitch (semitones), 99 % of an interval in time t.
struct Glide {
    float p;
    bool primed;

    void init() { p = 0.0f; primed = false; }

    float tick(float target, float t) {
        if (!primed || t <= 0.0f) {
            p = target;
            primed = true;
        } else {
            p += one_pole_coef(t, kControlRate, kLog2_100) * (target - p);
        }
        return p;
    }
};

// Feedback delay line on caller-provided memory (SDRAM), read with linear
// interpolation, with a one-pole low-pass in the loop. With fb < 1 and a
// low-pass gain of at most 1 the loop gain is below 1, so the output stays
// bounded (by max|in| / (1 - fb)).
struct Delay {
    float *buf;
    int32_t n, w;
    float lp, t;  // t: smoothed delay in samples

    void init(float *mem, int32_t len) {
        buf = mem;
        n = len;
        w = 0;
        lp = 0.0f;
        t = 0.25f * kSampleRate;
        if (buf)
            for (int32_t i = 0; i < n; i++) buf[i] = 0.0f;
    }

    // delay in samples (smoothed per sample, tape-like when moved); returns wet
    float process(float in, float delay, float fb) {
        if (!buf) return 0.0f;
        t += 0.0005f * (clampf(delay, 1.0f, (float)(n - 2)) - t);
        float rp = (float)w - t;
        if (rp < 0.0f) rp += (float)n;
        int32_t i = (int32_t)rp;
        const float f = rp - (float)i;
        int32_t j = i + 1;
        if (j >= n) j = 0;
        const float y = buf[i] + f * (buf[j] - buf[i]);
        lp += 0.35f * (y - lp);
        buf[w] = in + clampf(fb, 0.0f, 0.95f) * lp;
        if (++w >= n) w = 0;
        return y;
    }
};

// Cubic soft clip: identity near 0, continuous with zero slope at |x| = 1,
// output bounded by 2/3.
inline float soft_clip(float x) {
    x = clampf(x, -1.0f, 1.0f);
    return x - x * x * x * (1.0f / 3.0f);
}

}  // namespace freak

#endif
