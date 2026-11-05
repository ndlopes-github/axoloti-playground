// launchkey_ctl.h: maps a Launchkey 49 MK3 (standalone mode) onto the
// controls of the "freak" synth. This file is the single source of truth for
// docs/cheatsheet.tex.
//
// MIDI in (recorded from the unit, docs/launchkey49mk3_map.md):
//   ch 1  CC 21-28 knobs      CC 71-79 faders     CC 11-19 fader buttons
//         CC 1 mod wheel      pitch bend          notes = keys (played by the voices)
//   ch 10 notes 36-51 pads (top row 40-43 48-51, bottom row 36-39 44-47)
//   ch 16 CC 51/52 Device Select / Device Lock (right of the knobs; observed),
//         102/103 track < >, 104/105 round buttons right of the pads, 106/107 left of the pads,
//         74-77 and 115-118 the two transport rows
//
// The result goes on a control bus (freak::bus()) read by the voices and the
// effects. Header-only, C++11, no allocation; every type is a POD.
#ifndef LAUNCHKEY_CTL_H
#define LAUNCHKEY_CTL_H

#include <stdint.h>

#include "freak_dsp.h"

namespace freak {

const int kNumShapes = 34;  // Braids digital oscillator, table order

// Everything the voices and effects read, in physical units.
struct Bus {
    int shape;            // 0..33, set by track < > only
    float timbre, color;  // 0..1, LFO applied
    float cutoff_oct;     // cutoff = 20 Hz * 2^cutoff_oct, LFO applied
    float q;              // 0.5..20
    int fmode;            // 0 LP, 1 BP, 2 HP
    float env_amt_oct;    // mod envelope -> cutoff, -5..+5 octaves
    bool keytrack, velcut, modtimbre, cycle, glide;
    float amp_a, amp_d, amp_s, amp_r;  // s; level; s
    float mod_a, mod_d, mod_s, mod_r;
    float pitch_mod;      // semitones: bend + LFO
    float glide_t;        // s
    bool delay_on;
    float delay_t, delay_fb, delay_mix;  // s; 0..0.95; 0..1
    float rev_amount, rev_time, rev_diff, rev_lp;  // 0..1, to fx/lmnts/reverb
    float master;         // 0..1 gain
    float spice;          // 0..1: per-note random variation in the voices (knob 1)
};

// One bus per patch. A namespace-scope POD in a header that every object of
// the patch includes; the patch is a single translation unit, so there is
// exactly one instance, zero-initialised in .bss, with no constructor or
// destructor to register.
static Bus g_bus;
inline Bus &bus() { return g_bus; }

// Knob pages
enum { PAGE_SOUND, PAGE_FX, NUM_PAGES };
// PAGE_SOUND knobs
enum { K_SPICE, K_TIMBRE, K_COLOR, K_CUTOFF, K_RESO, K_ENVAMT, K_LFORATE, K_LFOAMT };
// PAGE_FX knobs
enum { K_GLIDE, K_DTIME, K_DFB, K_DMIX, K_RAMOUNT, K_RTIME, K_RDIFF, K_RLP };
// Faders 1..9
enum { F_AA, F_AD, F_AS, F_AR, F_MA, F_MD, F_MS, F_MR, F_MASTER, NUM_FADERS };
// Fader buttons 1..9 (button 9 steps through the presets)
enum { B_FMODE, B_LFOTGT, B_LFOWAVE, B_MODTIMBRE, B_GLIDE, B_KEYTRACK, B_VELCUT, B_DELAY, B_PRESET };

enum { LFO_TIMBRE, LFO_COLOR, LFO_CUTOFF, LFO_PITCH, NUM_LFO_TARGETS };
const int kNumLfoWaves = 5;  // sine, triangle, saw, square, sample & hold

// Power-on values (0..127, as a knob or fader would send) = preset 0.
static const uint8_t kKnobInit[NUM_PAGES][8] = {
    {0, 64, 64, 100, 20, 80, 40, 0},  // shape, timbre, color, cutoff, reso, env amt, lfo rate, lfo amt
    {24, 70, 50, 40, 40, 64, 64, 80}, // glide, delay time, fb, mix, reverb amount, time, diffusion, lowpass
};
static const uint8_t kFaderInit[NUM_FADERS] = {10, 70, 90, 70, 0, 70, 0, 70, 100};
const int kInitShape = 1;  // saw swarm

// Presets (fader button 9 steps through them, Undo returns to 0). A preset
// sets both knob pages, the 8 envelope faders, the shape and the toggles; the
// volume fader is not part of a preset. The SOUND knob 1 value is derived
// from the shape. These are the cheat sheet's starting points: chosen from
// the parameter meanings, not yet tuned by ear.
enum { P_MODTIMBRE = 1, P_GLIDE = 2, P_KEYTRACK = 4, P_VELCUT = 8, P_DELAY = 16, P_CYCLE = 32 };
struct Preset {
    const char *name;
    uint8_t shape;
    uint8_t knob[NUM_PAGES][8];
    uint8_t fader[8];  // amp A D S R, mod A D S R
    uint8_t fmode, lfo_target, lfo_wave, flags;
};
static const Preset kPresets[] = {
    {"init", 1, {{0, 64, 64, 100, 20, 80, 40, 0}, {24, 70, 50, 40, 40, 64, 64, 80}},
     {10, 70, 90, 70, 0, 70, 0, 70}, 0, LFO_TIMBRE, 0, P_KEYTRACK},
    {"vowel pad", 10, {{0, 64, 64, 95, 20, 70, 32, 42}, {0, 64, 42, 32, 64, 80, 80, 80}},
     {85, 70, 95, 95, 0, 70, 0, 70}, 0, LFO_TIMBRE, 0, P_KEYTRACK | P_DELAY},
    {"FM bass", 12, {{0, 32, 64, 42, 32, 95, 40, 0}, {32, 64, 20, 0, 20, 40, 40, 64}},
     {0, 64, 0, 32, 0, 64, 0, 32}, 0, LFO_TIMBRE, 0, P_KEYTRACK | P_VELCUT | P_GLIDE},
    {"glass mallets", 19, {{20, 42, 32, 32, 20, 64, 40, 0}, {0, 64, 40, 20, 95, 95, 80, 80}},
     {5, 90, 127, 95, 0, 70, 0, 70}, 2, LFO_TIMBRE, 0, P_KEYTRACK},
    {"chord stabs", 27, {{0, 64, 64, 100, 20, 64, 40, 0}, {0, 95, 64, 40, 40, 64, 64, 80}},
     {0, 64, 0, 50, 0, 70, 0, 70}, 0, LFO_TIMBRE, 0, P_KEYTRACK | P_DELAY},
    {"granular weather", 31, {{40, 64, 95, 90, 30, 95, 40, 60}, {0, 70, 50, 30, 80, 90, 80, 70}},
     {40, 70, 100, 90, 10, 64, 0, 64}, 1, LFO_COLOR, 4, P_KEYTRACK | P_CYCLE},
    {"pitched rain", 32, {{30, 32, 127, 100, 20, 64, 40, 0}, {0, 80, 95, 50, 95, 90, 90, 90}},
     {0, 70, 100, 80, 0, 70, 0, 70}, 0, LFO_TIMBRE, 0, P_KEYTRACK | P_DELAY},
    {"chaotic drone", 14, {{25, 50, 70, 80, 60, 80, 20, 40}, {0, 100, 80, 40, 90, 110, 100, 60}},
     {90, 90, 110, 100, 60, 80, 50, 90}, 1, LFO_CUTOFF, 1, P_KEYTRACK | P_DELAY | P_CYCLE},
};
const int kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);

// Events for the patcher log (launchkey_ctl.axo prints them), to debug the
// mapping on the board: page changes (with the CC that caused them), resets,
// presets, dice rolls, controls catching up, and CCs the mapping does not
// handle (each (channel, CC) once).
enum { EV_PAGE, EV_RESET, EV_PICKED, EV_UNMAPPED, EV_PRESET, EV_DICE };
const uint8_t kPickedFader = 2;  // EV_PICKED a: page 0/1 for knobs, 2 for faders
struct Event { uint8_t type, a, b, c; };
const int kEventQueue = 16;

// CC value -> unit interval
inline float unit(uint8_t v) { return (float)v * (1.0f / 127.0f); }
// Envelope time: 1 ms * 10^(4 v/127), so 1 ms .. 10 s, a decade per 32 steps.
inline float env_time(uint8_t v) { return 0.001f * fast_exp2(13.2877124f * unit(v)); }  // log2(10^4)

// Performance pads (bottom row): each acts only while held, by an amount set
// by the strike velocity and then by the pad's pressure (poly aftertouch).
enum { PERF_SWEEP, PERF_DIVE, PERF_VIBRATO, PERF_OCTAVE, PERF_THROW, PERF_SWELL, PERF_TIMBRE, PERF_COLOR, NUM_PERF };
const float kVibratoHz = 6.0f;

struct Launchkey {
    uint8_t knob[NUM_PAGES][8];
    bool picked[NUM_PAGES][8];
    int16_t anchor_phys[NUM_PAGES][8];  // knob position when pickup started, -1 = not yet seen
    uint8_t anchor_val[NUM_PAGES][8];   // stored value at that moment
    uint8_t phys[8];     // last position of each physical knob
    bool phys_known[8];
    uint8_t fader[NUM_FADERS];          // faders use the same scaled pickup
    bool fpicked[NUM_FADERS];
    int16_t fanchor_phys[NUM_FADERS];
    uint8_t fanchor_val[NUM_FADERS];
    uint8_t fphys[NUM_FADERS];
    bool fphys_known[NUM_FADERS];
    int page;
    int preset;
    int shape;
    int fmode, lfo_target, lfo_wave;
    bool modtimbre, glide, keytrack, velcut, delay_on, cycle;
    float bend;          // -1..1
    float modwheel;      // 0..1
    float lfo_hz;        // set by knob 7 or by tap tempo, whichever was last
    uint32_t ticks, last_tap;
    uint32_t rng;        // dice
    Lfo lfo;
    float perf[NUM_PERF];  // performance pad amounts, 0..1 (0 = not held)
    float vib_phase;
    Event events[kEventQueue];
    uint8_t ev_head, ev_count;
    uint32_t unmapped_seen[16][4];  // bit (ch, cc): already reported

    void push_event(uint8_t type, uint8_t a, uint8_t b, uint8_t c) {
        if (ev_count == kEventQueue) return;  // full: drop
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
    void unmapped(uint8_t ch, uint8_t num, uint8_t v) {
        uint32_t &w = unmapped_seen[ch][num >> 5];
        const uint32_t bit = 1u << (num & 31);
        if (w & bit) return;
        w |= bit;
        push_event(EV_UNMAPPED, (uint8_t)(ch + 1), num, v);
    }

    // The sound part of a preset: knob values, envelope faders, shape, toggles.
    void load(const Preset &p) {
        for (int g = 0; g < NUM_PAGES; g++)
            for (int k = 0; k < 8; k++) knob[g][k] = p.knob[g][k];
        for (int f = 0; f < 8; f++) fader[f] = p.fader[f];
        shape = p.shape;
        fmode = p.fmode; lfo_target = p.lfo_target; lfo_wave = p.lfo_wave;
        modtimbre = p.flags & P_MODTIMBRE; glide = p.flags & P_GLIDE; keytrack = p.flags & P_KEYTRACK;
        velcut = p.flags & P_VELCUT; delay_on = p.flags & P_DELAY; cycle = p.flags & P_CYCLE;
        lfo_hz = lfo_rate(knob[PAGE_SOUND][K_LFORATE]);
    }

    void init() {
        for (int g = 0; g < NUM_PAGES; g++)
            for (int k = 0; k < 8; k++) {
                picked[g][k] = (g == PAGE_SOUND);  // power-on page: knobs jump
                anchor_phys[g][k] = -1;
            }
        for (int k = 0; k < 8; k++) { phys[k] = 0; phys_known[k] = false; }
        for (int f = 0; f < NUM_FADERS; f++) {
            fpicked[f] = true;  // power-on: faders jump
            fanchor_phys[f] = -1;
            fphys[f] = 0;
            fphys_known[f] = false;
        }
        fader[F_MASTER] = kFaderInit[F_MASTER];
        preset = 0;
        load(kPresets[0]);
        for (int g = 0; g < NUM_PAGES; g++)
            for (int k = 0; k < 8; k++) anchor_val[g][k] = knob[g][k];
        for (int f = 0; f < NUM_FADERS; f++) fanchor_val[f] = fader[f];
        page = PAGE_SOUND;
        for (int i = 0; i < NUM_PERF; i++) perf[i] = 0.0f;
        vib_phase = 0.0f;
        bend = 0.0f; modwheel = 0.0f;
        ticks = 0; last_tap = 0;
        rng = 0x9E3779B9u;
        lfo.init();
        ev_head = ev_count = 0;
        for (int c = 0; c < 16; c++)
            for (int w = 0; w < 4; w++) unmapped_seen[c][w] = 0;
    }

    // After values change under the controls (page switch, preset, dice,
    // reset): a control more than 2 steps from its value is "not caught up"
    // and uses scaled pickup, anchored where it physically is.
    void reanchor_knobs(int g) {
        for (int k = 0; k < 8; k++) {
            const int d = (int)phys[k] - (int)knob[g][k];
            picked[g][k] = phys_known[k] && d >= -2 && d <= 2;
            anchor_phys[g][k] = phys_known[k] ? phys[k] : -1;
            anchor_val[g][k] = knob[g][k];
        }
    }
    void reanchor_faders() {
        for (int f = 0; f < 8; f++) {  // not the volume fader: presets keep it
            const int d = (int)fphys[f] - (int)fader[f];
            fpicked[f] = fphys_known[f] && d >= -2 && d <= 2;
            fanchor_phys[f] = fphys_known[f] ? fphys[f] : -1;
            fanchor_val[f] = fader[f];
        }
    }

    void apply_preset(int i) {
        preset = i;
        load(kPresets[i]);
        reanchor_knobs(page);  // the other page is re-anchored when it comes up
        reanchor_faders();
    }

    // Undo: back to the power-on sound (preset 0), keeping page and volume.
    void reset_sound() {
        apply_preset(0);
        push_event(EV_RESET, 0, 0, 0);
    }

    // Fader button 9: the next preset, wrapping around.
    void next_preset() {
        apply_preset((preset + 1) % kNumPresets);
        push_event(EV_PRESET, (uint8_t)preset, 0, 0);
    }

    // Dice (Capture MIDI button): a random SOUND page within musical ranges.
    // Amp envelope, FX page and volume are left alone, so it stays playable;
    // Undo gets the power-on sound back. Seeded by the press time.
    uint32_t rnd() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    int rnd_in(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }
    void dice() {
        rng ^= ticks * 2654435761u;
        if (rng == 0) rng = 1;
        shape = rnd_in(0, kNumShapes - 1);
        uint8_t *s = knob[PAGE_SOUND];
        s[K_SPICE] = (uint8_t)rnd_in(0, 50);
        s[K_TIMBRE] = (uint8_t)rnd_in(0, 127);
        s[K_COLOR] = (uint8_t)rnd_in(0, 127);
        s[K_CUTOFF] = (uint8_t)rnd_in(50, 120);
        s[K_RESO] = (uint8_t)rnd_in(0, 80);
        s[K_ENVAMT] = (uint8_t)rnd_in(30, 115);
        s[K_LFORATE] = (uint8_t)rnd_in(0, 100);
        s[K_LFOAMT] = (uint8_t)rnd_in(0, 50);
        static const uint8_t modes[4] = {0, 0, 1, 2};  // low-pass half of the time
        fmode = modes[rnd_in(0, 3)];
        lfo_target = rnd_in(0, NUM_LFO_TARGETS - 1);
        lfo_wave = rnd_in(0, kNumLfoWaves - 1);
        modtimbre = rnd_in(0, 1);
        velcut = rnd_in(0, 1);
        cycle = rnd_in(0, 3) == 0;
        lfo_hz = lfo_rate(s[K_LFORATE]);
        if (page == PAGE_SOUND) reanchor_knobs(PAGE_SOUND);
        push_event(EV_DICE, (uint8_t)shape, 0, 0);
    }

    // LFO rate: 0.05 Hz * 400^(v/127), so 0.05 .. 20 Hz.
    static float lfo_rate(uint8_t v) { return 0.05f * fast_exp2(8.64385619f * unit(v)); }  // log2(400)

    // Page select: Device Select (CC 106) or arrow up (51) = SOUND, Device
    // Lock (107) or arrow down (52) = FX. src: the CC, for the log.
    void set_page(int p, uint8_t src) {
        if (p != page) push_event(EV_PAGE, (uint8_t)p, src, 0);
        page = p;
        reanchor_knobs(p);
    }

    // Scaled pickup. With the anchor (a, s) = (control position, stored value)
    // at the start of the pickup, the value is
    //   f(v) = s + (v - a)(127 - s)/(127 - a)  for v >= a,
    //   f(v) = s - (a - v) s / a               for v <= a.
    // f is continuous and nondecreasing, f(a) = s, f(0) = 0, f(127) = 127. So
    // the value always moves the way the control turns (no dead control), there
    // is no jump, and control and value meet at an end of travel at the latest;
    // they are caught up as soon as |f(v) - v| <= 1. Both branches round away
    // from s (a ceiling of a nondecreasing function, so still nondecreasing),
    // so the first step from the anchor always moves the value. Computing f
    // from the anchor, not step by step, keeps slow turns from rounding to zero.
    static int scaled(int v, int a, int s) {
        if (v >= a) return a >= 127 ? s : s + ((v - a) * (127 - s) + (127 - a) - 1) / (127 - a);
        return a <= 0 ? s : s - ((a - v) * s + a - 1) / a;
    }

    // One control under scaled pickup. Returns true at the step it catches up.
    // At that step the value stays at f (setting v could step back against the
    // turn); afterwards it follows exactly.
    static bool follow(uint8_t &val, bool &picked, int16_t &anchor, uint8_t &aval, uint8_t v) {
        if (picked) { val = v; return false; }
        if (anchor < 0) { anchor = v; aval = val; }  // first position seen
        const int f = scaled(v, anchor, aval);
        val = (uint8_t)f;
        if (f - (int)v >= -1 && f - (int)v <= 1) { picked = true; return true; }
        return false;
    }

    void on_knob(int k, uint8_t v) {
        const int p = page;
        if (follow(knob[p][k], picked[p][k], anchor_phys[p][k], anchor_val[p][k], v))
            push_event(EV_PICKED, (uint8_t)p, (uint8_t)k, knob[p][k]);
        phys[k] = v;
        phys_known[k] = true;
        if (p == PAGE_SOUND && k == K_LFORATE) lfo_hz = lfo_rate(knob[p][k]);
    }

    void on_fader(int f, uint8_t v) {
        if (follow(fader[f], fpicked[f], fanchor_phys[f], fanchor_val[f], v))
            push_event(EV_PICKED, kPickedFader, (uint8_t)f, fader[f]);
        fphys[f] = v;
        fphys_known[f] = true;
    }

    // Tap tempo (Click button): two taps 0.1..4 s apart set the LFO period.
    void on_tap() {
        const uint32_t dt = ticks - last_tap;
        if (last_tap != 0 && dt >= (uint32_t)(0.1f * kControlRate) && dt <= (uint32_t)(4.0f * kControlRate))
            lfo_hz = kControlRate / (float)dt;
        last_tap = ticks == 0 ? 1 : ticks;
    }

    void on_button(int b) {  // fader buttons, on press
        switch (b) {
        case B_FMODE: fmode = (fmode + 1) % 3; break;
        case B_LFOTGT: lfo_target = (lfo_target + 1) % NUM_LFO_TARGETS; break;
        case B_LFOWAVE: lfo_wave = (lfo_wave + 1) % kNumLfoWaves; break;
        case B_MODTIMBRE: modtimbre = !modtimbre; break;
        case B_GLIDE: glide = !glide; break;
        case B_KEYTRACK: keytrack = !keytrack; break;
        case B_VELCUT: velcut = !velcut; break;
        case B_DELAY: delay_on = !delay_on; break;
        case B_PRESET: next_preset(); break;
        }
    }

    // Pads, by position: top row 0..7 = presets, bottom row 8..15 = the
    // performance pads (PERF_*), left to right.
    static int pad_index(uint8_t note) {  // -1 if not a pad
        static const int8_t idx[16] = {8, 9, 10, 11, 0, 1, 2, 3, 12, 13, 14, 15, 4, 5, 6, 7};  // note 36..51
        return (note >= 36 && note <= 51) ? idx[note - 36] : -1;
    }

    // Pad events: strike, pressure (poly aftertouch) and release (note on with
    // velocity 0, which is what the Launchkey sends, or note off).
    void on_pad(uint8_t note, uint8_t v, bool pressure) {
        const int i = pad_index(note);
        if (i < 0) return;
        if (i < 8) {  // top row: preset recall, on the strike only
            if (!pressure && v > 0) {
                apply_preset(i);
                push_event(EV_PRESET, (uint8_t)i, 0, 0);
            }
            return;
        }
        perf[i - 8] = unit(v);  // strike sets the amount, pressure then follows
    }

    // Raw MIDI from the Launchkey's MIDI port.
    void midi(uint8_t status, uint8_t d1, uint8_t d2) {
        const uint8_t type = status & 0xF0, ch = status & 0x0F;
        if (type == 0xB0 && ch == 0) {
            if (d1 >= 21 && d1 <= 28) on_knob(d1 - 21, d2);
            else if (d1 >= 71 && d1 <= 79) on_fader(d1 - 71, d2);
            else if (d1 >= 11 && d1 <= 19) { if (d2 > 0) on_button(d1 - 11); }
            else if (d1 == 1) modwheel = unit(d2);
            else unmapped(ch, d1, d2);
        } else if (type == 0xB0 && ch != 15) {
            unmapped(ch, d1, d2);
        } else if (type == 0xE0 && ch == 0) {
            bend = (float)(((int)d2 << 7 | d1) - 8192) * (1.0f / 8192.0f);
        } else if (type == 0x90 && ch == 9) {
            on_pad(d1, d2, false);   // velocity 0 = release
        } else if (type == 0x80 && ch == 9) {
            on_pad(d1, 0, false);
        } else if (type == 0xA0 && ch == 9) {
            on_pad(d1, d2, true);    // pad pressure
        } else if (type == 0xB0 && ch == 15 && d2 > 0) {
            switch (d1) {
            case 51: case 106: set_page(PAGE_SOUND, d1); break;  // Device Select (51); 106 too
            case 52: case 107: set_page(PAGE_FX, d1); break;     // Device Lock (52); 107 too
            case 102: shape = shape > 0 ? shape - 1 : kNumShapes - 1; break;  // track <
            case 103: shape = shape < kNumShapes - 1 ? shape + 1 : 0; break;  // track >
            case 74: dice(); break;                 // Capture MIDI: dice
            case 76: on_tap(); break;               // Click: tap tempo
            case 77: reset_sound(); break;          // Undo: back to the power-on sound
            case 118: cycle = !cycle; break;        // Loop: cycling mod envelope
            case 75: case 104: case 105: case 115: case 116: case 117: break;  // free
            default: unmapped(ch, d1, d2);
            }
        }
    }

    // Once per control tick: advance the LFOs, then publish to the bus.
    void tick() {
        ticks++;
        Bus &b = bus();
        const uint8_t *s = knob[PAGE_SOUND], *x = knob[PAGE_FX];
        const float amt = clampf(unit(s[K_LFOAMT]) + modwheel, 0.0f, 1.0f);
        const float l = amt * lfo.tick(lfo_hz, lfo_wave);
        vib_phase += kVibratoHz / kControlRate;
        if (vib_phase >= 1.0f) vib_phase -= 1.0f;
        const float *pf = perf;
        const float vib = 0.7f * pf[PERF_VIBRATO] * fast_sin(2.0f * kPi * vib_phase);  // up to +-0.7 st

        b.shape = shape;
        b.spice = unit(s[K_SPICE]);
        b.timbre = clampf(unit(s[K_TIMBRE]) + (lfo_target == LFO_TIMBRE ? 0.5f * l : 0.0f) + 0.5f * pf[PERF_TIMBRE], 0.0f, 1.0f);
        b.color = clampf(unit(s[K_COLOR]) + (lfo_target == LFO_COLOR ? 0.5f * l : 0.0f) + 0.5f * pf[PERF_COLOR], 0.0f, 1.0f);
        b.cutoff_oct = 10.0f * unit(s[K_CUTOFF]) + (lfo_target == LFO_CUTOFF ? 3.0f * l : 0.0f) + 4.0f * pf[PERF_SWEEP];
        b.q = 0.5f * fast_exp2(5.32192809f * unit(s[K_RESO]));  // 0.5 * 40^v: 0.5 .. 20
        b.env_amt_oct = 5.0f * clampf(((float)s[K_ENVAMT] - 64.0f) / 63.0f, -1.0f, 1.0f);
        b.fmode = fmode;
        b.keytrack = keytrack; b.velcut = velcut; b.modtimbre = modtimbre; b.cycle = cycle;
        b.amp_a = env_time(fader[F_AA]); b.amp_d = env_time(fader[F_AD]);
        b.amp_s = unit(fader[F_AS]);     b.amp_r = env_time(fader[F_AR]);
        b.mod_a = env_time(fader[F_MA]); b.mod_d = env_time(fader[F_MD]);
        b.mod_s = unit(fader[F_MS]);     b.mod_r = env_time(fader[F_MR]);
        b.pitch_mod = 2.0f * bend + (lfo_target == LFO_PITCH ? 2.0f * l : 0.0f)
                    - 12.0f * pf[PERF_DIVE] + (pf[PERF_OCTAVE] > 0.0f ? 12.0f : 0.0f) + vib;
        b.glide = glide;
        b.glide_t = unit(x[K_GLIDE]) * unit(x[K_GLIDE]);  // 0 .. 1 s, finer at the short end
        // Delay throw: while held, the delay takes input even when switched
        // off, with more feedback and wet; on release the echoes ring out.
        const float th = pf[PERF_THROW];
        b.delay_on = delay_on || th > 0.0f;
        b.delay_t = 0.01f * fast_exp2(7.22881869f * unit(x[K_DTIME]));  // 10 ms .. 1.5 s (log2 150)
        b.delay_fb = clampf(0.95f * unit(x[K_DFB]) + 0.4f * th, 0.0f, 0.95f);
        b.delay_mix = th > 0.0f ? clampf(unit(x[K_DMIX]) + 0.6f * th, 0.0f, 1.0f) : unit(x[K_DMIX]);
        const float sw = pf[PERF_SWELL];
        b.rev_amount = clampf(unit(x[K_RAMOUNT]) + 0.7f * sw, 0.0f, 1.0f);
        b.rev_time = clampf(unit(x[K_RTIME]) + 0.3f * sw, 0.0f, 1.0f);
        b.rev_diff = unit(x[K_RDIFF]);     b.rev_lp = unit(x[K_RLP]);
        const float m = unit(fader[F_MASTER]);
        b.master = m * m;
    }
};

}  // namespace freak

#endif
