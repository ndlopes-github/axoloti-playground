// cmsis_cfft.h: the radix-8 complex FFT of CMSIS-DSP V1.6.0 (18 March 2019),
// as shipped in the Ksoloti 1.1.0 tree (CMSIS/DSP/Source), for lengths 64 and
// 128 only. The firmware has no float FFT, and its arm_* symbols would
// clash with a second copy, so the code lives in namespace cmsis_fft with
// internal linkage. Changes from upstream: this wrapper, `static`, the
// namespace, the twiddles built at run time (cmsis_init, into bss instead
// of 1.5 KB of rodata in patch SRAM), and only the two bit-reversal tables
// needed. The function bodies are unchanged. See README.md here.
//
// Copyright (C) 2010-2019 ARM Limited or its affiliates. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Licensed under the Apache License, Version 2.0 (the License); you may not
// use this file except in compliance with the License. You may obtain a copy
// of the License at www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an AS IS BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.
#ifndef CMSIS_CFFT_H
#define CMSIS_CFFT_H

#include <stdint.h>

namespace cmsis_fft {

typedef float float32_t;

typedef struct {
    uint16_t fftLen;
    const float32_t *pTwiddle;
    const uint16_t *pBitRevTable;
    uint16_t bitRevLength;
} arm_cfft_instance_f32;

// ---- arm_cfft_radix8_f32.c
static void arm_radix8_butterfly_f32(
  float32_t * pSrc,
  uint16_t fftLen,
  const float32_t * pCoef,
  uint16_t twidCoefModifier)
{
   uint32_t ia1, ia2, ia3, ia4, ia5, ia6, ia7;
   uint32_t i1, i2, i3, i4, i5, i6, i7, i8;
   uint32_t id;
   uint32_t n1, n2, j;

   float32_t r1, r2, r3, r4, r5, r6, r7, r8;
   float32_t t1, t2;
   float32_t s1, s2, s3, s4, s5, s6, s7, s8;
   float32_t p1, p2, p3, p4;
   float32_t co2, co3, co4, co5, co6, co7, co8;
   float32_t si2, si3, si4, si5, si6, si7, si8;
   const float32_t C81 = 0.70710678118f;

   n2 = fftLen;

   do
   {
      n1 = n2;
      n2 = n2 >> 3;
      i1 = 0;

      do
      {
         i2 = i1 + n2;
         i3 = i2 + n2;
         i4 = i3 + n2;
         i5 = i4 + n2;
         i6 = i5 + n2;
         i7 = i6 + n2;
         i8 = i7 + n2;
         r1 = pSrc[2 * i1] + pSrc[2 * i5];
         r5 = pSrc[2 * i1] - pSrc[2 * i5];
         r2 = pSrc[2 * i2] + pSrc[2 * i6];
         r6 = pSrc[2 * i2] - pSrc[2 * i6];
         r3 = pSrc[2 * i3] + pSrc[2 * i7];
         r7 = pSrc[2 * i3] - pSrc[2 * i7];
         r4 = pSrc[2 * i4] + pSrc[2 * i8];
         r8 = pSrc[2 * i4] - pSrc[2 * i8];
         t1 = r1 - r3;
         r1 = r1 + r3;
         r3 = r2 - r4;
         r2 = r2 + r4;
         pSrc[2 * i1] = r1 + r2;
         pSrc[2 * i5] = r1 - r2;
         r1 = pSrc[2 * i1 + 1] + pSrc[2 * i5 + 1];
         s5 = pSrc[2 * i1 + 1] - pSrc[2 * i5 + 1];
         r2 = pSrc[2 * i2 + 1] + pSrc[2 * i6 + 1];
         s6 = pSrc[2 * i2 + 1] - pSrc[2 * i6 + 1];
         s3 = pSrc[2 * i3 + 1] + pSrc[2 * i7 + 1];
         s7 = pSrc[2 * i3 + 1] - pSrc[2 * i7 + 1];
         r4 = pSrc[2 * i4 + 1] + pSrc[2 * i8 + 1];
         s8 = pSrc[2 * i4 + 1] - pSrc[2 * i8 + 1];
         t2 = r1 - s3;
         r1 = r1 + s3;
         s3 = r2 - r4;
         r2 = r2 + r4;
         pSrc[2 * i1 + 1] = r1 + r2;
         pSrc[2 * i5 + 1] = r1 - r2;
         pSrc[2 * i3]     = t1 + s3;
         pSrc[2 * i7]     = t1 - s3;
         pSrc[2 * i3 + 1] = t2 - r3;
         pSrc[2 * i7 + 1] = t2 + r3;
         r1 = (r6 - r8) * C81;
         r6 = (r6 + r8) * C81;
         r2 = (s6 - s8) * C81;
         s6 = (s6 + s8) * C81;
         t1 = r5 - r1;
         r5 = r5 + r1;
         r8 = r7 - r6;
         r7 = r7 + r6;
         t2 = s5 - r2;
         s5 = s5 + r2;
         s8 = s7 - s6;
         s7 = s7 + s6;
         pSrc[2 * i2]     = r5 + s7;
         pSrc[2 * i8]     = r5 - s7;
         pSrc[2 * i6]     = t1 + s8;
         pSrc[2 * i4]     = t1 - s8;
         pSrc[2 * i2 + 1] = s5 - r7;
         pSrc[2 * i8 + 1] = s5 + r7;
         pSrc[2 * i6 + 1] = t2 - r8;
         pSrc[2 * i4 + 1] = t2 + r8;

         i1 += n1;
      } while (i1 < fftLen);

      if (n2 < 8)
         break;

      ia1 = 0;
      j = 1;

      do
      {
         /*  index calculation for the coefficients */
         id  = ia1 + twidCoefModifier;
         ia1 = id;
         ia2 = ia1 + id;
         ia3 = ia2 + id;
         ia4 = ia3 + id;
         ia5 = ia4 + id;
         ia6 = ia5 + id;
         ia7 = ia6 + id;

         co2 = pCoef[2 * ia1];
         co3 = pCoef[2 * ia2];
         co4 = pCoef[2 * ia3];
         co5 = pCoef[2 * ia4];
         co6 = pCoef[2 * ia5];
         co7 = pCoef[2 * ia6];
         co8 = pCoef[2 * ia7];
         si2 = pCoef[2 * ia1 + 1];
         si3 = pCoef[2 * ia2 + 1];
         si4 = pCoef[2 * ia3 + 1];
         si5 = pCoef[2 * ia4 + 1];
         si6 = pCoef[2 * ia5 + 1];
         si7 = pCoef[2 * ia6 + 1];
         si8 = pCoef[2 * ia7 + 1];

         i1 = j;

         do
         {
            /*  index calculation for the input */
            i2 = i1 + n2;
            i3 = i2 + n2;
            i4 = i3 + n2;
            i5 = i4 + n2;
            i6 = i5 + n2;
            i7 = i6 + n2;
            i8 = i7 + n2;
            r1 = pSrc[2 * i1] + pSrc[2 * i5];
            r5 = pSrc[2 * i1] - pSrc[2 * i5];
            r2 = pSrc[2 * i2] + pSrc[2 * i6];
            r6 = pSrc[2 * i2] - pSrc[2 * i6];
            r3 = pSrc[2 * i3] + pSrc[2 * i7];
            r7 = pSrc[2 * i3] - pSrc[2 * i7];
            r4 = pSrc[2 * i4] + pSrc[2 * i8];
            r8 = pSrc[2 * i4] - pSrc[2 * i8];
            t1 = r1 - r3;
            r1 = r1 + r3;
            r3 = r2 - r4;
            r2 = r2 + r4;
            pSrc[2 * i1] = r1 + r2;
            r2 = r1 - r2;
            s1 = pSrc[2 * i1 + 1] + pSrc[2 * i5 + 1];
            s5 = pSrc[2 * i1 + 1] - pSrc[2 * i5 + 1];
            s2 = pSrc[2 * i2 + 1] + pSrc[2 * i6 + 1];
            s6 = pSrc[2 * i2 + 1] - pSrc[2 * i6 + 1];
            s3 = pSrc[2 * i3 + 1] + pSrc[2 * i7 + 1];
            s7 = pSrc[2 * i3 + 1] - pSrc[2 * i7 + 1];
            s4 = pSrc[2 * i4 + 1] + pSrc[2 * i8 + 1];
            s8 = pSrc[2 * i4 + 1] - pSrc[2 * i8 + 1];
            t2 = s1 - s3;
            s1 = s1 + s3;
            s3 = s2 - s4;
            s2 = s2 + s4;
            r1 = t1 + s3;
            t1 = t1 - s3;
            pSrc[2 * i1 + 1] = s1 + s2;
            s2 = s1 - s2;
            s1 = t2 - r3;
            t2 = t2 + r3;
            p1 = co5 * r2;
            p2 = si5 * s2;
            p3 = co5 * s2;
            p4 = si5 * r2;
            pSrc[2 * i5]     = p1 + p2;
            pSrc[2 * i5 + 1] = p3 - p4;
            p1 = co3 * r1;
            p2 = si3 * s1;
            p3 = co3 * s1;
            p4 = si3 * r1;
            pSrc[2 * i3]     = p1 + p2;
            pSrc[2 * i3 + 1] = p3 - p4;
            p1 = co7 * t1;
            p2 = si7 * t2;
            p3 = co7 * t2;
            p4 = si7 * t1;
            pSrc[2 * i7]     = p1 + p2;
            pSrc[2 * i7 + 1] = p3 - p4;
            r1 = (r6 - r8) * C81;
            r6 = (r6 + r8) * C81;
            s1 = (s6 - s8) * C81;
            s6 = (s6 + s8) * C81;
            t1 = r5 - r1;
            r5 = r5 + r1;
            r8 = r7 - r6;
            r7 = r7 + r6;
            t2 = s5 - s1;
            s5 = s5 + s1;
            s8 = s7 - s6;
            s7 = s7 + s6;
            r1 = r5 + s7;
            r5 = r5 - s7;
            r6 = t1 + s8;
            t1 = t1 - s8;
            s1 = s5 - r7;
            s5 = s5 + r7;
            s6 = t2 - r8;
            t2 = t2 + r8;
            p1 = co2 * r1;
            p2 = si2 * s1;
            p3 = co2 * s1;
            p4 = si2 * r1;
            pSrc[2 * i2]     = p1 + p2;
            pSrc[2 * i2 + 1] = p3 - p4;
            p1 = co8 * r5;
            p2 = si8 * s5;
            p3 = co8 * s5;
            p4 = si8 * r5;
            pSrc[2 * i8]     = p1 + p2;
            pSrc[2 * i8 + 1] = p3 - p4;
            p1 = co6 * r6;
            p2 = si6 * s6;
            p3 = co6 * s6;
            p4 = si6 * r6;
            pSrc[2 * i6]     = p1 + p2;
            pSrc[2 * i6 + 1] = p3 - p4;
            p1 = co4 * t1;
            p2 = si4 * t2;
            p3 = co4 * t2;
            p4 = si4 * t1;
            pSrc[2 * i4]     = p1 + p2;
            pSrc[2 * i4 + 1] = p3 - p4;

            i1 += n1;
         } while (i1 < fftLen);

         j++;
      } while (j < n2);

      twidCoefModifier <<= 3;
   } while (n2 > 7);
}

// ---- arm_cfft_f32.c (radix 8 by 2)
// Split here into the radix-2 pass (this function, the loop of upstream
// arm_cfft_radix8by2_f32, unchanged) and the two radix-8 calls (below), so
// the solver can run them in different control ticks.
static void arm_cfft_radix8by2_pass_f32 (const arm_cfft_instance_f32 * S, float32_t * p1)
{
  uint32_t    L  = S->fftLen;
  float32_t * pCol1, * pCol2, * pMid1, * pMid2;
  float32_t * p2 = p1 + L;
  const float32_t * tw = (float32_t *) S->pTwiddle;
  float32_t t1[4], t2[4], t3[4], t4[4], twR, twI;
  float32_t m0, m1, m2, m3;
  uint32_t l;

  pCol1 = p1;
  pCol2 = p2;

  /* Define new length */
  L >>= 1;

  /* Initialize mid pointers */
  pMid1 = p1 + L;
  pMid2 = p2 + L;

  /* do two dot Fourier transform */
  for (l = L >> 2; l > 0; l-- )
  {
    t1[0] = p1[0];
    t1[1] = p1[1];
    t1[2] = p1[2];
    t1[3] = p1[3];

    t2[0] = p2[0];
    t2[1] = p2[1];
    t2[2] = p2[2];
    t2[3] = p2[3];

    t3[0] = pMid1[0];
    t3[1] = pMid1[1];
    t3[2] = pMid1[2];
    t3[3] = pMid1[3];

    t4[0] = pMid2[0];
    t4[1] = pMid2[1];
    t4[2] = pMid2[2];
    t4[3] = pMid2[3];

    *p1++ = t1[0] + t2[0];
    *p1++ = t1[1] + t2[1];
    *p1++ = t1[2] + t2[2];
    *p1++ = t1[3] + t2[3];    /* col 1 */

    t2[0] = t1[0] - t2[0];
    t2[1] = t1[1] - t2[1];
    t2[2] = t1[2] - t2[2];
    t2[3] = t1[3] - t2[3];    /* for col 2 */

    *pMid1++ = t3[0] + t4[0];
    *pMid1++ = t3[1] + t4[1];
    *pMid1++ = t3[2] + t4[2];
    *pMid1++ = t3[3] + t4[3]; /* col 1 */

    t4[0] = t4[0] - t3[0];
    t4[1] = t4[1] - t3[1];
    t4[2] = t4[2] - t3[2];
    t4[3] = t4[3] - t3[3];    /* for col 2 */

    twR = *tw++;
    twI = *tw++;

    /* multiply by twiddle factors */
    m0 = t2[0] * twR;
    m1 = t2[1] * twI;
    m2 = t2[1] * twR;
    m3 = t2[0] * twI;

    /* R  =  R  *  Tr - I * Ti */
    *p2++ = m0 + m1;
    /* I  =  I  *  Tr + R * Ti */
    *p2++ = m2 - m3;

    /* use vertical symmetry */
    /*  0.9988 - 0.0491i <==> -0.0491 - 0.9988i */
    m0 = t4[0] * twI;
    m1 = t4[1] * twR;
    m2 = t4[1] * twI;
    m3 = t4[0] * twR;

    *pMid2++ = m0 - m1;
    *pMid2++ = m2 + m3;

    twR = *tw++;
    twI = *tw++;

    m0 = t2[2] * twR;
    m1 = t2[3] * twI;
    m2 = t2[3] * twR;
    m3 = t2[2] * twI;

    *p2++ = m0 + m1;
    *p2++ = m2 - m3;

    m0 = t4[2] * twI;
    m1 = t4[3] * twR;
    m2 = t4[3] * twI;
    m3 = t4[2] * twR;

    *pMid2++ = m0 - m1;
    *pMid2++ = m2 + m3;
  }

  (void)pCol1; (void)pCol2;
}

// The rest of upstream arm_cfft_radix8by2_f32: a radix-8 FFT of each half.
static void arm_cfft_radix8by2_half_f32 (const arm_cfft_instance_f32 * S, float32_t * p1, int half)
{
  arm_radix8_butterfly_f32 (p1 + half * S->fftLen, S->fftLen >> 1, (float32_t *) S->pTwiddle, 2U);
}

// ---- arm_bitreversal2.c
static void arm_bitreversal_32(
        uint32_t *pSrc, 
  const uint16_t bitRevLen, 
  const uint16_t *pBitRevTab)
{
  uint32_t a, b, i, tmp;

  for (i = 0; i < bitRevLen; )
  {
     a = pBitRevTab[i    ] >> 2;
     b = pBitRevTab[i + 1] >> 2;

     //real
     tmp = pSrc[a];
     pSrc[a] = pSrc[b];
     pSrc[b] = tmp;

     //complex
     tmp = pSrc[a+1];
     pSrc[a+1] = pSrc[b+1];
     pSrc[b+1] = tmp;

    i += 2;
  }
}



// ---- arm_common_tables.c
static const uint16_t armBitRevIndexTable64[56] =
{
   /* radix 8, size 56 */
   8,64, 16,128, 24,192, 32,256, 40,320, 48,384, 56,448, 80,136, 88,200,
   96,264, 104,328, 112,392, 120,456, 152,208, 160,272, 168,336, 176,400,
   184,464, 224,280, 232,344, 240,408, 248,472, 296,352, 304,416, 312,480,
   368,424, 376,488, 440,496
};
static const uint16_t armBitRevIndexTable128[208] =
{
   /* 8x2, size 208 */
   8,512, 16,64, 24,576, 32,128, 40,640, 48,192, 56,704, 64,256, 72,768,
   80,320, 88,832, 96,384, 104,896, 112,448, 120,960, 128,512, 136,520,
   144,768, 152,584, 160,520, 168,648, 176,200, 184,712, 192,264, 200,776,
   208,328, 216,840, 224,392, 232,904, 240,456, 248,968, 264,528, 272,320,
   280,592, 288,768, 296,656, 304,328, 312,720, 328,784, 344,848, 352,400,
   360,912, 368,464, 376,976, 384,576, 392,536, 400,832, 408,600, 416,584,
   424,664, 432,840, 440,728, 448,592, 456,792, 464,848, 472,856, 480,600,
   488,920, 496,856, 504,984, 520,544, 528,576, 536,608, 552,672, 560,608,
   568,736, 576,768, 584,800, 592,832, 600,864, 608,800, 616,928, 624,864,
   632,992, 648,672, 656,896, 664,928, 688,904, 696,744, 704,896, 712,808,
   720,912, 728,872, 736,928, 744,936, 752,920, 760,1000, 776,800, 784,832,
   792,864, 808,904, 816,864, 824,920, 840,864, 856,880, 872,944, 888,1008,
   904,928, 912,960, 920,992, 944,968, 952,1000, 968,992, 984,1008
};

}  // namespace cmsis_fft

#endif
