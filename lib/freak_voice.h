// freak_voice.h: one voice of the freak synth: Braids digital oscillator ->
// state-variable filter -> amp, with amp and mod envelopes and glide. Its
// settings come from the control bus (launchkey_ctl.h).
//
// The same code runs in objects/freak_voice.axo and in the host renderer
// (tests/render_freak.cpp). The Braids implementation itself
// (third_party/braids/digital_oscillator.cc) must be compiled once alongside:
// the object includes it, and the host build links it.
//
// The struct is trivially destructible, because the voice lives in a
// polyphonic subpatch (see README: atexit). The oscillator therefore sits in
// raw storage, built with placement new.
#ifndef FREAK_VOICE_H
#define FREAK_VOICE_H

#include <new>
#include <string.h>

#include "braids/digital_oscillator.h"
#include "braids_init.h"
#include "launchkey_ctl.h"

namespace freak {

struct Voice {
    alignas(braids::DigitalOscillator) uint8_t osc_mem[sizeof(braids::DigitalOscillator)];
    braids::DigitalOscillator *osc;
    Env amp, mod;
    Svf svf;
    Glide glide;
    uint8_t sync[kBlock];
    int16_t buf[kBlock];
    bool prev_gate;
    float prev_gain;
    bool delay_ok;  // the delay lines are 16 KB contiguous (braids_init.h)
    float saw_phase;

    // Comb (shape 2) filters its buffer in place. Braids feeds it its analog
    // saw (MacroOscillator::RenderSawComb), so here a PolyBLEP saw fills the
    // buffer first: naive saw 2p - 1, minus the polynomial residual of the
    // step around each wrap, which band-limits the discontinuity.
    void render_saw(float semis) {
        const float dt = clampf(440.0f * fast_exp2((semis - 69.0f) * (1.0f / 12.0f)) / kSampleRate, 0.0f, 0.45f);
        for (int i = 0; i < kBlock; i++) {
            float p = saw_phase, y = 2.0f * p - 1.0f;
            if (p < dt) {
                const float t = p / dt;
                y -= t + t - t * t - 1.0f;
            } else if (p > 1.0f - dt) {
                const float t = (p - 1.0f) / dt;
                y -= t * t + t + t + 1.0f;
            }
            buf[i] = (int16_t)(y * 0.7f * 32767.0f);
            saw_phase += dt;
            if (saw_phase >= 1.0f) saw_phase -= 1.0f;
        }
    }

    void init() {
        osc = new (osc_mem) braids::DigitalOscillator();
        delay_ok = init_braids(osc);
        saw_phase = 0.0f;
        amp.init();
        mod.init();
        svf.init();
        glide.init();
        memset(sync, 0, sizeof(sync));
        prev_gate = false;
        prev_gain = 0.0f;
    }

    // note: MIDI note number (fractional); vel 0..1; out: kBlock samples,
    // nominally within [-1, 1] (soft-clipped, scaled for 3 voices)
    void render(float note, bool gate, float vel, float *out) {
        const Bus &b = bus();
        const float semis = glide.tick(note, b.glide ? b.glide_t : 0.0f) + b.pitch_mod;
        // Braids: MIDI note << 7, with tables for 96 kHz, so + 12 semitones at 48 kHz.
        const int32_t p = (int32_t)((semis + 12.0f) * 128.0f);
        osc->set_pitch((int16_t)(p < 0 ? 0 : (p > 32767 ? 32767 : p)));

        const float menv = mod.tick(gate, b.mod_a, b.mod_d, b.mod_s, b.mod_r, b.cycle);
        const float aenv = amp.tick(gate, b.amp_a, b.amp_d, b.amp_s, b.amp_r, false);

        const float timbre = clampf(b.timbre + (b.modtimbre ? 0.5f * menv : 0.0f), 0.0f, 1.0f);
        const int shape = (b.shape == kShapeComb && !delay_ok) ? kShapeComb + 1 : b.shape;
        osc->set_shape((braids::DigitalOscillatorShape)shape);
        osc->set_parameters((int16_t)(timbre * 32767.0f), (int16_t)(b.color * 32767.0f));
        if (gate && !prev_gate) osc->Strike();
        prev_gate = gate;
        if (shape == kShapeComb) render_saw(semis);
        osc->Render(sync, buf, kBlock);

        // Cutoff in octaves above 20 Hz: knob, key tracking (note 60 = no
        // offset), mod envelope, velocity.
        const float oct = b.cutoff_oct + (b.keytrack ? (note - 60.0f) / 12.0f : 0.0f) +
                          b.env_amt_oct * menv + (b.velcut ? 2.0f * vel : 0.0f);
        svf.set(20.0f * fast_exp2(oct), b.q);

        // Gain: envelope x velocity (soft notes keep a quarter), / 4 headroom
        // for 3 voices, ramped across the block to avoid zipper noise.
        const float gain = aenv * (0.25f + 0.75f * vel) * 0.25f;
        const float step = (gain - prev_gain) * (1.0f / kBlock);
        float g = prev_gain;
        for (int i = 0; i < kBlock; i++) {
            g += step;
            out[i] = g * soft_clip(svf.process(buf[i] * (1.0f / 32768.0f), b.fmode));
        }
        prev_gain = gain;
    }
};

}  // namespace freak

#endif
