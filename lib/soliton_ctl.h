// soliton_ctl.h: maps a Launchkey 49 MK3 (standalone mode) onto the soliton
// synth, keeping freak's layout (lib/launchkey_ctl.h) wherever a function
// carries over. This file is the single source of truth for
// docs/soliton_cheatsheet.tex.
//
// As in freak: the SOUND knob page (SPICE, TIMBRE, COLOR, filter, LFO; here
// TIMBRE and COLOR are the engine's two parameters), the FX page, the faders
// (amp ADSR, mod ADSR, volume), the fader buttons (4 = MOD -> TIMBRE),
// Device Select / Lock = SOUND / FX page, Track < > = engine, Capture MIDI =
// dice, Click = tap tempo, Undo = reset, Loop = cycling mod envelope, top
// pads = presets, bottom pads = pressure performance pads, mod wheel = more
// LFO, pitch bend +-2 semitones. The keys go to the 3 voices of the
// polyphonic subpatch (midi/in/keyb), not through here.
// New: a FIELD knob page (round button 104): RATE, AMOUNT, VEL > AMOUNT,
// SEED, MOD > RATE, DAMP; a freeze latch on Quantise; the performance pads
// FREEZE, REWIND, FAST and KICK act on the field's time.
//
// Results go on two buses: freak's Bus (filter, envelopes, glide, pitch, FX,
// volume: so fx_delay and fx_reverb work unchanged) and the SolBus.
// Header-only, C++11, no allocation; every type is a POD.
#ifndef SOLITON_CTL_H
#define SOLITON_CTL_H

#include <stdint.h>

#include "launchkey_ctl.h"
#include "soliton_field.h"

namespace freak {

// What the soliton voices read besides freak's Bus.
struct SolBus {
    int engine;          // ENG_*
    float timbre, color; // 0..1, LFO and pads applied
    float rate;          // field time units per second; < 0 runs backwards (REWIND)
    float amount;        // 0..1: amplitude of a new note's field
    float veldepth;      // 0..1: velocity scales the amplitude by 2^(veldepth (2 vel - 1))
    float seed;          // 0..1: initial perturbation
    float modrate;       // -1..1: mod envelope scales the rate by 2^(4 modrate env)
    float damp;          // gamma, per time unit
    uint32_t kicks;      // counts KICK strikes; voices apply each new one
    uint32_t tick;       // control ticks since init (the voices take turns by its parity)
    float kick_amt;      // 0..1, the last kick's size
};

static SolBus g_sbus;  // one per patch, as freak's g_bus
inline SolBus &sbus() { return g_sbus; }

// Knob pages
enum { SPAGE_SOUND, SPAGE_FIELD, SPAGE_FX, NUM_SPAGES };
// SOUND: freak's K_SPICE, K_TIMBRE, K_COLOR, K_CUTOFF .. K_LFOAMT
// FIELD
enum { SK_RATE, SK_AMOUNT, SK_VELDEPTH, SK_SEED, SK_MODRATE, SK_DAMP };
// FX: freak's K_GLIDE .. K_RLP. Faders: freak's F_*. Fader buttons: freak's B_*.
enum { SLFO_TIMBRE, SLFO_RATE, SLFO_CUTOFF, SLFO_PITCH, NUM_SLFO_TARGETS };
// Performance pads, bottom row left to right
enum { SPERF_FREEZE, SPERF_REWIND, SPERF_FAST, SPERF_KICK, SPERF_SWEEP, SPERF_VIBRATO, SPERF_THROW, SPERF_SWELL, NUM_SPERF };
// Log events beyond freak's EV_* (same Event queue)
enum { EV_ENGINE = 16, EV_FREEZE_LATCH = 17 };

static const char *const kEngineNames[NUM_ENGINES] = {"SHOCK", "SOLITON", "CHAOS", "ROGUE"};

// Presets: engine, the three knob pages, the 8 envelope faders, the toggles.
// Not the volume. Chosen from the parameter meanings, not yet tuned by ear.
enum { SP_MODTIMBRE = 1, SP_GLIDE = 2, SP_KEYTRACK = 4, SP_VELCUT = 8, SP_DELAY = 16, SP_CYCLE = 32 };
struct SolPreset {
    const char *name;
    uint8_t engine;
    uint8_t knob[NUM_SPAGES][8];
    uint8_t fader[8];  // amp A D S R, mod A D S R
    uint8_t fmode, lfo_target, lfo_wave, flags;
};
// knob[SOUND]: spice, timbre, color, cutoff, reso, env amt, lfo rate, lfo amt
// knob[FIELD]: rate, amount, vel>amount, seed, mod>rate, damp, -, -
// knob[FX]:    glide, delay time, fb, mix, reverb amount, time, diffusion, lowpass
static const SolPreset kSolPresets[] = {
    {"init", ENG_SOLITON,
     {{0, 80, 0, 110, 10, 64, 40, 0}, {80, 80, 64, 20, 64, 0, 0, 0}, {24, 70, 50, 40, 40, 64, 64, 80}},
     {10, 70, 90, 70, 0, 70, 0, 70}, 0, SLFO_TIMBRE, 0, SP_KEYTRACK},
    {"shock pluck", ENG_SHOCK,
     {{10, 115, 0, 120, 10, 64, 40, 0}, {100, 110, 100, 10, 64, 0, 0, 0}, {0, 70, 40, 30, 40, 64, 64, 80}},
     {0, 90, 0, 70, 0, 80, 0, 60}, 0, SLFO_TIMBRE, 0, SP_KEYTRACK | SP_DELAY},
    {"driven saw", ENG_SHOCK,
     {{0, 95, 75, 95, 30, 80, 30, 20}, {80, 64, 40, 20, 64, 0, 0, 0}, {20, 64, 40, 30, 64, 80, 80, 80}},
     {30, 70, 110, 80, 40, 90, 60, 80}, 0, SLFO_TIMBRE, 1, SP_KEYTRACK},
    {"soliton train", ENG_SOLITON,
     {{0, 127, 0, 115, 10, 64, 40, 0}, {100, 100, 64, 20, 64, 0, 0, 0}, {0, 80, 60, 40, 64, 90, 80, 80}},
     {20, 70, 100, 80, 0, 70, 0, 70}, 0, SLFO_TIMBRE, 0, SP_KEYTRACK | SP_DELAY},
    {"bump fission", ENG_SOLITON,
     {{20, 100, 127, 110, 20, 64, 40, 0}, {80, 90, 80, 10, 100, 0, 0, 0}, {0, 70, 50, 30, 50, 70, 70, 80}},
     {0, 80, 100, 70, 0, 100, 0, 70}, 0, SLFO_TIMBRE, 0, SP_KEYTRACK},
    {"turbulence", ENG_CHAOS,
     {{20, 110, 0, 100, 30, 70, 20, 30}, {100, 90, 40, 80, 64, 0, 0, 0}, {0, 90, 70, 40, 80, 100, 90, 70}},
     {60, 70, 110, 100, 30, 80, 60, 80}, 0, SLFO_RATE, 4, SP_KEYTRACK | SP_DELAY},
    {"cells", ENG_CHAOS,
     {{0, 30, 90, 110, 20, 64, 40, 0}, {90, 64, 40, 20, 64, 0, 0, 0}, {0, 64, 40, 30, 50, 70, 70, 80}},
     {10, 70, 100, 70, 0, 70, 0, 70}, 0, SLFO_TIMBRE, 0, SP_KEYTRACK},
    {"rogue breather", ENG_ROGUE,
     {{10, 20, 0, 110, 20, 64, 30, 0}, {90, 115, 80, 70, 64, 0, 0, 0}, {10, 80, 60, 40, 70, 90, 80, 80}},
     {30, 70, 110, 90, 0, 70, 0, 70}, 0, SLFO_TIMBRE, 0, SP_KEYTRACK | SP_DELAY},
};
const int kNumSolPresets = sizeof(kSolPresets) / sizeof(kSolPresets[0]);

// Knob value maps (v = 0..127).
// RATE: 0.05 * 400^(v/127) field time units per second (log2 400 = 8.64).
inline float sol_rate(uint8_t v) { return 0.05f * fast_exp2(8.64385619f * unit(v)); }
// DAMP: 2 v^2 per time unit.
inline float sol_damp(uint8_t v) { return 2.0f * unit(v) * unit(v); }
// MOD > RATE: bipolar around the centre.
inline float sol_modrate(uint8_t v) { return clampf(((float)v - 64.0f) / 63.0f, -1.0f, 1.0f); }

// Compiled for size, as GrainsCtl (see there): runs once per block and on
// MIDI events.
#pragma GCC push_options
#pragma GCC optimize("Os")
struct SolitonCtl {
    uint8_t knob[NUM_SPAGES][8];
    bool picked[NUM_SPAGES][8];
    int16_t anchor_phys[NUM_SPAGES][8];
    uint8_t anchor_val[NUM_SPAGES][8];
    uint8_t phys[8];
    bool phys_known[8];
    uint8_t fader[NUM_FADERS];
    bool fpicked[NUM_FADERS];
    int16_t fanchor_phys[NUM_FADERS];
    uint8_t fanchor_val[NUM_FADERS];
    uint8_t fphys[NUM_FADERS];
    bool fphys_known[NUM_FADERS];
    int page, preset, engine;
    int fmode, lfo_target, lfo_wave;
    bool modtimbre, glide, keytrack, velcut, delay_on, cycle, freeze_latch;
    float bend, modwheel, lfo_hz;
    uint32_t ticks, last_tap, rng;
    Lfo lfo;
    float perf[NUM_SPERF];
    float vib_phase;
    uint32_t kicks;
    float kick_amt;
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

    void load(const SolPreset &p) {
        for (int g = 0; g < NUM_SPAGES; g++)
            for (int k = 0; k < 8; k++) knob[g][k] = p.knob[g][k];
        for (int f = 0; f < 8; f++) fader[f] = p.fader[f];
        engine = p.engine;
        fmode = p.fmode; lfo_target = p.lfo_target; lfo_wave = p.lfo_wave;
        modtimbre = p.flags & SP_MODTIMBRE; glide = p.flags & SP_GLIDE; keytrack = p.flags & SP_KEYTRACK;
        velcut = p.flags & SP_VELCUT; delay_on = p.flags & SP_DELAY; cycle = p.flags & SP_CYCLE;
        lfo_hz = Launchkey::lfo_rate(knob[SPAGE_SOUND][K_LFORATE]);
    }

    void init() {
        for (int g = 0; g < NUM_SPAGES; g++)
            for (int k = 0; k < 8; k++) {
                picked[g][k] = (g == SPAGE_SOUND);  // power-on page: knobs jump
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
        load(kSolPresets[0]);
        for (int g = 0; g < NUM_SPAGES; g++)
            for (int k = 0; k < 8; k++) anchor_val[g][k] = knob[g][k];
        for (int f = 0; f < NUM_FADERS; f++) fanchor_val[f] = fader[f];
        page = SPAGE_SOUND;
        freeze_latch = false;
        for (int i = 0; i < NUM_SPERF; i++) perf[i] = 0.0f;
        vib_phase = 0.0f;
        kicks = 0;
        kick_amt = 0.0f;
        bend = 0.0f; modwheel = 0.0f;
        ticks = 0; last_tap = 0;
        rng = 0x6A09E667u;
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
        load(kSolPresets[i]);
        reanchor_knobs(page);
        reanchor_faders();
        push_event(EV_PRESET, (uint8_t)i, 0, 0);
    }
    void reset_sound() {
        apply_preset(0);
        push_event(EV_RESET, 0, 0, 0);
    }
    void next_preset() { apply_preset((preset + 1) % kNumSolPresets); }

    uint32_t rnd() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    int rnd_in(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

    // Dice: random engine, SOUND and FIELD pages, within playable ranges.
    // Envelopes, FX page and volume stay; Undo gets preset 0 back.
    void dice() {
        rng ^= ticks * 2654435761u;
        if (rng == 0) rng = 1;
        engine = rnd_in(0, NUM_ENGINES - 1);
        uint8_t *s = knob[SPAGE_SOUND], *f = knob[SPAGE_FIELD];
        s[K_SPICE] = (uint8_t)rnd_in(0, 50);
        s[K_TIMBRE] = (uint8_t)rnd_in(0, 127);
        s[K_COLOR] = (uint8_t)rnd_in(0, 127);
        s[K_CUTOFF] = (uint8_t)rnd_in(60, 120);
        s[K_RESO] = (uint8_t)rnd_in(0, 80);
        s[K_ENVAMT] = (uint8_t)rnd_in(30, 115);
        s[K_LFORATE] = (uint8_t)rnd_in(0, 100);
        s[K_LFOAMT] = (uint8_t)rnd_in(0, 50);
        f[SK_RATE] = (uint8_t)rnd_in(50, 110);
        f[SK_AMOUNT] = (uint8_t)rnd_in(30, 127);
        f[SK_VELDEPTH] = (uint8_t)rnd_in(0, 127);
        f[SK_SEED] = (uint8_t)rnd_in(0, 80);
        f[SK_MODRATE] = (uint8_t)rnd_in(30, 100);
        f[SK_DAMP] = (uint8_t)rnd_in(0, 30);
        static const uint8_t modes[4] = {0, 0, 1, 2};  // low-pass half of the time
        fmode = modes[rnd_in(0, 3)];
        lfo_target = rnd_in(0, NUM_SLFO_TARGETS - 1);
        lfo_wave = rnd_in(0, kNumLfoWaves - 1);
        modtimbre = rnd_in(0, 1);
        velcut = rnd_in(0, 1);
        cycle = rnd_in(0, 3) == 0;
        lfo_hz = Launchkey::lfo_rate(s[K_LFORATE]);
        if (page != SPAGE_FX) reanchor_knobs(page);
        push_event(EV_DICE, (uint8_t)engine, 0, 0);
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
        if (p == SPAGE_SOUND && k == K_LFORATE) lfo_hz = Launchkey::lfo_rate(knob[p][k]);
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
        case B_LFOTGT: lfo_target = (lfo_target + 1) % NUM_SLFO_TARGETS; break;
        case B_LFOWAVE: lfo_wave = (lfo_wave + 1) % kNumLfoWaves; break;
        case B_MODTIMBRE: modtimbre = !modtimbre; break;
        case B_GLIDE: glide = !glide; break;
        case B_KEYTRACK: keytrack = !keytrack; break;
        case B_VELCUT: velcut = !velcut; break;
        case B_DELAY: delay_on = !delay_on; break;
        case B_PRESET: next_preset(); break;
        }
    }

    void step_engine(int dir) {
        engine = (engine + dir + NUM_ENGINES) % NUM_ENGINES;
        push_event(EV_ENGINE, (uint8_t)engine, 0, 0);
    }

    static int pad_index(uint8_t note) { return Launchkey::pad_index(note); }
    void on_pad(uint8_t note, uint8_t v, bool pressure) {
        const int i = pad_index(note);
        if (i < 0) return;
        if (i < 8) {
            if (!pressure && v > 0) apply_preset(i);
            return;
        }
        const int p = i - 8;
        if (p == SPERF_KICK && !pressure && v > 0) {  // a kick per strike
            kicks++;
            kick_amt = 0.3f + 0.7f * unit(v);
        }
        perf[p] = unit(v);
    }

    // Raw MIDI from the Launchkey's MIDI port. Notes on channel 1 are the
    // voices' (midi/in/keyb in the polyphonic subpatch).
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
            on_pad(d1, d2, false);
        } else if (type == 0x80 && ch == 9) {
            on_pad(d1, 0, false);
        } else if (type == 0xA0 && ch == 9) {
            on_pad(d1, d2, true);
        } else if (type == 0xB0 && ch == 15 && d2 > 0) {
            switch (d1) {
            case 51: case 106: set_page(SPAGE_SOUND, d1); break;  // Device Select
            case 52: case 107: set_page(SPAGE_FX, d1); break;     // Device Lock
            case 104: set_page(SPAGE_FIELD, d1); break;           // round button, upper
            case 102: step_engine(-1); break;                     // track <
            case 103: step_engine(+1); break;                     // track >
            case 75:                                              // Quantise
                freeze_latch = !freeze_latch;
                push_event(EV_FREEZE_LATCH, freeze_latch, 0, 0);
                break;
            case 74: dice(); break;            // Capture MIDI
            case 76: on_tap(); break;          // Click
            case 77: reset_sound(); break;     // Undo
            case 118: cycle = !cycle; break;   // Loop
            case 105: case 115: case 116: case 117: break;  // free
            default: unmapped(ch, d1, d2);
            }
        }
    }

    // Once per control tick: advance the LFO, then publish to both buses.
    void tick() {
        ticks++;
        Bus &b = bus();
        SolBus &g = sbus();
        const uint8_t *s = knob[SPAGE_SOUND], *f = knob[SPAGE_FIELD], *x = knob[SPAGE_FX];
        const float amt = clampf(unit(s[K_LFOAMT]) + modwheel, 0.0f, 1.0f);
        const float l = amt * lfo.tick(lfo_hz, lfo_wave);
        const float *pf = perf;
        vib_phase += kVibratoHz / kControlRate;
        if (vib_phase >= 1.0f) vib_phase -= 1.0f;
        const float vib = 0.7f * pf[SPERF_VIBRATO] * fast_sin(2.0f * kPi * vib_phase);

        g.engine = engine;
        g.timbre = clampf(unit(s[K_TIMBRE]) + (lfo_target == SLFO_TIMBRE ? 0.5f * l : 0.0f), 0.0f, 1.0f);
        g.color = unit(s[K_COLOR]);
        // Field time: RATE, the LFO (+-2 octaves), FAST (up to x8), FREEZE
        // (pressure slows it to a stop; the Quantise latch stops it), and
        // REWIND (backwards, faster with pressure; the field ignores it for
        // SHOCK and CHAOS, whose backward problems are ill-posed).
        float rate = sol_rate(f[SK_RATE]) * (lfo_target == SLFO_RATE ? fast_exp2(2.0f * l) : 1.0f);
        rate *= fast_exp2(3.0f * pf[SPERF_FAST]);
        rate *= 1.0f - pf[SPERF_FREEZE];
        if (pf[SPERF_REWIND] > 0.0f) rate = -rate * (0.5f + 1.5f * pf[SPERF_REWIND]);
        if (freeze_latch) rate = 0.0f;
        g.rate = rate;
        g.amount = unit(f[SK_AMOUNT]);
        g.veldepth = unit(f[SK_VELDEPTH]);
        g.seed = unit(f[SK_SEED]);
        g.modrate = sol_modrate(f[SK_MODRATE]);
        g.damp = sol_damp(f[SK_DAMP]);
        g.kicks = kicks;
        g.tick = ticks;
        g.kick_amt = kick_amt;

        b.spice = unit(s[K_SPICE]);
        b.cutoff_oct = 10.0f * unit(s[K_CUTOFF]) + (lfo_target == SLFO_CUTOFF ? 3.0f * l : 0.0f) + 4.0f * pf[SPERF_SWEEP];
        b.q = 0.5f * fast_exp2(5.32192809f * unit(s[K_RESO]));
        b.env_amt_oct = 5.0f * clampf(((float)s[K_ENVAMT] - 64.0f) / 63.0f, -1.0f, 1.0f);
        b.fmode = fmode;
        b.keytrack = keytrack; b.velcut = velcut; b.modtimbre = modtimbre; b.cycle = cycle;
        b.amp_a = env_time(fader[F_AA]); b.amp_d = env_time(fader[F_AD]);
        b.amp_s = unit(fader[F_AS]);     b.amp_r = env_time(fader[F_AR]);
        b.mod_a = env_time(fader[F_MA]); b.mod_d = env_time(fader[F_MD]);
        b.mod_s = unit(fader[F_MS]);     b.mod_r = env_time(fader[F_MR]);
        b.pitch_mod = 2.0f * bend + (lfo_target == SLFO_PITCH ? 2.0f * l : 0.0f) + vib;
        b.glide = glide;
        b.glide_t = unit(x[K_GLIDE]) * unit(x[K_GLIDE]);
        const float th = pf[SPERF_THROW];
        b.delay_on = delay_on || th > 0.0f;
        b.delay_t = 0.01f * fast_exp2(7.22881869f * unit(x[K_DTIME]));
        b.delay_fb = clampf(0.95f * unit(x[K_DFB]) + 0.4f * th, 0.0f, 0.95f);
        b.delay_mix = th > 0.0f ? clampf(unit(x[K_DMIX]) + 0.6f * th, 0.0f, 1.0f) : unit(x[K_DMIX]);
        const float sw = pf[SPERF_SWELL];
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
