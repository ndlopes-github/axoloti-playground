// grains_ctl.h: maps a Launchkey 49 MK3 (standalone mode) onto the grains
// synth, keeping freak's layout (lib/launchkey_ctl.h) wherever a function
// carries over. This file is the single source of truth for
// docs/grains_cheatsheet.tex.
//
// As in freak: faders (amp ADSR, mod ADSR, volume), fader buttons (except 4:
// MOD -> POSITION instead of timbre), the FX knob page, Device Select / Lock
// = SOUND / FX page, Capture MIDI = dice, Click = tap tempo, Undo = reset,
// Loop = cycling mod envelope, top pads = presets, bottom pads = pressure
// performance pads, mod wheel = more LFO, pitch bend +-2 semitones.
// New here: a GRAIN knob page (round button 104), HOLD (round button 105,
// or the sustain pedal), the grain modes on Play / Stop / Record, a freeze
// latch on Quantise, the source on track < >. The voice is mono: the keys
// are handled here, last-note priority, legato.
//
// Results go on two buses: freak's Bus (filter, envelopes, glide, pitch, FX,
// volume: so fx_reverb and the delays work unchanged) and the GrainBus.
// Header-only, C++11, no allocation; every type is a POD.
#ifndef GRAINS_CTL_H
#define GRAINS_CTL_H

#include <stdint.h>

#include "grains_bank.h"
#include "launchkey_ctl.h"

namespace freak {

// What the grains voice reads besides freak's Bus.
struct GrainBus {
    float note;          // MIDI note of the sounding key (the voice glides to it)
    float vel;           // 0..1
    bool gate;
    uint32_t strikes;    // counts new phrases: envelopes restart, SCAN restarts, SPICE redraws
    uint32_t notes;      // counts every note-on, legato too: HIT fires a burst on each
    int source;          // bank index (0 = built-in)
    int mode;            // GM_SCAN, GM_CLOUD, GM_HIT
    float position;      // 0..1: knob, LFO, SCRUB pad
    float size;          // s
    float density;       // grains per second (SCAN, CLOUD)
    int burst;           // grains per note (HIT)
    float spray;         // s
    float scatter;       // semitones
    float octave_prob, reverse_prob, stereo, shape;  // 0..1
    float scan_speed;    // x real time, 0 = still, < 0 backwards
    bool freeze;
    bool modpos;         // mod envelope -> position
};

static GrainBus g_gbus;  // one per patch, as freak's g_bus
inline GrainBus &gbus() { return g_gbus; }

// Knob pages
enum { GPAGE_SOUND, GPAGE_GRAIN, GPAGE_FX, NUM_GPAGES };
// SOUND: knobs 4..8 as in freak (K_CUTOFF .. K_LFOAMT)
enum { GK_SPICE, GK_POSITION, GK_SIZE };
// GRAIN
enum { GK_DENSITY, GK_SCAN, GK_SPRAY, GK_SCATTER, GK_STEREO, GK_SHAPE, GK_REVERSE, GK_OCTAVE };
// FX: freak's K_GLIDE .. K_RLP. Faders: freak's F_*. Fader buttons: freak's
// B_*, with B_MODTIMBRE meaning MOD -> POSITION.
enum { GLFO_POSITION, GLFO_SIZE, GLFO_CUTOFF, GLFO_PITCH, NUM_GLFO_TARGETS };
// Performance pads, bottom row left to right
enum { GPERF_FREEZE, GPERF_REVERSE, GPERF_SCRUB, GPERF_DENSITY, GPERF_SPRAY, GPERF_OCTAVE, GPERF_THROW, GPERF_SWELL, NUM_GPERF };
// Log events beyond freak's EV_* (same Event queue)
enum { EV_MODE = 16, EV_SOURCE, EV_HOLD, EV_FREEZE };

// Presets: mode, source (the starter bank's files 01..08 are sources 1..8),
// all three knob pages, the 8 envelope faders and the toggles. Not the volume.
// Chosen from the parameter meanings, not yet tuned by ear.
enum { GP_MODPOS = 1, GP_GLIDE = 2, GP_KEYTRACK = 4, GP_VELCUT = 8, GP_DELAY = 16, GP_CYCLE = 32 };
struct GrainPreset {
    const char *name;
    uint8_t mode, source;
    uint8_t knob[NUM_GPAGES][8];
    uint8_t fader[8];  // amp A D S R, mod A D S R
    uint8_t fmode, lfo_target, lfo_wave, flags;
};
static const GrainPreset kGrainPresets[] = {
    {"init cloud", GM_CLOUD, 1,
     {{0, 64, 80, 100, 20, 64, 40, 0}, {80, 64, 30, 0, 64, 0, 0, 0}, {24, 70, 50, 40, 50, 64, 64, 80}},
     {40, 70, 100, 80, 0, 70, 0, 70}, 0, GLFO_POSITION, 0, GP_KEYTRACK},
    {"time stretch", GM_SCAN, 2,
     {{0, 0, 85, 110, 10, 64, 30, 0}, {85, 97, 10, 0, 40, 0, 0, 0}, {0, 64, 40, 30, 60, 80, 80, 80}},
     {30, 70, 110, 90, 0, 70, 0, 70}, 0, GLFO_POSITION, 0, GP_KEYTRACK | GP_DELAY},
    {"frozen choir", GM_SCAN, 1,
     {{20, 40, 100, 95, 20, 64, 20, 30}, {95, 64, 60, 20, 100, 0, 0, 30}, {0, 80, 60, 40, 90, 100, 100, 70}},
     {80, 70, 120, 110, 0, 70, 0, 70}, 0, GLFO_POSITION, 1, GP_KEYTRACK | GP_DELAY},
    {"glass hits", GM_HIT, 4,
     {{30, 33, 64, 110, 20, 80, 40, 0}, {70, 64, 20, 30, 80, 100, 0, 40}, {0, 70, 60, 50, 60, 70, 70, 80}},
     {0, 90, 0, 80, 0, 50, 0, 50}, 0, GLFO_PITCH, 0, GP_KEYTRACK | GP_DELAY},
    {"reverse swarm", GM_CLOUD, 8,
     {{20, 70, 90, 90, 30, 64, 40, 20}, {90, 64, 50, 10, 110, 30, 110, 0}, {20, 80, 60, 40, 70, 90, 80, 70}},
     {60, 70, 110, 100, 0, 70, 0, 70}, 0, GLFO_SIZE, 4, GP_KEYTRACK},
    {"particle rain", GM_CLOUD, 5,
     {{40, 64, 50, 100, 40, 90, 60, 40}, {110, 64, 90, 60, 127, 90, 20, 60}, {0, 75, 70, 50, 80, 90, 90, 90}},
     {10, 70, 100, 90, 0, 60, 0, 60}, 0, GLFO_CUTOFF, 4, GP_KEYTRACK | GP_DELAY},
    {"octave shimmer", GM_CLOUD, 3,
     {{10, 80, 95, 100, 20, 64, 25, 25}, {100, 64, 40, 10, 100, 0, 0, 100}, {0, 90, 70, 40, 100, 110, 100, 80}},
     {90, 70, 120, 110, 0, 70, 0, 70}, 0, GLFO_POSITION, 0, GP_KEYTRACK | GP_DELAY},
    {"scrub drone", GM_SCAN, 7,
     {{30, 50, 70, 90, 50, 80, 15, 50}, {90, 70, 20, 5, 60, 20, 30, 10}, {40, 100, 80, 50, 80, 100, 90, 60}},
     {70, 90, 110, 110, 60, 90, 60, 90}, 0, GLFO_POSITION, 1, GP_KEYTRACK | GP_CYCLE | GP_GLIDE},
};
const int kNumGrainPresets = sizeof(kGrainPresets) / sizeof(kGrainPresets[0]);

// Knob value maps (v = 0..127). Exponential where the ear is logarithmic.
inline float grain_size(uint8_t v) { return 0.005f * fast_exp2(7.64385619f * unit(v)); }     // 5 ms .. 1 s (log2 200)
inline float grain_density(uint8_t v) { return 0.5f * fast_exp2(8.64385619f * unit(v)); }   // 0.5 .. 200 /s (log2 400)
inline int grain_burst(uint8_t v) { return 1 + (int)(unit(v) * (float)(kMaxGrains - 1) + 0.5f); }  // 1 .. kMaxGrains
inline float grain_spray(uint8_t v) { return 0.5f * unit(v) * unit(v); }                    // 0 .. 0.5 s
inline float grain_scatter(uint8_t v) { return 12.0f * unit(v) * unit(v); }                 // 0 .. 12 st
// Scan speed: centre (61..67) = still; either side 1/16 .. 4 x real time,
// exponential, backwards to the left. x1 at 107 (and -1 at 21).
inline float scan_speed(uint8_t v) {
    const float d = clampf(((float)v - 64.0f) / 63.0f, -1.0f, 1.0f);
    const float m = absf(d);
    if (m < 0.06f) return 0.0f;
    const float s = fast_exp2(6.0f * (m - 0.06f) / 0.94f - 4.0f);
    return d < 0.0f ? -s : s;
}

const int kNoteStack = 16;

// The controller runs once per block and on MIDI events, so it is compiled
// for size: at the patch's -O3 the inlining made it ~17 KB of the 44 KB of
// patch RAM (SRAM1). GCC does not inline across differing optimisation
// options, so the callers in the generated patch code stay small too.
#pragma GCC push_options
#pragma GCC optimize("Os")
struct GrainsCtl {
    uint8_t knob[NUM_GPAGES][8];
    bool picked[NUM_GPAGES][8];
    int16_t anchor_phys[NUM_GPAGES][8];
    uint8_t anchor_val[NUM_GPAGES][8];
    uint8_t phys[8];
    bool phys_known[8];
    uint8_t fader[NUM_FADERS];
    bool fpicked[NUM_FADERS];
    int16_t fanchor_phys[NUM_FADERS];
    uint8_t fanchor_val[NUM_FADERS];
    uint8_t fphys[NUM_FADERS];
    bool fphys_known[NUM_FADERS];
    int page, preset, mode, source;
    int fmode, lfo_target, lfo_wave;
    bool modpos, glide, keytrack, velcut, delay_on, cycle, freeze_latch, hold, sustain;
    // Keys: held notes in press order (the last one sounds), and the note
    // that sounds while HOLD or the pedal keeps the gate open.
    uint8_t stack[kNoteStack];
    int nstack;
    uint8_t sounding;
    float vel;
    bool gate;
    uint32_t strikes, notes;
    float bend, modwheel, lfo_hz;
    uint32_t ticks, last_tap, rng;
    Lfo lfo;
    float perf[NUM_GPERF];
    Event events[kEventQueue];
    uint8_t ev_head, ev_count;
    uint32_t unmapped_seen[16][4];

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
    void unmapped(uint8_t ch, uint8_t num, uint8_t v) {
        uint32_t &w = unmapped_seen[ch][num >> 5];
        const uint32_t bit = 1u << (num & 31);
        if (w & bit) return;
        w |= bit;
        push_event(EV_UNMAPPED, (uint8_t)(ch + 1), num, v);
    }

    void load(const GrainPreset &p) {
        for (int g = 0; g < NUM_GPAGES; g++)
            for (int k = 0; k < 8; k++) knob[g][k] = p.knob[g][k];
        for (int f = 0; f < 8; f++) fader[f] = p.fader[f];
        mode = p.mode;
        source = p.source;
        fmode = p.fmode; lfo_target = p.lfo_target; lfo_wave = p.lfo_wave;
        modpos = p.flags & GP_MODPOS; glide = p.flags & GP_GLIDE; keytrack = p.flags & GP_KEYTRACK;
        velcut = p.flags & GP_VELCUT; delay_on = p.flags & GP_DELAY; cycle = p.flags & GP_CYCLE;
        lfo_hz = Launchkey::lfo_rate(knob[GPAGE_SOUND][K_LFORATE]);
    }

    void init() {
        for (int g = 0; g < NUM_GPAGES; g++)
            for (int k = 0; k < 8; k++) {
                picked[g][k] = (g == GPAGE_SOUND);  // power-on page: knobs jump
                anchor_phys[g][k] = -1;
            }
        for (int k = 0; k < 8; k++) { phys[k] = 0; phys_known[k] = false; }
        for (int f = 0; f < NUM_FADERS; f++) {
            fpicked[f] = true;
            fanchor_phys[f] = -1;
            fphys[f] = 0;
            fphys_known[f] = false;
        }
        fader[F_MASTER] = kFaderInit[F_MASTER];
        preset = 0;
        load(kGrainPresets[0]);
        for (int g = 0; g < NUM_GPAGES; g++)
            for (int k = 0; k < 8; k++) anchor_val[g][k] = knob[g][k];
        for (int f = 0; f < NUM_FADERS; f++) fanchor_val[f] = fader[f];
        page = GPAGE_SOUND;
        freeze_latch = hold = sustain = false;
        nstack = 0;
        sounding = 60;
        vel = 0.0f;
        gate = false;
        strikes = notes = 0;
        for (int i = 0; i < NUM_GPERF; i++) perf[i] = 0.0f;
        bend = 0.0f; modwheel = 0.0f;
        ticks = 0; last_tap = 0;
        rng = 0x7F4A7C15u;
        lfo.init();
        ev_head = ev_count = 0;
        for (int c = 0; c < 16; c++)
            for (int w = 0; w < 4; w++) unmapped_seen[c][w] = 0;
    }

    // Pickup, as freak (Launchkey::reanchor_knobs / reanchor_faders).
    void reanchor_knobs(int g) {
        for (int k = 0; k < 8; k++) {
            const int d = (int)phys[k] - (int)knob[g][k];
            picked[g][k] = phys_known[k] && d >= -2 && d <= 2;
            anchor_phys[g][k] = phys_known[k] ? phys[k] : -1;
            anchor_val[g][k] = knob[g][k];
        }
    }
    void reanchor_faders() {
        for (int f = 0; f < 8; f++) {
            const int d = (int)fphys[f] - (int)fader[f];
            fpicked[f] = fphys_known[f] && d >= -2 && d <= 2;
            fanchor_phys[f] = fphys_known[f] ? fphys[f] : -1;
            fanchor_val[f] = fader[f];
        }
    }

    void apply_preset(int i) {
        preset = i;
        load(kGrainPresets[i]);
        reanchor_knobs(page);
        reanchor_faders();
        push_event(EV_PRESET, (uint8_t)i, 0, 0);
    }
    void reset_sound() {
        apply_preset(0);
        push_event(EV_RESET, 0, 0, 0);
    }
    void next_preset() { apply_preset((preset + 1) % kNumGrainPresets); }

    uint32_t rnd() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    int rnd_in(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

    // Dice: random SOUND and GRAIN pages and mode, within playable ranges.
    // Source, envelopes, FX page and volume stay; Undo gets preset 0 back.
    void dice() {
        rng ^= ticks * 2654435761u;
        if (rng == 0) rng = 1;
        mode = rnd_in(0, NUM_GMODES - 1);
        uint8_t *s = knob[GPAGE_SOUND], *g = knob[GPAGE_GRAIN];
        s[GK_SPICE] = (uint8_t)rnd_in(0, 60);
        s[GK_POSITION] = (uint8_t)rnd_in(0, 127);
        s[GK_SIZE] = (uint8_t)rnd_in(40, 110);
        s[K_CUTOFF] = (uint8_t)rnd_in(60, 120);
        s[K_RESO] = (uint8_t)rnd_in(0, 70);
        s[K_ENVAMT] = (uint8_t)rnd_in(40, 100);
        s[K_LFORATE] = (uint8_t)rnd_in(0, 100);
        s[K_LFOAMT] = (uint8_t)rnd_in(0, 60);
        g[GK_DENSITY] = (uint8_t)rnd_in(50, 115);
        g[GK_SCAN] = (uint8_t)rnd_in(0, 127);
        g[GK_SPRAY] = (uint8_t)rnd_in(0, 90);
        g[GK_SCATTER] = (uint8_t)rnd_in(0, 60);
        g[GK_STEREO] = (uint8_t)rnd_in(30, 127);
        g[GK_SHAPE] = (uint8_t)rnd_in(0, 127);
        g[GK_REVERSE] = (uint8_t)rnd_in(0, 80);
        g[GK_OCTAVE] = (uint8_t)rnd_in(0, 70);
        static const uint8_t modes[4] = {0, 0, 1, 2};  // low-pass half of the time
        fmode = modes[rnd_in(0, 3)];
        lfo_target = rnd_in(0, NUM_GLFO_TARGETS - 1);
        lfo_wave = rnd_in(0, kNumLfoWaves - 1);
        modpos = rnd_in(0, 1);
        cycle = rnd_in(0, 3) == 0;
        lfo_hz = Launchkey::lfo_rate(s[K_LFORATE]);
        if (page != GPAGE_FX) reanchor_knobs(page);
        push_event(EV_DICE, (uint8_t)mode, 0, 0);
    }

    void set_page(int p, uint8_t src) {
        if (p != page) push_event(EV_PAGE, (uint8_t)p, src, 0);
        page = p;
        reanchor_knobs(p);
    }

    void on_knob(int k, uint8_t v) {
        const int p = page;
        if (Launchkey::follow(knob[p][k], picked[p][k], anchor_phys[p][k], anchor_val[p][k], v))
            push_event(EV_PICKED, (uint8_t)p, (uint8_t)k, knob[p][k]);
        phys[k] = v;
        phys_known[k] = true;
        if (p == GPAGE_SOUND && k == K_LFORATE) lfo_hz = Launchkey::lfo_rate(knob[p][k]);
    }

    void on_fader(int f, uint8_t v) {
        if (Launchkey::follow(fader[f], fpicked[f], fanchor_phys[f], fanchor_val[f], v))
            push_event(EV_PICKED, kPickedFader, (uint8_t)f, fader[f]);
        fphys[f] = v;
        fphys_known[f] = true;
    }

    void on_tap() {
        const uint32_t dt = ticks - last_tap;
        if (last_tap != 0 && dt >= (uint32_t)(0.1f * kControlRate) && dt <= (uint32_t)(4.0f * kControlRate))
            lfo_hz = kControlRate / (float)dt;
        last_tap = ticks == 0 ? 1 : ticks;
    }

    void on_button(int b) {
        switch (b) {
        case B_FMODE: fmode = (fmode + 1) % 3; break;
        case B_LFOTGT: lfo_target = (lfo_target + 1) % NUM_GLFO_TARGETS; break;
        case B_LFOWAVE: lfo_wave = (lfo_wave + 1) % kNumLfoWaves; break;
        case B_MODTIMBRE: modpos = !modpos; break;
        case B_GLIDE: glide = !glide; break;
        case B_KEYTRACK: keytrack = !keytrack; break;
        case B_VELCUT: velcut = !velcut; break;
        case B_DELAY: delay_on = !delay_on; break;
        case B_PRESET: next_preset(); break;
        }
    }

    // Source select (track < >): steps through the sources that are loaded,
    // wrapping. A preset may name a source that is not (yet) there; the voice
    // then plays the built-in one, and the first step starts from the last
    // loaded source.
    void step_source(int dir, int count) {
        if (count <= 0) return;
        int s = source >= count ? count - 1 : source;
        s = (s + dir + count) % count;
        source = s;
        push_event(EV_SOURCE, (uint8_t)s, 0, 0);
    }

    // Keys. Last-note priority: the newest held key sounds; releasing it
    // goes back to the previous one, without restarting (legato). A key
    // pressed with no other key held starts a new phrase (strikes++). HOLD
    // or the sustain pedal keeps the last note sounding after release.
    bool held_open() const { return hold || sustain; }
    void remove_key(uint8_t n) {
        int j = 0;
        for (int i = 0; i < nstack; i++)
            if (stack[i] != n) stack[j++] = stack[i];
        nstack = j;
    }
    void key_on(uint8_t n, uint8_t v) {
        remove_key(n);  // a repeated note moves to the top
        if (nstack == kNoteStack) {  // full: drop the oldest
            for (int i = 1; i < kNoteStack; i++) stack[i - 1] = stack[i];
            nstack--;
        }
        const bool phrase = nstack == 0;  // no other key down: a new phrase, even when held open
        stack[nstack++] = n;
        sounding = n;
        vel = unit(v);
        gate = true;
        notes++;
        if (phrase) strikes++;
    }
    void key_off(uint8_t n) {
        remove_key(n);
        if (nstack > 0) sounding = stack[nstack - 1];
        else if (!held_open()) gate = false;
    }
    void release_hold() {  // HOLD off or pedal up: end the note if no key is down
        if (!held_open() && nstack == 0) gate = false;
    }

    static int pad_index(uint8_t note) { return Launchkey::pad_index(note); }
    void on_pad(uint8_t note, uint8_t v, bool pressure) {
        const int i = pad_index(note);
        if (i < 0) return;
        if (i < 8) {
            if (!pressure && v > 0) apply_preset(i);
            return;
        }
        perf[i - 8] = unit(v);
    }

    // Raw MIDI from the Launchkey's MIDI port. count: sources in the bank.
    void midi(uint8_t status, uint8_t d1, uint8_t d2, int count) {
        const uint8_t type = status & 0xF0, ch = status & 0x0F;
        if (type == 0x90 && ch == 0) {
            if (d2 > 0) key_on(d1, d2);
            else key_off(d1);
        } else if (type == 0x80 && ch == 0) {
            key_off(d1);
        } else if (type == 0xB0 && ch == 0) {
            if (d1 >= 21 && d1 <= 28) on_knob(d1 - 21, d2);
            else if (d1 >= 71 && d1 <= 79) on_fader(d1 - 71, d2);
            else if (d1 >= 11 && d1 <= 19) { if (d2 > 0) on_button(d1 - 11); }
            else if (d1 == 1) modwheel = unit(d2);
            else if (d1 == 64) { sustain = d2 >= 64; release_hold(); }
            else unmapped(ch, d1, d2);
        } else if (type == 0xB0 && ch != 15) {
            unmapped(ch, d1, d2);
        } else if (type == 0xE0 && ch == 0) {
            bend = (float)(((int)d2 << 7 | d1) - 8192) * (1.0f / 8192.0f);
        } else if (type == 0x90 && ch == 9) {
            on_pad(d1, d2, false);
        } else if (type == 0x80 && ch == 9) {
            on_pad(d1, 0, false);
        } else if (type == 0xA0 && ch == 9) {
            on_pad(d1, d2, true);
        } else if (type == 0xB0 && ch == 15 && d2 > 0) {
            switch (d1) {
            case 51: case 106: set_page(GPAGE_SOUND, d1); break;  // Device Select
            case 52: case 107: set_page(GPAGE_FX, d1); break;     // Device Lock
            case 104: set_page(GPAGE_GRAIN, d1); break;           // round button, upper
            case 105:                                              // round button, lower
                hold = !hold;
                release_hold();
                push_event(EV_HOLD, hold, 0, 0);
                break;
            case 102: step_source(-1, count); break;  // track <
            case 103: step_source(+1, count); break;  // track >
            case 115: mode = GM_SCAN; push_event(EV_MODE, (uint8_t)mode, 0, 0); break;   // Play
            case 116: mode = GM_CLOUD; push_event(EV_MODE, (uint8_t)mode, 0, 0); break;  // Stop
            case 117: mode = GM_HIT; push_event(EV_MODE, (uint8_t)mode, 0, 0); break;    // Record
            case 75:                                                                       // Quantise
                freeze_latch = !freeze_latch;
                push_event(EV_FREEZE, freeze_latch, 0, 0);
                break;
            case 74: dice(); break;            // Capture MIDI
            case 76: on_tap(); break;          // Click
            case 77: reset_sound(); break;     // Undo
            case 118: cycle = !cycle; break;   // Loop
            default: unmapped(ch, d1, d2);
            }
        }
    }

    // Once per control tick: advance the LFO, then publish to both buses.
    void tick() {
        ticks++;
        Bus &b = bus();
        GrainBus &g = gbus();
        const uint8_t *s = knob[GPAGE_SOUND], *k = knob[GPAGE_GRAIN], *x = knob[GPAGE_FX];
        const float amt = clampf(unit(s[K_LFOAMT]) + modwheel, 0.0f, 1.0f);
        const float l = amt * lfo.tick(lfo_hz, lfo_wave);
        const float *pf = perf;

        g.note = (float)sounding;
        g.vel = vel;
        g.gate = gate;
        g.strikes = strikes;
        g.notes = notes;
        g.source = source;
        g.mode = mode;
        // SCRUB: while held, its pressure is the position.
        const float pos = unit(s[GK_POSITION]) + (lfo_target == GLFO_POSITION ? 0.5f * l : 0.0f);
        g.position = clampf(pf[GPERF_SCRUB] > 0.0f ? pf[GPERF_SCRUB] : pos, 0.0f, 1.0f);
        g.size = grain_size(s[GK_SIZE]) * (lfo_target == GLFO_SIZE ? fast_exp2(2.0f * l) : 1.0f);
        g.density = grain_density(k[GK_DENSITY]) * (1.0f + 3.0f * pf[GPERF_DENSITY]);
        g.burst = grain_burst(k[GK_DENSITY]);
        g.spray = grain_spray(k[GK_SPRAY]) + 0.3f * pf[GPERF_SPRAY];
        g.scatter = grain_scatter(k[GK_SCATTER]);
        g.stereo = unit(k[GK_STEREO]);
        g.shape = unit(k[GK_SHAPE]);
        const float rv = unit(k[GK_REVERSE]);
        g.reverse_prob = rv + (1.0f - rv) * pf[GPERF_REVERSE];
        g.octave_prob = unit(k[GK_OCTAVE]);
        g.scan_speed = scan_speed(k[GK_SCAN]) * (pf[GPERF_REVERSE] > 0.0f ? -1.0f : 1.0f);
        g.freeze = freeze_latch || pf[GPERF_FREEZE] > 0.0f;
        g.modpos = modpos;

        b.spice = unit(s[GK_SPICE]);
        b.cutoff_oct = 10.0f * unit(s[K_CUTOFF]) + (lfo_target == GLFO_CUTOFF ? 3.0f * l : 0.0f);
        b.q = 0.5f * fast_exp2(5.32192809f * unit(s[K_RESO]));
        b.env_amt_oct = 5.0f * clampf(((float)s[K_ENVAMT] - 64.0f) / 63.0f, -1.0f, 1.0f);
        b.fmode = fmode;
        b.keytrack = keytrack; b.velcut = velcut; b.cycle = cycle;
        b.amp_a = env_time(fader[F_AA]); b.amp_d = env_time(fader[F_AD]);
        b.amp_s = unit(fader[F_AS]);     b.amp_r = env_time(fader[F_AR]);
        b.mod_a = env_time(fader[F_MA]); b.mod_d = env_time(fader[F_MD]);
        b.mod_s = unit(fader[F_MS]);     b.mod_r = env_time(fader[F_MR]);
        b.pitch_mod = 2.0f * bend + (lfo_target == GLFO_PITCH ? 2.0f * l : 0.0f) + (pf[GPERF_OCTAVE] > 0.0f ? 12.0f : 0.0f);
        b.glide = glide;
        b.glide_t = unit(x[K_GLIDE]) * unit(x[K_GLIDE]);
        const float th = pf[GPERF_THROW];
        b.delay_on = delay_on || th > 0.0f;
        b.delay_t = 0.01f * fast_exp2(7.22881869f * unit(x[K_DTIME]));
        b.delay_fb = clampf(0.95f * unit(x[K_DFB]) + 0.4f * th, 0.0f, 0.95f);
        b.delay_mix = th > 0.0f ? clampf(unit(x[K_DMIX]) + 0.6f * th, 0.0f, 1.0f) : unit(x[K_DMIX]);
        const float sw = pf[GPERF_SWELL];
        b.rev_amount = clampf(unit(x[K_RAMOUNT]) + 0.7f * sw, 0.0f, 1.0f);
        b.rev_time = clampf(unit(x[K_RTIME]) + 0.3f * sw, 0.0f, 1.0f);
        b.rev_diff = unit(x[K_RDIFF]);     b.rev_lp = unit(x[K_RLP]);
        const float m = unit(fader[F_MASTER]);
        b.master = m * m;
    }
};
#pragma GCC pop_options

}  // namespace freak

#endif
