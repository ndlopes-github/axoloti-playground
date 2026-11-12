// groove_ctl.h: maps a Launchkey 49 MK3 (standalone mode) onto the groove
// patch: a real-time looper of drums (pads) and chords (keys, a 4-voice
// synth), with drums generated in 16 styles after apc-chord. This file is
// the single source of truth for docs/groove_cheatsheet.tex.
//
//   keys          the synth (4 voices), played and recorded as played;
//                 sustain pedal (CC 64) holds notes
//   pads          the 16 drums, velocity; pressing harder rolls the pad in
//                 16ths. Bottom row = kit pads 1-8, top row = 9-16.
//                 Hold the upper round button (CC 104) + pad: write drums in
//                 style 1-16 (1-4 funk, 5-8 neo-soul, 9-12 boom-bap, 13-16
//                 broken beat; it sets tempo, swing and drunk too; the same
//                 pad again: a variation). Hold the lower round button
//                 (CC 105) + pad: erase that pad's hits;
//                 + Record: clear the whole loop; + a key: clear the chords.
//   transport     Play, Stop, Record (overdub; first take starts the loop),
//                 Capture MIDI (the last bars played into the loop),
//                 Quantise (drum input on the 16th grid, on/off), Click
//                 (metronome), Undo (and redo). The loop is always 4 bars;
//                 Loop is unused.
//   knobs         SYNTH page (Device Select): wave, PWM, detune, cutoff,
//                 resonance, env amount, attack, release.
//                 GROOVE page (Device Lock): tempo, swing, drunk, delay
//                 time (tempo divisions), delay feedback, delay level,
//                 reverb level, reverb time.
//   faders        kick, snare, hats, percussion, chords, drum send, chord
//                 send, synth decay, master
//   fader buttons mute kick, snare, hats, percussion, chords; sub
//                 oscillator; velocity -> cutoff; chorus (off, I, II, I+II);
//                 next synth preset
//   track < >     synth preset
//   mod wheel     vibrato; pitch bend +-2 semitones
//
// Header-only, C++11, no allocation; every type is a POD.
#ifndef GROOVE_CTL_H
#define GROOVE_CTL_H

#include <stdint.h>

#include "groove_drums.h"
#include "groove_seq.h"
#include "groove_synth.h"
#include "launchkey_ctl.h"

namespace freak {

// Everything the sound objects read, once per block.
struct GrooveBus {
    BlockOut out;                 // this block's drum hits and note events
    SynthParams synth;
    int chorus;                   // 0 off, 1 I, 2 II, 3 I+II
    float group_gain[NUM_DRUM_GROUPS];  // mutes applied
    float chords_gain;
    float drum_send, chord_send;
    float delay_t, delay_fb, delay_level;  // s; 0..0.9; 0..1
    float rev_level, rev_time;    // 0..1
    float master;
};
static GrooveBus g_groove_bus;
inline GrooveBus &gbus_groove() { return g_groove_bus; }

enum { GV_SYNTH, GV_GROOVE, NUM_GVPAGES };
enum { SK_WAVE, SK_PWM, SK_DETUNE, SK_CUTOFF, SK_RESO, SK_ENVAMT, SK_ATTACK, SK_RELEASE };
enum { GK_TEMPO, GK_SWING, GK_DRUNK, GK_DTIME, GK_DFB, GK_DLEVEL, GK_RLEVEL, GK_RTIME };
enum { GF_KICK, GF_SNARE, GF_HATS, GF_PERC, GF_CHORDS, GF_DSEND, GF_CSEND, GF_DECAY, GF_MASTER, NUM_GFADERS };
enum { GB_MKICK, GB_MSNARE, GB_MHATS, GB_MPERC, GB_MCHORDS, GB_SUB, GB_VELCUT, GB_CHORUS, GB_PRESET };

// Synth presets: the SYNTH knob page, the decay fader, sustain levels and
// switches. Chosen from the parameter meanings, not tuned by ear.
struct SynthPreset {
    const char *name;
    uint8_t knob[8];   // wave, pwm, detune, cutoff, reso, env amt, attack, release
    uint8_t decay;     // fader 8
    uint8_t amp_s, flt_s;  // sustain levels, 0..127
    bool sub, velcut;
    uint8_t chorus;
};
static const SynthPreset kSynthPresets[] = {
    {"juno brass",    {0, 0, 0, 52, 20, 70, 45, 45},   70, 90, 70, true, true, 1},
    {"prophet brass", {0, 0, 45, 48, 25, 75, 52, 50},  76, 95, 75, false, true, 0},
    {"soft pad",      {70, 60, 25, 62, 12, 25, 85, 85}, 95, 120, 110, false, false, 3},
    {"stab",          {0, 0, 35, 42, 38, 90, 0, 30},   48, 0, 0, false, true, 2},
    {"pwm strings",   {127, 85, 0, 66, 8, 20, 70, 75}, 85, 110, 105, true, false, 1},
    {"dark keys",     {40, 20, 20, 36, 30, 60, 10, 55}, 70, 40, 35, false, true, 2},
    {"bright brass",  {0, 0, 60, 60, 18, 65, 40, 40},  62, 100, 85, false, true, 1},
    {"reso sweep",    {0, 0, 40, 34, 75, 85, 75, 65},  88, 60, 50, false, false, 0},
};
const int kNumSynthPresets = sizeof(kSynthPresets) / sizeof(kSynthPresets[0]);

static const uint8_t kGrooveKnobInit[8] = {91, 0, 0, 40, 60, 70, 45, 64};  // 120 BPM, straight
static const uint8_t kGFaderInit[NUM_GFADERS] = {110, 105, 95, 95, 100, 25, 40, 70, 120};

// Value maps
inline float groove_tempo(uint8_t v) { return 70.0f + 70.0f * unit(v); }  // 70 .. 140 BPM (91 -> 120)
// Delay time as a tempo division of the beat: 1/16, 1/8, dotted 1/8, 1/4,
// dotted 1/4, 1/2 (in beats: 0.25 .. 2), in six equal knob zones.
static const float kDelayBeats[6] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
inline float delay_beats(uint8_t v) { int i = (int)v * 6 / 128; return kDelayBeats[i < 0 ? 0 : (i > 5 ? 5 : i)]; }
inline float attack_time(uint8_t v) { return 0.001f * fast_exp2(11.5507f * unit(v)); }   // 1 ms .. 3 s (log2 3000)
inline float decay_time(uint8_t v) { return 0.01f * fast_exp2(8.9658f * unit(v)); }     // 10 ms .. 5 s (log2 500)
inline float release_time(uint8_t v) { return 0.005f * fast_exp2(9.9658f * unit(v)); }  // 5 ms .. 5 s (log2 1000)

const int kMidiRing = 64;
struct MidiMsg { uint8_t s, d1, d2, pad_; };

enum { GEV_STYLE = 32, GEV_REC, GEV_TRANSPORT, GEV_UNDO, GEV_CAPTURE, GEV_CLEAR, GEV_SYNTHPRESET, GEV_QUANT, GEV_CLICK, GEV_CHORUS,
       GEV_LOOP };  // GEV_LOOP: never sent (the loop is always 4 bars); kept so a patcher holding the
                    // old groove_ctl.axo in its object cache still compiles

// The controller runs per block and per MIDI event: compiled for size (see
// groove_seq.h).
#pragma GCC push_options
#pragma GCC optimize("Os")
struct GrooveCtl {
    Groove g;
    // pickup, as freak
    uint8_t knob[NUM_GVPAGES][8];
    bool picked[NUM_GVPAGES][8];
    int16_t anchor_phys[NUM_GVPAGES][8];
    uint8_t anchor_val[NUM_GVPAGES][8];
    uint8_t phys[8];
    bool phys_known[8];
    uint8_t fader[NUM_GFADERS];
    bool fpicked[NUM_GFADERS];
    int16_t fanchor_phys[NUM_GFADERS];
    uint8_t fanchor_val[NUM_GFADERS];
    uint8_t fphys[NUM_GFADERS];
    bool fphys_known[NUM_GFADERS];
    int page, preset;
    bool mute[5];
    bool sub, velcut;
    int chorus;
    uint8_t amp_s, flt_s;
    bool mod_style, mod_erase;   // round buttons held
    bool keys_held[128];         // keys down (for Capture)
    bool sustain;
    bool deferred[128];          // released under the pedal
    uint8_t pressure[kNumPads];  // pad pressure (rolls)
    uint32_t last_step;
    float bend, modwheel;
    Lfo lfo;
    float lfo_hz;
    uint32_t ticks;
    MidiMsg ring[kMidiRing];
    volatile uint8_t rhead, rtail;
    Event events[kEventQueue];
    uint8_t ev_head, ev_count;

    void push_event(uint8_t type, uint8_t a, uint8_t b, uint8_t c) {
        if (ev_count == kEventQueue) return;
        Event &e = events[(ev_head + ev_count) % kEventQueue];
        e.type = type; e.a = a; e.b = b; e.c = c;
        ev_count++;
    }
    bool pop_event(Event &e) {
        if (ev_count == 0) return false;
        e = events[ev_head];
        ev_head = (uint8_t)((ev_head + 1) % kEventQueue);
        ev_count--;
        return true;
    }

    void load_preset(int i) {
        const SynthPreset &p = kSynthPresets[i];
        preset = i;
        for (int k = 0; k < 8; k++) knob[GV_SYNTH][k] = p.knob[k];
        fader[GF_DECAY] = p.decay;
        amp_s = p.amp_s; flt_s = p.flt_s;
        sub = p.sub; velcut = p.velcut; chorus = p.chorus;
    }
    void reanchor_knobs(int pg) {
        for (int k = 0; k < 8; k++) {
            const int d = (int)phys[k] - (int)knob[pg][k];
            picked[pg][k] = phys_known[k] && d >= -2 && d <= 2;
            anchor_phys[pg][k] = phys_known[k] ? phys[k] : -1;
            anchor_val[pg][k] = knob[pg][k];
        }
    }
    void reanchor_fader(int f) {
        const int d = (int)fphys[f] - (int)fader[f];
        fpicked[f] = fphys_known[f] && d >= -2 && d <= 2;
        fanchor_phys[f] = fphys_known[f] ? fphys[f] : -1;
        fanchor_val[f] = fader[f];
    }
    void apply_preset(int i) {
        load_preset(i);
        if (page == GV_SYNTH) reanchor_knobs(GV_SYNTH);
        reanchor_fader(GF_DECAY);
        push_event(GEV_SYNTHPRESET, (uint8_t)i, 0, 0);
    }

    void init() {
        g.init();
        for (int pg = 0; pg < NUM_GVPAGES; pg++)
            for (int k = 0; k < 8; k++) { picked[pg][k] = pg == GV_SYNTH; anchor_phys[pg][k] = -1; }
        for (int k = 0; k < 8; k++) { phys[k] = 0; phys_known[k] = false; knob[GV_GROOVE][k] = kGrooveKnobInit[k]; }
        for (int f = 0; f < NUM_GFADERS; f++) {
            fader[f] = kGFaderInit[f];
            fpicked[f] = true;
            fanchor_phys[f] = -1;
            fphys[f] = 0;
            fphys_known[f] = false;
        }
        load_preset(0);
        for (int pg = 0; pg < NUM_GVPAGES; pg++)
            for (int k = 0; k < 8; k++) anchor_val[pg][k] = knob[pg][k];
        for (int f = 0; f < NUM_GFADERS; f++) fanchor_val[f] = fader[f];
        page = GV_SYNTH;
        for (int i = 0; i < 5; i++) mute[i] = false;
        mod_style = mod_erase = false;
        for (int i = 0; i < 128; i++) { keys_held[i] = false; deferred[i] = false; }
        sustain = false;
        for (int i = 0; i < kNumPads; i++) pressure[i] = 0;
        last_step = 0;
        bend = modwheel = 0.0f;
        lfo.init();
        lfo_hz = 5.0f;
        ticks = 0;
        rhead = rtail = 0;
        ev_head = ev_count = 0;
        g.clock.set_bpm(groove_tempo(knob[GV_GROOVE][GK_TEMPO]));
    }

    // ---- MIDI in: queued here, handled in tick() (one consumer)
    void midi(uint8_t s, uint8_t d1, uint8_t d2) {
        const uint8_t next = (uint8_t)((rtail + 1) % kMidiRing);
        if (next == rhead) return;  // full: drop
        ring[rtail].s = s; ring[rtail].d1 = d1; ring[rtail].d2 = d2;
        __asm__ volatile("" ::: "memory");
        rtail = next;
    }

    // Bottom row = kit pads 0..7, top row = 8..15 (Launchkey::pad_index has
    // the rows the other way round, for freak's presets on top).
    static int drum_pad(uint8_t note) {
        const int i = Launchkey::pad_index(note);
        return i < 0 ? -1 : (i >= 8 ? i - 8 : i + 8);
    }

    void on_knob(int k, uint8_t v) {
        const int p = page;
        if (Launchkey::follow(knob[p][k], picked[p][k], anchor_phys[p][k], anchor_val[p][k], v))
            push_event(EV_PICKED, (uint8_t)p, (uint8_t)k, knob[p][k]);
        phys[k] = v;
        phys_known[k] = true;
        if (p == GV_GROOVE) {
            if (k == GK_TEMPO) g.clock.set_bpm(groove_tempo(knob[p][k]));
            if (k == GK_SWING) g.swing = unit(knob[p][k]);
            if (k == GK_DRUNK) g.drunk = unit(knob[p][k]);
        }
    }
    void on_fader(int f, uint8_t v) {
        if (Launchkey::follow(fader[f], fpicked[f], fanchor_phys[f], fanchor_val[f], v))
            push_event(EV_PICKED, kPickedFader, (uint8_t)f, fader[f]);
        fphys[f] = v;
        fphys_known[f] = true;
    }
    void on_button(int b) {
        if (b <= GB_MCHORDS) { mute[b] = !mute[b]; return; }
        switch (b) {
        case GB_SUB: sub = !sub; break;
        case GB_VELCUT: velcut = !velcut; break;
        case GB_CHORUS: chorus = (chorus + 1) % 4; push_event(GEV_CHORUS, (uint8_t)chorus, 0, 0); break;
        case GB_PRESET: apply_preset((preset + 1) % kNumSynthPresets); break;
        }
    }
    void set_page(int p, uint8_t src) {
        if (p != page) push_event(EV_PAGE, (uint8_t)p, src, 0);
        page = p;
        reanchor_knobs(p);
    }
    // After a style: the GROOVE knobs tempo, swing and drunk take its values
    // (the physical knobs pick them up when turned through them).
    void style_knobs() {
        const float t = (g.clock.bpm - 70.0f) * (127.0f / 70.0f) + 0.5f;
        knob[GV_GROOVE][GK_TEMPO] = (uint8_t)(t < 0.0f ? 0.0f : t > 127.0f ? 127.0f : t);
        knob[GV_GROOVE][GK_SWING] = (uint8_t)(g.swing * 127.0f + 0.5f);
        knob[GV_GROOVE][GK_DRUNK] = (uint8_t)(g.drunk * 127.0f + 0.5f);
        if (page == GV_GROOVE) reanchor_knobs(GV_GROOVE);
        else for (int k = GK_TEMPO; k <= GK_DRUNK; k++) anchor_val[GV_GROOVE][k] = knob[GV_GROOVE][k];
    }

    void key_on(uint8_t n, uint8_t v, BlockOut &out) {
        if (mod_erase) {  // lower round button + a key: clear the chords
            g.loop.snapshot();
            g.loop.erase(EV_ON, -1);
            g.resync();
            g.release_loop_notes(out);
            push_event(GEV_CLEAR, 1, 0, 0);
            return;
        }
        if (deferred[n]) { deferred[n] = false; g.live(EV_OFF, n, 0, out); }
        keys_held[n] = true;
        g.live(EV_ON, n, v, out);
    }
    void key_off(uint8_t n, BlockOut &out) {
        if (!keys_held[n]) return;
        keys_held[n] = false;
        if (sustain) { deferred[n] = true; return; }
        g.live(EV_OFF, n, 0, out);
    }
    void pedal(bool down, BlockOut &out) {
        sustain = down;
        if (down) return;
        for (int k = 0; k < 128; k++)
            if (deferred[k]) { deferred[k] = false; g.live(EV_OFF, (uint8_t)k, 0, out); }
    }
    void on_pad(uint8_t note, uint8_t v, bool is_pressure, BlockOut &out) {
        const int d = drum_pad(note);
        if (d < 0) return;
        if (is_pressure) { pressure[d] = v; return; }
        if (v == 0) { pressure[d] = 0; return; }  // release
        if (mod_style) {
            const bool vary = g.style == d;
            g.generate(d, vary);
            style_knobs();
            push_event(GEV_STYLE, (uint8_t)d, vary, 0);
            return;
        }
        if (mod_erase) {
            g.loop.snapshot();
            g.loop.erase(EV_DRUM, d);
            g.resync();
            push_event(GEV_CLEAR, 2, (uint8_t)d, 0);
            return;
        }
        g.live(EV_DRUM, (uint8_t)d, v, out);
    }
    void transport(uint8_t cc, BlockOut &out) {
        switch (cc) {
        case 115: g.play(); push_event(GEV_TRANSPORT, 1, 0, 0); break;           // Play
        case 116: g.stop(out); push_event(GEV_TRANSPORT, 0, 0, 0); break;        // Stop
        case 117:                                                               // Record
            if (mod_erase) {
                g.loop.snapshot();
                g.loop.erase(-1, -1);
                g.resync();
                g.release_loop_notes(out);
                push_event(GEV_CLEAR, 0, 0, 0);
            } else {
                g.record_toggle();
                push_event(GEV_REC, g.recording, 0, 0);
            }
            break;
        case 118: break;                                                         // Loop: unused (always 4 bars)
        case 74: push_event(GEV_CAPTURE, g.capture(keys_held), 0, 0); break;    // Capture MIDI
        case 75: g.quantize = !g.quantize; push_event(GEV_QUANT, g.quantize, 0, 0); break;  // Quantise
        case 76: g.click_on = !g.click_on; push_event(GEV_CLICK, g.click_on, 0, 0); break;  // Click
        case 77:                                                                 // Undo
            if (g.loop.undo()) { g.release_loop_notes(out); g.resync(); push_event(GEV_UNDO, 0, 0, 0); }
            break;
        }
    }

    void handle(uint8_t s, uint8_t d1, uint8_t d2, BlockOut &out) {
        const uint8_t type = s & 0xF0, ch = s & 0x0F;
        if (ch == 0 && type == 0x90) { if (d2 > 0) key_on(d1, d2, out); else key_off(d1, out); }
        else if (ch == 0 && type == 0x80) key_off(d1, out);
        else if (ch == 0 && type == 0xB0) {
            if (d1 >= 21 && d1 <= 28) on_knob(d1 - 21, d2);
            else if (d1 >= 71 && d1 <= 79) on_fader(d1 - 71, d2);
            else if (d1 >= 11 && d1 <= 19) { if (d2 > 0) on_button(d1 - 11); }
            else if (d1 == 1) modwheel = unit(d2);
            else if (d1 == 64) pedal(d2 >= 64, out);
        } else if (ch == 0 && type == 0xE0) {
            bend = (float)(((int)d2 << 7 | d1) - 8192) * (1.0f / 8192.0f);
        } else if (ch == 9 && type == 0x90) on_pad(d1, d2, false, out);
        else if (ch == 9 && type == 0x80) on_pad(d1, 0, false, out);
        else if (ch == 9 && type == 0xA0) on_pad(d1, d2, true, out);
        else if (ch == 15 && type == 0xB0) {
            if (d1 == 104) { mod_style = d2 > 0; return; }   // held: modifiers
            if (d1 == 105) { mod_erase = d2 > 0; return; }
            if (d2 == 0) return;
            switch (d1) {
            case 51: case 106: set_page(GV_SYNTH, d1); break;
            case 52: case 107: set_page(GV_GROOVE, d1); break;
            case 102: apply_preset((preset + kNumSynthPresets - 1) % kNumSynthPresets); break;
            case 103: apply_preset((preset + 1) % kNumSynthPresets); break;
            default: transport(d1, out);
            }
        }
    }

    // Pads pressed past 3/4 of their pressure roll in 16ths, at the
    // pressure's velocity, on the swung grid (and are recorded so).
    void rolls(BlockOut &out) {
        if (!g.clock.running) return;
        const uint32_t step = g.now() / kStepTicks;
        if (step == last_step) return;
        last_step = step;
        for (int d = 0; d < kNumPads; d++)
            if (pressure[d] >= 96) g.live(EV_DRUM, (uint8_t)d, pressure[d], out);
    }

    // Once per block: MIDI in, the loop, then the buses.
    void tick() {
        ticks++;
        GrooveBus &b = gbus_groove();
        g.tick(b.out);  // clears out, then plays the loop
        while (rhead != rtail) {
            const MidiMsg m = ring[rhead];
            rhead = (uint8_t)((rhead + 1) % kMidiRing);
            handle(m.s, m.d1, m.d2, b.out);
        }
        rolls(b.out);

        const uint8_t *s = knob[GV_SYNTH], *x = knob[GV_GROOVE];
        SynthParams &sp = b.synth;
        sp.wave = unit(s[SK_WAVE]);
        sp.pwm = unit(s[SK_PWM]);
        sp.detune = unit(s[SK_DETUNE]);
        sp.cutoff_oct = 10.0f * unit(s[SK_CUTOFF]);
        sp.reso = unit(s[SK_RESO]);
        sp.env_amt = 6.0f * unit(s[SK_ENVAMT]);
        sp.f_a = sp.a_a = attack_time(s[SK_ATTACK]);
        sp.f_d = sp.a_d = decay_time(fader[GF_DECAY]);
        sp.f_r = sp.a_r = release_time(s[SK_RELEASE]);
        sp.a_s = unit(amp_s);
        sp.f_s = unit(flt_s);
        sp.sub = sub;
        sp.velcut = velcut;
        sp.bend = 2.0f * bend;
        sp.vib = 0.4f * modwheel;
        sp.lfo = lfo.tick(lfo_hz, 1);  // triangle, for PWM and vibrato
        b.chorus = chorus;
        for (int i = 0; i < NUM_DRUM_GROUPS; i++) {
            const float u = unit(fader[GF_KICK + i]);
            b.group_gain[i] = mute[i] ? 0.0f : u * u;
        }
        const float c = unit(fader[GF_CHORDS]);
        b.chords_gain = mute[GB_MCHORDS] ? 0.0f : c * c;
        b.drum_send = unit(fader[GF_DSEND]);
        b.chord_send = unit(fader[GF_CSEND]);
        b.delay_t = delay_beats(x[GK_DTIME]) * 60.0f / g.clock.bpm;
        b.delay_fb = 0.9f * unit(x[GK_DFB]);
        b.delay_level = unit(x[GK_DLEVEL]);
        b.rev_level = unit(x[GK_RLEVEL]);
        b.rev_time = unit(x[GK_RTIME]);
        const float m = unit(fader[GF_MASTER]);
        b.master = m * m;
    }
};

#pragma GCC pop_options

// The sound objects' blocks, shared by objects/groove_drums.axo,
// groove_synth.axo and the host renderer. They run after the hub's tick.
inline void groove_drums_block(Drums &d, const GrooveBus &b, float *dl, float *dr, float *sl, float *sr) {
    for (int i = 0; i < b.out.nd; i++) d.trigger(kit(), b.out.dpad[i], b.out.dvel[i]);
    d.render(b.group_gain, b.drum_send, dl, dr, sl, sr);
}
inline void groove_synth_block(Synth &s, Chorus &ch, const GrooveBus &b, float *mono, float *l, float *r,
                               float *sl, float *sr) {
    if (b.out.all_off) s.all_off();
    for (int i = 0; i < b.out.nn; i++) {
        if (b.out.ntype[i] == EV_ON) s.note_on(b.out.nnote[i], b.out.nvel[i]);
        else s.note_off(b.out.nnote[i]);
    }
    s.render(b.synth, mono);
    // Four loud voices can sum past 1: the soft clip bounds them by 2/3.
    for (int i = 0; i < kBlock; i++) mono[i] = b.chords_gain * soft_clip(mono[i]);
    ch.process(b.chorus, mono, l, r);
    for (int i = 0; i < kBlock; i++) { sl[i] = b.chord_send * l[i]; sr[i] = b.chord_send * r[i]; }
}

}  // namespace freak

#endif
