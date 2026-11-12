// groove_synth.h: the groove patch's 4-voice synth for chords, after the
// Juno and the Prophet: two band-limited oscillators (saw morphing into a
// pulse with PWM), a sub an octave down, a 4-pole resonant low-pass with its
// own envelope, an amp envelope, and a Juno-style stereo chorus.
//
// Header-only, C++11, no libm, every struct a POD.
#ifndef GROOVE_SYNTH_H
#define GROOVE_SYNTH_H

#include <stdint.h>

#include "freak_dsp.h"

namespace freak {

const int kSynthVoices = 4;

// What the voices read, set by the controller once per block.
struct SynthParams {
    float wave;        // 0 saw .. 1 pulse
    float pwm;         // 0..1: LFO depth on the pulse width
    float detune;      // 0..1: oscillator B fades in over the first 1/8, then detunes up to 25 cents
    float cutoff_oct;  // cutoff = 20 Hz * 2^cutoff_oct (at note 60, before the envelope)
    float reso;        // 0..1 -> ladder feedback 0..3.6 (4 self-oscillates)
    float env_amt;     // octaves the filter envelope opens, 0..6
    float f_a, f_d, f_s, f_r;  // filter envelope: s, s, level, s
    float a_a, a_d, a_s, a_r;  // amp envelope
    bool sub, velcut;
    float bend;        // semitones
    float vib;         // vibrato depth, semitones
    float lfo;         // shared LFO, -1..1 (PWM and vibrato)
};

// PolyBLEP residual of a saw's falling step at phase 0: subtracted from the
// naive saw 2p - 1 it band-limits the discontinuity (as freak_voice.h).
// idt = 1/dt, precomputed per block.
inline float poly_blep(float p, float dt, float idt) {
    if (p < dt) { const float t = p * idt; return t + t - t * t - 1.0f; }
    if (p > 1.0f - dt) { const float t = (p - 1.0f) * idt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
inline float blep_saw(float p, float dt, float idt) { return 2.0f * p - 1.0f - poly_blep(p, dt, idt); }
inline float wrap01(float p) { return p >= 1.0f ? p - 1.0f : p; }
// Pulse of width w in (0, 1) as the difference of two band-limited saws,
// saw(p) - saw(p - w): it is 2w - 2 for p < w and 2w after, so its mean is
// (2w - 2) w + 2w (1 - w) = 0, with no DC whatever the width.
inline float blep_pulse(float p, float w, float dt, float idt) {
    return blep_saw(p, dt, idt) - blep_saw(wrap01(p + 1.0f - w), dt, idt);
}

struct SynthVoice {
    float pa, pb, ps;   // phases of oscillators A, B and the sub
    float s1, s2, s3, s4;  // ladder stage states
    Env amp, flt;
    uint8_t note;
    float vel;
    bool gate;
    uint32_t age;       // when it was struck (voice stealing)
    float gain_prev;

    void init() {
        pa = pb = ps = 0.0f;
        s1 = s2 = s3 = s4 = 0.0f;
        amp.init();
        flt.init();
        note = 60;
        vel = 0.0f;
        gate = false;
        age = 0;
        gain_prev = 0.0f;
    }
    bool idle() const { return amp.stage == Env::IDLE && !gate; }

    // Add one block to out (mono, nominally within +-0.5 per voice).
    //
    // Filter: a 4-pole ladder of trapezoidal one-poles (Zavalishin, "The Art
    // of VA Filter Design", ch. 5). A stage with state s and G = g/(1+g),
    // g = tan(pi fc/fs), gives y = G x + (1 - G) s. Four in a row from input
    // u give y4 = G^4 u + S with S = (1-G)(G^3 s1 + G^2 s2 + G s3 + s4), so
    // the feedback u = x - k y4 solves in closed form as
    // u = (x - k S) / (1 + k G^4): no unit delay in the loop. For k < 4 the
    // analog ladder is stable, and the trapezoidal (bilinear) map keeps it
    // so. u is soft-clipped, which bounds the resonance and adds drive.
    void render(const SynthParams &sp, float *out) {
        const float menv = flt.tick(gate, sp.f_a, sp.f_d, sp.f_s, sp.f_r, false);
        const float aenv = amp.tick(gate, sp.a_a, sp.a_d, sp.a_s, sp.a_r, false);
        if (amp.stage == Env::IDLE && aenv <= 0.0f && gain_prev <= 0.0f) return;

        const float semis = (float)note - 69.0f + sp.bend + sp.vib * sp.lfo;
        const float fa = 440.0f * fast_exp2(semis * (1.0f / 12.0f));
        const float cents = 25.0f * clampf(sp.detune, 0.0f, 1.0f);
        const float lb = clampf(8.0f * sp.detune, 0.0f, 1.0f);  // oscillator B level
        const float dta = clampf(fa / kSampleRate, 1e-5f, 0.45f);
        const float dtb = clampf(dta * fast_exp2(cents * (1.0f / 1200.0f)), 1e-5f, 0.45f);
        const float dts = 0.5f * dta;
        const float ia = 1.0f / dta, ib = 1.0f / dtb, is = 1.0f / dts;
        const float w = clampf(sp.wave, 0.0f, 1.0f);
        const float width = clampf(0.5f - 0.42f * clampf(sp.pwm, 0.0f, 1.0f) * (0.5f + 0.5f * sp.lfo), 0.05f, 0.5f);
        const float sub = sp.sub ? 0.5f : 0.0f;
        const float norm = 0.45f / (1.0f + 0.6f * lb + sub);

        // Cutoff: base, envelope (scaled by velocity with VELO -> CUTOFF),
        // key tracking at half an octave per octave around note 60.
        const float amt = sp.env_amt * (sp.velcut ? 0.4f + 0.6f * vel : 1.0f);
        const float oct = sp.cutoff_oct + amt * menv + 0.5f * ((float)note - 60.0f) / 12.0f;
        const float fc = clampf(20.0f * fast_exp2(oct), 20.0f, 0.45f * kSampleRate);
        const float g = fast_tan_pi(fc / kSampleRate);
        const float G = g / (1.0f + g);
        const float G2 = G * G, G3 = G2 * G, G4 = G2 * G2, H = 1.0f - G;
        const float k = 3.6f * clampf(sp.reso, 0.0f, 1.0f);
        const float ik = 1.0f / (1.0f + k * G4);
        const float comp = 1.0f + 0.5f * k;  // part of the pass-band loss 1/(1+k) back

        const float gain = aenv * (0.3f + 0.7f * vel) * 0.5f;  // the sum is soft-clipped (groove_synth_block)
        const float step = (gain - gain_prev) * (1.0f / kBlock);
        float gg = gain_prev;
        for (int i = 0; i < kBlock; i++) {
            float a = blep_saw(pa, dta, ia);
            if (w > 0.0f) a += w * (blep_pulse(pa, width, dta, ia) - a);
            float x = a;
            if (lb > 0.0f) {
                float b = blep_saw(pb, dtb, ib);
                if (w > 0.0f) b += w * (blep_pulse(pb, width, dtb, ib) - b);
                x += lb * b;
            }
            if (sub > 0.0f) x += sub * blep_pulse(ps, 0.5f, dts, is);
            x *= norm;
            pa = wrap01(pa + dta);
            pb = wrap01(pb + dtb);
            ps = wrap01(ps + dts);

            const float S = H * (G3 * s1 + G2 * s2 + G * s3 + s4);
            const float u = soft_clip((x - k * S) * ik);
            float v = (u - s1) * G; const float y1 = v + s1; s1 = y1 + v;
            v = (y1 - s2) * G; const float y2 = v + s2; s2 = y2 + v;
            v = (y2 - s3) * G; const float y3 = v + s3; s3 = y3 + v;
            v = (y3 - s4) * G; const float y4 = v + s4; s4 = y4 + v;
            gg += step;
            out[i] += gg * comp * y4;
        }
        gain_prev = gain;
    }
};

struct Synth {
    SynthVoice v[kSynthVoices];
    uint32_t clock;

    void init() {
        for (int i = 0; i < kSynthVoices; i++) v[i].init();
        clock = 0;
    }
    // A note: the voice already on that note, else an idle one, else the
    // oldest released one, else the oldest of all.
    int pick_voice(int note) const {
        for (int i = 0; i < kSynthVoices; i++) if (!v[i].idle() && v[i].note == note) return i;
        for (int i = 0; i < kSynthVoices; i++) if (v[i].idle()) return i;
        int pick = -1;
        for (int i = 0; i < kSynthVoices; i++)
            if (!v[i].gate && (pick < 0 || v[i].age < v[pick].age)) pick = i;
        if (pick >= 0) return pick;
        pick = 0;
        for (int i = 1; i < kSynthVoices; i++) if (v[i].age < v[pick].age) pick = i;
        return pick;
    }
    void note_on(int note, int vel127) {
        SynthVoice &s = v[pick_voice(note)];
        s.note = (uint8_t)note;
        s.vel = (float)vel127 * (1.0f / 127.0f);
        s.gate = true;
        s.age = ++clock;
        // The envelopes start on a rising gate. If they already saw the
        // gate high (the same note again, or a held voice stolen), restart
        // them here.
        if (s.amp.gate) { s.amp.stage = Env::ATTACK; s.flt.stage = Env::ATTACK; }
    }
    void note_off(int note) {
        for (int i = 0; i < kSynthVoices; i++) if (v[i].note == note) v[i].gate = false;
    }
    void all_off() {
        for (int i = 0; i < kSynthVoices; i++) v[i].gate = false;
    }
    void render(const SynthParams &sp, float *out) {
        for (int i = 0; i < kBlock; i++) out[i] = 0.0f;
        for (int i = 0; i < kSynthVoices; i++) v[i].render(sp, out);
    }
};

// Juno-style chorus: a short delay swept by a triangle LFO, read twice in
// opposite phase for left and right, mixed with the dry signal. Modes after
// the Juno-60: I (0.5 Hz) and II (0.8 Hz), sweeping 1.7..5.3 ms; I+II a fast
// shallow vibrato (9.75 Hz).
const int kChorusLen = 512;  // 10.7 ms
struct Chorus {
    float buf[kChorusLen];
    int w;
    float phase;
    void init() { for (int i = 0; i < kChorusLen; i++) buf[i] = 0.0f; w = 0; phase = 0.0f; }
    float read(float d) {  // d samples ago, 1 <= d <= kChorusLen - 2
        float rp = (float)w - d;
        if (rp < 0.0f) rp += (float)kChorusLen;
        const int i = (int)rp;
        const float f = rp - (float)i;
        const int j = i + 1 < kChorusLen ? i + 1 : 0;
        return buf[i] + f * (buf[j] - buf[i]);
    }
    void process(int mode, const float *in, float *l, float *r) {
        if (mode <= 0) {
            for (int i = 0; i < kBlock; i++) l[i] = r[i] = in[i];
            return;
        }
        const float rate = mode == 1 ? 0.513f : mode == 2 ? 0.863f : 9.75f;
        const float centre = (mode == 3 ? 3.3f : 3.5f) * 0.001f * kSampleRate;
        const float depth = (mode == 3 ? 0.3f : 1.8f) * 0.001f * kSampleRate;
        const float dp = rate / kSampleRate;
        for (int i = 0; i < kBlock; i++) {
            buf[w] = in[i];
            const float tri = 4.0f * absf(phase - 0.5f) - 1.0f;
            const float wl = read(centre + depth * tri), wr = read(centre - depth * tri);
            l[i] = 0.7f * (in[i] + wl);
            r[i] = 0.7f * (in[i] + wr);
            phase += dp;
            if (phase >= 1.0f) phase -= 1.0f;
            if (++w >= kChorusLen) w = 0;
        }
    }
};

}  // namespace freak

#endif
