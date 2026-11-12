// soliton_voice.h: one voice of the soliton synth: the field oscillator
// (soliton_field.h) -> state-variable filter -> amp, with amp and mod
// envelopes and glide, as freak's voice. Its settings come from the control
// buses (soliton_ctl.h).
//
// The same code runs in objects/soliton_voice.axo and in the host renderer
// (tests/render_soliton.cpp). Trivially destructible (polyphonic subpatch).
//
// CPU: the field runs one work item (about 8,000 cycles at most, README)
// every kTicksPerItem = 2 ticks, and only while the voice sounds; 12 items
// make a step, so the field takes 125 steps per second. The voices take
// turns by parity (an offset per voice, set at init), so with 3 voices at
// most two items run in any tick. One item per tick for all three voices
// measured 89-90 % DSP with the effects, too close to the limit.
#ifndef SOLITON_VOICE_H
#define SOLITON_VOICE_H

#include "soliton_ctl.h"
#include "soliton_field.h"

namespace freak {

const int kTicksPerItem = 2;

static int g_sol_voices;  // counts voices at init, for the offsets

struct SolVoice {
    Field f;
    Env amp, mod;
    Svf svf;
    Glide glide;
    float buf[kBlock];
    bool prev_gate;
    float prev_gain;
    uint32_t kicks_seen;
    int offset;
    // SPICE: per-note random offsets in [-1, 1], drawn at each new note and
    // scaled live by the SPICE knob.
    uint32_t rng;
    float sp_pitch, sp_timbre, sp_rate, sp_cut;

    float rnd_unit() {  // xorshift32, uniform in [-1, 1)
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float)(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    void init() {
        f.init(kTicksPerItem);
        amp.init();
        mod.init();
        svf.init();
        glide.init();
        prev_gate = false;
        prev_gain = 0.0f;
        kicks_seen = 0;
        offset = g_sol_voices++ % kTicksPerItem;
        rng = 0x9E3779B9u ^ (uint32_t)(uintptr_t)this;
        if (rng == 0) rng = 1;
        sp_pitch = sp_timbre = sp_rate = sp_cut = 0.0f;
    }

    // Is the voice silent (envelope idle, no gate)? Then the field is not run.
    bool idle(bool gate) const { return !gate && amp.stage == Env::IDLE; }

    // note: MIDI note number (fractional); vel 0..1; out: kBlock samples,
    // nominally within [-1, 1] (soft-clipped, scaled for 3 voices)
    void render(float note, bool gate, float vel, float *out) {
        const Bus &b = bus();
        const SolBus &g = sbus();
        const bool strike = gate && !prev_gate;
        prev_gate = gate;
        const float sp = b.spice;
        if (strike) {
            sp_pitch = rnd_unit();
            sp_timbre = rnd_unit();
            sp_rate = rnd_unit();
            sp_cut = rnd_unit();
        }
        const float semis = glide.tick(note, b.glide ? b.glide_t : 0.0f) + b.pitch_mod + 0.5f * sp * sp_pitch;
        const float f0 = 440.0f * fast_exp2((semis - 69.0f) * (1.0f / 12.0f));

        const bool was_idle = amp.stage == Env::IDLE;  // before this tick's attack
        const float menv = mod.tick(gate, b.mod_a, b.mod_d, b.mod_s, b.mod_r, b.cycle);
        const float aenv = amp.tick(gate, b.amp_a, b.amp_d, b.amp_s, b.amp_r, false);

        FieldCtl c;
        c.engine = g.engine;
        c.timbre = clampf(g.timbre + (b.modtimbre ? 0.5f * menv : 0.0f) + 0.2f * sp * sp_timbre, 0.0f, 1.0f);
        c.color = g.color;
        c.damp = g.damp;
        c.rate = g.rate * fast_exp2(4.0f * g.modrate * menv) * (1.0f + 0.4f * sp * sp_rate);
        c.f0 = f0;

        if (strike) {
            // Amplitude: AMOUNT, scaled by velocity over +-1 octave at full
            // VEL > AMOUNT; SPICE turns the seed's phases.
            float A = engine_amp(c.engine, g.amount) * fast_exp2(g.veldepth * (2.0f * vel - 1.0f));
            const float amax = engine_amp_max(c.engine);
            if (A > amax) A = amax;
            const float ph[3] = {kPi * sp * rnd_unit(), kPi * sp * rnd_unit(), kPi * sp * rnd_unit()};
            f.note_on(c, A, c.engine == ENG_SOLITON ? c.color : 0.0f, g.seed, ph, was_idle);
            kicks_seen = g.kicks;
        } else if (idle(gate)) {
            for (int i = 0; i < kBlock; i++) out[i] = 0.0f;
            prev_gain = 0.0f;
            return;
        }
        if (g.kicks != kicks_seen) {
            kicks_seen = g.kicks;
            f.add_kick(g.kick_amt, kPi * (1.0f + rnd_unit()));
        }
        // Turns by the parity of the controller's tick count: global, so
        // voices that were idle stay out of step with each other.
        if (!strike && (g.tick + (uint32_t)offset) % kTicksPerItem == 0) f.run_item(c);
        f.play(buf, kBlock, f0);

        // Cutoff in octaves above 20 Hz: knob, key tracking (note 60 = no
        // offset), mod envelope, velocity, SPICE.
        const float oct = b.cutoff_oct + (b.keytrack ? (note - 60.0f) / 12.0f : 0.0f) +
                          b.env_amt_oct * menv + (b.velcut ? 2.0f * vel : 0.0f) + 1.5f * sp * sp_cut;
        svf.set(20.0f * fast_exp2(oct), b.q);

        // Gain: envelope x velocity (soft notes keep 0.4: velocity mostly
        // drives the field's amplitude), / 4 headroom for 3 voices, ramped
        // across the block.
        const float gain = aenv * (0.4f + 0.6f * vel) * 0.25f;
        const float step = (gain - prev_gain) * (1.0f / kBlock);
        float gg = prev_gain;
        for (int i = 0; i < kBlock; i++) {
            gg += step;
            out[i] = gg * soft_clip(svf.process(buf[i], b.fmode));
        }
        prev_gain = gain;
    }
};

}  // namespace freak

#endif
