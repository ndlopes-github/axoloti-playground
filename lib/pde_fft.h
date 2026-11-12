// pde_fft.h: FFTs for the soliton patch's spectral solver
// (lib/soliton_field.h). Header-only, C++11, float32, no allocation, no libm.
//
// Conventions (checked against a direct DFT in tests/test_soliton.cpp):
//  - cfft_fwd(x, n): X_k = sum_j x_j exp(-2 pi i jk/n), unnormalised, in
//    place. cfft(x, n, inverse) adds the inverse (sign +, unnormalised) as
//    conj(DFT(conj x)); the solver folds those conjugations into its own
//    loops instead. n a power of 2, 2 <= n <= 256.
//  - rfft(x, X, n):  X_k = (1/n) sum_j x_j exp(-2 pi i jk/n), k = 0..n/2, so
//    that x_j = sum_k X_k exp(2 pi i jk/n) over all k, with X_{-k} = conj X_k.
//    These X_k are the Fourier amplitudes of u(y) = sum_k X_k e^{iky} sampled
//    at y_j = 2 pi j/n.
//  - irfft(X, x, n): the inverse of rfft, i.e. x_j = sum_k X_k e^{iky_j} with
//    the imaginary parts of X_0 and X_{n/2} ignored. n a power of 2,
//    4 <= n <= 256.
// The real transforms use the standard half-length trick: the n reals are
// packed as n/2 complex numbers, transformed, and split.
//
// Complex FFTs of 64 and 128 points (the sizes the solver uses) run the
// radix-8 code of CMSIS-DSP 1.6 (third_party/cmsis_fft); other sizes a plain
// radix-2 loop, kept for the tests. On the board the radix-2 loop took
// 22,000 cycles for 128 points (README).
//
// Twiddles, built once by fft_init() into a namespace-scope POD (bss, so no
// constructor and no rodata in the patch's SRAM1): W^j = exp(-2 pi i j/256),
// j = 0..127, for the radix-2 loop and the real-transform splits, and
// CMSIS's tables (cos, sin)(2 pi i/n), i < n, for n = 64 and 128. Each sine
// is a degree-11 Taylor polynomial on [0, pi/4] (truncation error
// < (pi/4)^13/13! < 7e-11), the cosine is then sqrt(1 - s^2), and the other
// octants follow by symmetry (sincos_unit), so every twiddle is exact to
// float rounding.
#ifndef PDE_FFT_H
#define PDE_FFT_H

#include <stdint.h>

#include "../third_party/cmsis_fft/cmsis_cfft.h"

// The bit reversal: on the board the firmware's assembly version (CMSIS
// arm_bitreversal2.S, in flash; the C version took 3,000 cycles for 128
// points), on the host the vendored C version.
#if defined(__arm__)
extern "C" void arm_bitreversal_32(uint32_t *pSrc, const uint16_t bitRevLen, const uint16_t *pBitRevTable);
#endif

namespace freak {

inline void bitrev32(void *x, uint16_t len, const uint16_t *table) {
#if defined(__arm__)
    ::arm_bitreversal_32((uint32_t *)x, len, table);
#else
    cmsis_fft::arm_bitreversal_32((uint32_t *)x, len, table);
#endif
}

struct Cplx {
    float re, im;
};

const int kFftTable = 256;  // largest transform, real or complex

struct FftTables {
    Cplx w[kFftTable / 2];  // W^j = exp(-2 pi i j / kFftTable)
    float tw64[128], tw128[256];  // CMSIS: cos, sin of 2 pi i / n
    cmsis_fft::arm_cfft_instance_f32 s128;
    bool ready;
};
static FftTables g_fft;

// sin x for 0 <= x <= pi/4, Taylor to degree 11.
inline float sin_octant(float x) {
    const float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 +
               x2 * (1.0f / 362880 + x2 * (-1.0f / 39916800))))));
}

// cos x and sin x for any x, with c^2 + s^2 = 1 to float rounding (one of
// the two comes from sqrt(1 - other^2)). The linear-flow factors of the
// conservative equations need that: a modulus 1 + e per step compounds to
// (1 + e)^steps in the energy. x is reduced to [-pi, pi] in float, so for
// |x| ~ 1000 the angle is off by ~1e-4 rad; only the phase is affected.
inline void sincos_unit(float x, float &c, float &s) {
    const float twopi = 6.28318530717959f, pi = 3.14159265358979f;
    float k = x * (1.0f / twopi);
    k = (float)(int32_t)(k + (k >= 0.0f ? 0.5f : -0.5f));  // round to nearest
    float a = x - k * twopi;
    const bool neg = a < 0.0f;
    if (neg) a = -a;
    if (a > pi) a = pi;  // rounding at the edge
    const bool back = a > 0.5f * pi;  // second quadrant: cos < 0
    if (back) a = pi - a;
    if (a <= 0.25f * pi) {
        s = sin_octant(a);
        c = __builtin_sqrtf(1.0f - s * s);
    } else {
        c = sin_octant(0.5f * pi - a);
        s = __builtin_sqrtf(1.0f - c * c);
    }
    if (back) c = -c;
    if (neg) s = -s;
}

// cos and sin of 2 pi j / n, exactly reduced (j, n integers, 0 <= j < n).
inline void sincos_frac(int j, int n, float &c, float &s) {
    // reduce j/n to [0, 1/8] with the symmetries of the circle
    int q = 0;  // octant
    j = j % n;
    int a = 8 * j;  // angle in units of 2 pi / (8 n): octant = a / n
    q = a / n;
    const int r = a - q * n;  // remainder in [0, n)
    const float t = 0.78539816339745f * (float)r / (float)n;  // in [0, pi/4)
    const float so = sin_octant(t), co = __builtin_sqrtf(1.0f - so * so);
    const float sc = sin_octant(0.78539816339745f - t), cc = __builtin_sqrtf(1.0f - sc * sc);
    // angle = q pi/4 + t
    switch (q) {
    case 0: c = co; s = so; break;
    case 1: c = sc; s = cc; break;    // pi/4 + t = pi/2 - (pi/4 - t)
    case 2: c = -so; s = co; break;   // pi/2 + t
    case 3: c = -cc; s = sc; break;   // pi - (pi/4 - t)
    case 4: c = -co; s = -so; break;  // pi + t
    case 5: c = -sc; s = -cc; break;
    case 6: c = so; s = -co; break;   // 3 pi/2 + t
    default: c = cc; s = -sc; break;  // 2 pi - (pi/4 - t)
    }
}

inline void fft_init() {
    if (g_fft.ready) return;
    float c, s;
    for (int j = 0; j < kFftTable / 2; j++) {
        sincos_frac(j, kFftTable, c, s);
        g_fft.w[j].re = c;
        g_fft.w[j].im = -s;
    }
    for (int i = 0; i < 64; i++) {
        sincos_frac(i, 64, c, s);
        g_fft.tw64[2 * i] = c;
        g_fft.tw64[2 * i + 1] = s;
    }
    for (int i = 0; i < 128; i++) {
        sincos_frac(i, 128, c, s);
        g_fft.tw128[2 * i] = c;
        g_fft.tw128[2 * i + 1] = s;
    }
    g_fft.s128.fftLen = 128;
    g_fft.s128.pTwiddle = g_fft.tw128;
    g_fft.s128.pBitRevTable = cmsis_fft::armBitRevIndexTable128;
    g_fft.s128.bitRevLength = 208;
    g_fft.ready = true;
}

// The 128-point FFT in four pieces, so the solver can spread one transform
// over several control ticks: pass, half 0, half 1, bitrev (in that order).
inline void cfft128_pass(Cplx *x) { cmsis_fft::arm_cfft_radix8by2_pass_f32(&g_fft.s128, (float *)x); }
inline void cfft128_half(Cplx *x, int half) { cmsis_fft::arm_cfft_radix8by2_half_f32(&g_fft.s128, (float *)x, half); }
inline void cfft128_bitrev(Cplx *x) {
    bitrev32(x, 208, cmsis_fft::armBitRevIndexTable128);
}

// In-place forward FFT, unnormalised (see the header comment).
__attribute__((noinline)) inline void cfft_fwd(Cplx *x, int n) {
    if (n == 64) {
        cmsis_fft::arm_radix8_butterfly_f32((float *)x, 64, g_fft.tw64, 1);
        bitrev32(x, 56, cmsis_fft::armBitRevIndexTable64);
        return;
    }
    if (n == 128) {
        cfft128_pass(x);
        cfft128_half(x, 0);
        cfft128_half(x, 1);
        bitrev32(x, 208, cmsis_fft::armBitRevIndexTable128);
        return;
    }
    // Other sizes (tests): iterative radix-2 decimation in time.
    for (int i = 1, j = 0; i < n; i++) {  // bit-reversal permutation
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            const Cplx t = x[i];
            x[i] = x[j];
            x[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len >> 1, stride = kFftTable / len;  // W_len^k = W^(k stride)
        for (int k = 0; k < half; k++) {
            const float wr = g_fft.w[k * stride].re, wi = g_fft.w[k * stride].im;
            for (int i = k; i < n; i += len) {
                const float ar = x[i].re, ai = x[i].im, br = x[i + half].re, bi = x[i + half].im;
                const float tr = wr * br - wi * bi, ti = wr * bi + wi * br;
                x[i + half].re = ar - tr;
                x[i + half].im = ai - ti;
                x[i].re = ar + tr;
                x[i].im = ai + ti;
            }
        }
    }
}

// Forward or inverse, unnormalised: inverse = conj(DFT(conj x)).
inline void cfft(Cplx *x, int n, bool inverse) {
    if (inverse)
        for (int j = 0; j < n; j++) x[j].im = -x[j].im;
    cfft_fwd(x, n);
    if (inverse)
        for (int j = 0; j < n; j++) x[j].im = -x[j].im;
}

// Real forward transform of x[0..n-1] into X[0..n/2]. z: scratch of n/2.
inline void rfft(const float *x, Cplx *X, int n, Cplx *z) {
    const int h = n >> 1, stride = kFftTable / n;
    for (int j = 0; j < h; j++) {
        z[j].re = x[2 * j];
        z[j].im = x[2 * j + 1];
    }
    cfft_fwd(z, h);
    const float s = 1.0f / (float)n, s2 = 0.5f * s;
    // Z_k = A_k + i B_k with A, B the DFTs of the even and odd samples:
    // A_k = (Z_k + conj Z_{h-k})/2, B_k = (Z_k - conj Z_{h-k})/(2i), and
    // X_k = A_k + W_n^k B_k. Pairwise: A_{h-k} = conj A_k, B_{h-k} = conj B_k
    // and W^{h-k} = -conj W^k, so with T = W^k B_k, X_k = A_k + T and
    // X_{h-k} = conj(A_k - T): one set of loads for both.
    X[0].re = (z[0].re + z[0].im) * s;
    X[0].im = 0.0f;
    X[h].re = (z[0].re - z[0].im) * s;
    X[h].im = 0.0f;
    for (int k = 1; 2 * k <= h; k++) {
        const int m = h - k;
        const float pr = z[k].re, pi = z[k].im, qr = z[m].re, qi = -z[m].im;  // Z_k, conj Z_{h-k}
        const float ar = pr + qr, ai = pi + qi;    // 2 A_k
        const float br = pi - qi, bi = -(pr - qr);  // 2 B_k = (Z_k - conj Z_{h-k})/i
        const float wr = g_fft.w[k * stride].re, wi = g_fft.w[k * stride].im;
        const float tr = wr * br - wi * bi, ti = wr * bi + wi * br;  // 2 T
        X[k].re = (ar + tr) * s2;
        X[k].im = (ai + ti) * s2;
        if (m != k) {
            X[m].re = (ar - tr) * s2;
            X[m].im = -(ai - ti) * s2;
        }
    }
}

// Real inverse transform of X[0..n/2] into x[0..n-1]. z: scratch of n/2.
// From X_k = A_k + W^k B_k and X_{k+h} = A_k - W^k B_k = conj X_{h-k}:
// 2 A_k = X_k + conj X_{h-k}, 2 B_k = (X_k - conj X_{h-k}) W^-k. Then the
// inverse DFT of Z = 2(A + iB) over h points gives x_{2j} + i x_{2j+1}; it
// is computed as conj(DFT(conj Z)), the conjugations folded into the pre-
// and post-processing. Pairwise: A_{h-k} = conj A_k and B_{h-k} = conj B_k,
// so conj Z_k = conj(2A) - i conj(2B) and conj Z_{h-k} = 2A - i 2B. In
// three parts (irfft_pre, a forward FFT of z over h points, irfft_post) so
// that the solver can spread it over ticks.
inline void irfft_pre(const Cplx *X, int n, Cplx *z) {
    const int h = n >> 1, stride = kFftTable / n;
    {  // k = 0, with X_0 and X_h real
        const float a = X[0].re + X[h].re, b = X[0].re - X[h].re;
        z[0].re = a;   // conj(a + i b), a and b real
        z[0].im = -b;
    }
    for (int k = 1; 2 * k <= h; k++) {
        const int m = h - k;
        const float pr = X[k].re, pi = X[k].im, qr = X[m].re, qi = -X[m].im;  // X_k, conj X_{h-k}
        const float ar = pr + qr, ai = pi + qi;  // 2 A_k
        const float dr = pr - qr, di = pi - qi;
        const float wr = g_fft.w[k * stride].re, wi = -g_fft.w[k * stride].im;  // W^-k
        const float br = dr * wr - di * wi, bi = dr * wi + di * wr;  // 2 B_k
        z[k].re = ar - bi;     // conj Z_k = (ar - bi) - i (ai + br)
        z[k].im = -(ai + br);
        if (m != k) {
            z[m].re = ar + bi;  // conj Z_{h-k} = (ar + bi) + i (ai - br)
            z[m].im = ai - br;
        }
    }
}
inline void irfft_post(const Cplx *z, float *x, int n) {
    for (int j = 0; j < n / 2; j++) {
        x[2 * j] = z[j].re;
        x[2 * j + 1] = -z[j].im;
    }
}
inline void irfft(const Cplx *X, float *x, int n, Cplx *z) {
    irfft_pre(X, n, z);
    cfft_fwd(z, n >> 1);
    irfft_post(z, x, n);
}

}  // namespace freak

#endif
