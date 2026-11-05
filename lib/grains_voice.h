// grains_voice.h: the voice of the grains synth: grain engine (grains.h) on
// a bank source -> stereo state-variable filter -> amp, with an amp
// envelope, a mod envelope (to cutoff and optionally position, can cycle)
// and glide. Settings come from the control buses (grains_ctl.h) and the
// sources from the bank (grains_bank.h).
//
// The same code runs in objects/grains_voice.axo and in the host renderer
// (tests/render_grains.cpp). A POD, like freak's voice.
#ifndef GRAINS_VOICE_H
#define GRAINS_VOICE_H

#include "grains.h"
#include "grains_bank.h"
#include "grains_ctl.h"

namespace freak {

// Drive into the soft clip. Windowed grains of a source at -15 dBFS RMS come
// out about 14 dB below freak's voice (Braids runs near full scale); this
// brings them level, measured with tests/render_grains, while the soft clip
// still bounds the output.
#ifndef GRAINS_DRIVE
#define GRAINS_DRIVE 4.0f
#endif
const float kDrive = GRAINS_DRIVE;

struct GrainVoice {
    GrainEngine eng;
    Env amp, mod;
    Svf svf_l, svf_r;
    Glide glide;
    uint32_t strikes_seen, notes_seen;
    float prev_gain;
    float held_pos;     // position kept while frozen
    float bl[kBlock], br[kBlock];
    // SPICE: per-phrase random offsets in [-1, 1], scaled live by the knob.
    uint32_t rng;
    float sp_pos, sp_pitch, sp_cut;

    float rnd_unit() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float)(rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    void init(uint32_t seed) {
        eng.init(seed);
        amp.init();
        mod.init();
        svf_l.init();
        svf_r.init();
        glide.init();
        const GrainBus &g = gbus();
        strikes_seen = g.strikes;
        notes_seen = g.notes;
        prev_gain = 0.0f;
        held_pos = 0.5f;
        rng = seed ^ 0x9E3779B9u;
        if (rng == 0) rng = 1;
        sp_pos = sp_pitch = sp_cut = 0.0f;
    }

    // One block into l, r (overwritten), nominally within [-0.5, 0.5].
    void render(float *l, float *r) {
        const Bus &b = bus();
        const GrainBus &g = gbus();
        const GrainSource &src = bank_source(bank(), g.source);
        const bool strike = g.strikes != strikes_seen;
        const bool note_on = g.notes != notes_seen;
        strikes_seen = g.strikes;
        notes_seen = g.notes;
        if (strike) {
            sp_pos = rnd_unit();
            sp_pitch = rnd_unit();
            sp_cut = rnd_unit();
            // A new phrase while the gate stayed open (HOLD, pedal) restarts
            // the envelopes; otherwise the rising gate does.
            if (amp.gate) { amp.stage = Env::ATTACK; mod.stage = Env::ATTACK; }
        }
        const float sp = b.spice;
        const float note = glide.tick(g.note, b.glide ? b.glide_t : 0.0f);
        const float menv = mod.tick(g.gate, b.mod_a, b.mod_d, b.mod_s, b.mod_r, b.cycle);
        const float aenv = amp.tick(g.gate, b.amp_a, b.amp_d, b.amp_s, b.amp_r, false);

        // Position: control, mod envelope, SPICE (+-0.25); FREEZE holds it.
        float pos = clampf(g.position + (g.modpos ? 0.5f * menv : 0.0f) + 0.25f * sp * sp_pos, 0.0f, 1.0f);
        if (g.freeze) pos = held_pos;
        else held_pos = pos;

        GrainParams p;
        p.mode = g.mode;
        p.position = pos;
        p.size = g.size;
        p.density = g.density;
        // SPICE is also a chaos macro: more spray, scatter, reversal and width.
        p.spray = g.spray + 0.1f * sp;
        p.scatter = g.scatter + 3.0f * sp;
        p.octave_prob = g.octave_prob;
        p.reverse_prob = clampf(g.reverse_prob + 0.3f * sp, 0.0f, 1.0f);
        p.stereo = clampf(g.stereo + 0.5f * sp, 0.0f, 1.0f);
        p.shape = g.shape;
        p.scan_speed = g.scan_speed;
        p.pitch = note - 60.0f + b.pitch_mod + 0.5f * sp * sp_pitch;  // C4 plays the source as recorded
        p.freeze = g.freeze;
        p.active = amp.stage != Env::IDLE;  // grains while the amp envelope is open (also in release)
        if (strike) eng.restart(src, p);
        if (note_on && g.mode == GM_HIT) eng.hit(g.burst);

        for (int i = 0; i < kBlock; i++) bl[i] = br[i] = 0.0f;
        eng.render(src, p, bl, br);

        // Cutoff in octaves above 20 Hz, as freak: knob, key tracking (note 60
        // = no offset), mod envelope, velocity, SPICE (+-1.5 oct).
        const float oct = b.cutoff_oct + (b.keytrack ? (note - 60.0f) / 12.0f : 0.0f) +
                          b.env_amt_oct * menv + (b.velcut ? 2.0f * g.vel : 0.0f) + 1.5f * sp * sp_cut;
        svf_l.set(20.0f * fast_exp2(oct), b.q);
        svf_r.a1 = svf_l.a1; svf_r.a2 = svf_l.a2; svf_r.a3 = svf_l.a3; svf_r.k = svf_l.k;  // same filter

        // Gain: envelope x velocity (soft notes keep a quarter), ramped
        // across the block. The filter output is soft-clipped (bounded by
        // 2/3), so the output stays within 2/3 x 0.75 = 0.5: freak's
        // headroom (3 voices x 1/4 x 2/3). Dense, correlated clouds saturate
        // gently there instead of clipping.
        const float gain = aenv * (0.25f + 0.75f * g.vel) * 0.75f;
        const float step = (gain - prev_gain) * (1.0f / kBlock);
        float gg = prev_gain;
        for (int i = 0; i < kBlock; i++) {
            gg += step;
            l[i] = gg * soft_clip(kDrive * svf_l.process(bl[i], b.fmode));
            r[i] = gg * soft_clip(kDrive * svf_r.process(br[i], b.fmode));
        }
        prev_gain = gain;
    }
};

}  // namespace freak

#endif
