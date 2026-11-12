// groove_mix.h: the groove patch's mixer: drums and synth (dry and send,
// stereo), a tempo-synced stereo delay and the Elements reverb on the sends,
// the metronome click, and the master volume.
//
//   fx in   = drum send + chord send
//   delay   = feedback delay of fx in (wet only)
//   reverb  = Elements reverb of (fx in + delay / 2), at amount 1 (wet only:
//             its output is in + (wet - in) amount)
//   out     = master (drums + synth + delay level x delay + reverb level x
//             reverb + click), clamped to the converter's range
//
// Shared by objects/groove_mix.axo and the host renderer; it needs the
// Elements reverb header from the firmware tree (xpatch.h includes it on
// the board; the host build adds -I $(FW)/mutable_instruments).
#ifndef GROOVE_MIX_H
#define GROOVE_MIX_H

#include "elements/dsp/fx/reverb.h"
#include "groove_ctl.h"

namespace freak {

// elements::Reverb runs FxEngine<32768, FORMAT_16_BIT>: 32768 uint16 words
// (fx_reverb.axo: the factory object allocated half of that).
const size_t kReverbBytes = 32768 * sizeof(uint16_t);
const int32_t kGrooveDelayLen = 96000;  // 2 s: a half note at 60 BPM

struct GrooveMix {
    Delay dl, dr;
    elements::Reverb *rev;  // null: no reverb
    float rl[kBlock], rr[kBlock];
    float wet_d, wet_r;     // smoothed levels
    float click_env, click_ph, click_hz;

    // mem_l, mem_r: kGrooveDelayLen floats each (or null); rev: an
    // initialised reverb (or null).
    void init(float *mem_l, float *mem_r, elements::Reverb *r) {
        dl.init(mem_l, kGrooveDelayLen);
        dr.init(mem_r, kGrooveDelayLen);
        rev = r;
        wet_d = wet_r = 0.0f;
        click_env = 0.0f; click_ph = 0.0f; click_hz = 1000.0f;
    }

    // Inputs are kBlock floats each; out l, r are overwritten.
    void process(const GrooveBus &b, const float *drl, const float *drr, const float *dsl, const float *dsr,
                 const float *syl, const float *syr, const float *ssl, const float *ssr, float *l, float *r) {
        if (b.out.click) {  // a beat: 1 kHz, a bar: 1.6 kHz; 20 ms decay
            click_env = 1.0f;
            click_hz = b.out.click == 2 ? 1600.0f : 1000.0f;
            click_ph = 0.0f;
        }
        const float d = b.delay_t * kSampleRate;
        const float cdecay = 0.99896f;  // e^(-1/(0.02 fs)): 20 ms
        for (int i = 0; i < kBlock; i++) {
            const float fl = dsl[i] + ssl[i], fr = dsr[i] + ssr[i];
            const float yl = dl.process(fl, d, b.delay_fb);
            const float yr = dr.process(fr, d, b.delay_fb);
            rl[i] = fl + 0.5f * yl;
            rr[i] = fr + 0.5f * yr;
            wet_d += 0.002f * (b.delay_level - wet_d);
            l[i] = drl[i] + syl[i] + wet_d * yl;
            r[i] = drr[i] + syr[i] + wet_d * yr;
        }
        if (rev) {
            rev->set_amount(1.0f);
            rev->set_input_gain(0.5f);
            rev->set_time(0.35f + 0.6f * b.rev_time);
            rev->set_diffusion(0.7f);
            rev->set_lp(0.7f);
            rev->Process(rl, rr, kBlock);
            for (int i = 0; i < kBlock; i++) {
                wet_r += 0.002f * (b.rev_level - wet_r);
                l[i] += wet_r * rl[i];
                r[i] += wet_r * rr[i];
            }
        }
        for (int i = 0; i < kBlock; i++) {
            float c = 0.0f;
            if (click_env > 1e-4f) {
                c = 0.25f * click_env * fast_sin(2.0f * kPi * click_ph);
                click_ph += click_hz / kSampleRate;
                if (click_ph >= 1.0f) click_ph -= 1.0f;
                click_env *= cdecay;
            }
            l[i] = clampf(b.master * (l[i] + c), -0.999f, 0.999f);
            r[i] = clampf(b.master * (r[i] + c), -0.999f, 0.999f);
        }
    }
};

}  // namespace freak

#endif
