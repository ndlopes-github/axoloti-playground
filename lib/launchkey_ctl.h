// launchkey_ctl.h: maps a Launchkey 49 MK3 (standalone mode) onto the
// controls of the "freak" synth. This file is the single source of truth for
// docs/cheatsheet.tex.
//
// MIDI in (recorded from the unit, docs/launchkey49mk3_map.md):
//   ch 1  CC 21-28 knobs      CC 71-79 faders     CC 11-19 fader buttons
//         CC 1 mod wheel      pitch bend          notes = keys (played by the voices)
//   ch 10 notes 36-51 pads (top row 40-43 48-51, bottom row 36-39 44-47)
//   ch 16 CC 51/52 arrows by the knobs, 102/103 track < >, 104/105 round
//         buttons right of the pads, 106/107 left of the pads,
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
    int shape;            // 0..33
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
enum { K_SHAPE, K_TIMBRE, K_COLOR, K_CUTOFF, K_RESO, K_ENVAMT, K_LFORATE, K_LFOAMT };
// PAGE_FX knobs
enum { K_GLIDE, K_DTIME, K_DFB, K_DMIX, K_RAMOUNT, K_RTIME, K_RDIFF, K_RLP };
// Faders 1..9
enum { F_AA, F_AD, F_AS, F_AR, F_MA, F_MD, F_MS, F_MR, F_MASTER, NUM_FADERS };
// Fader buttons 1..9
enum { B_FMODE, B_LFOTGT, B_LFOWAVE, B_MODTIMBRE, B_GLIDE, B_KEYTRACK, B_VELCUT, B_DELAY, B_PAGE };

enum { LFO_TIMBRE, LFO_COLOR, LFO_CUTOFF, LFO_PITCH, NUM_LFO_TARGETS };
const int kNumLfoWaves = 5;  // sine, triangle, saw, square, sample & hold

// Power-on values (0..127, as a knob or fader would send).
static const uint8_t kKnobInit[NUM_PAGES][8] = {
    {0, 64, 64, 100, 20, 80, 40, 0},  // shape, timbre, color, cutoff, reso, env amt, lfo rate, lfo amt
    {24, 70, 50, 40, 40, 64, 64, 80}, // glide, delay time, fb, mix, reverb amount, time, diffusion, lowpass
};
static const uint8_t kFaderInit[NUM_FADERS] = {10, 70, 90, 70, 0, 70, 0, 70, 100};
const int kInitShape = 1;  // saw swarm

// CC value -> unit interval
inline float unit(uint8_t v) { return (float)v * (1.0f / 127.0f); }
// Envelope time: 1 ms * 10^(4 v/127), so 1 ms .. 10 s, a decade per 32 steps.
inline float env_time(uint8_t v) { return 0.001f * fast_exp2(13.2877124f * unit(v)); }  // log2(10^4)

struct Launchkey {
    uint8_t knob[NUM_PAGES][8];
    bool picked[NUM_PAGES][8];
    uint8_t phys[8];     // last position of each physical knob
    bool phys_known[8];
    uint8_t fader[NUM_FADERS];
    int page;
    int shape;
    int fmode, lfo_target, lfo_wave;
    bool modtimbre, glide, keytrack, velcut, delay_on, cycle;
    float bend;          // -1..1
    float modwheel;      // 0..1
    float lfo_hz;        // set by knob 7 or by tap tempo, whichever was last
    uint32_t ticks, last_tap;
    Lfo lfo;

    void init() {
        for (int p = 0; p < NUM_PAGES; p++)
            for (int k = 0; k < 8; k++) {
                knob[p][k] = kKnobInit[p][k];
                picked[p][k] = (p == PAGE_SOUND);  // power-on page: knobs jump
            }
        for (int k = 0; k < 8; k++) { phys[k] = 0; phys_known[k] = false; }
        for (int f = 0; f < NUM_FADERS; f++) fader[f] = kFaderInit[f];
        page = PAGE_SOUND;
        pad_bank = 0;
        shape = kInitShape;
        fmode = 0; lfo_target = LFO_TIMBRE; lfo_wave = 0;
        modtimbre = false; glide = false; keytrack = true; velcut = false;
        delay_on = false; cycle = false;
        bend = 0.0f; modwheel = 0.0f;
        lfo_hz = lfo_rate(knob[PAGE_SOUND][K_LFORATE]);
        ticks = 0; last_tap = 0;
        lfo.init();
    }

    // Reset to the power-on sound, keeping the page. Knobs whose stored value
    // moved away from the physical position must be picked up again.
    void reset_sound() {
        const int p = page;
        const uint8_t ph[8] = {phys[0], phys[1], phys[2], phys[3], phys[4], phys[5], phys[6], phys[7]};
        bool known[8];
        for (int k = 0; k < 8; k++) known[k] = phys_known[k];
        const uint32_t t = ticks;
        init();
        for (int k = 0; k < 8; k++) { phys[k] = ph[k]; phys_known[k] = known[k]; }
        ticks = t;
        set_page(p);
    }

    // LFO rate: 0.05 Hz * 400^(v/127), so 0.05 .. 20 Hz.
    static float lfo_rate(uint8_t v) { return 0.05f * fast_exp2(8.64385619f * unit(v)); }  // log2(400)

    // Page switch: a knob is live on the new page only if the physical knob
    // already sits within 2 steps of the stored value; otherwise it is picked
    // up when it reaches or crosses that value (soft takeover).
    void set_page(int p) {
        page = p;
        for (int k = 0; k < 8; k++) {
            const int d = (int)phys[k] - (int)knob[p][k];
            picked[p][k] = phys_known[k] && d >= -2 && d <= 2;
        }
    }

    void on_knob(int k, uint8_t v) {
        if (!picked[page][k]) {
            const int now = (int)v - (int)knob[page][k];
            const int before = (int)phys[k] - (int)knob[page][k];
            const bool near = now >= -2 && now <= 2;
            const bool crossed = phys_known[k] && ((before <= 0 && now >= 0) || (before >= 0 && now <= 0));
            if (near || crossed) picked[page][k] = true;
        }
        phys[k] = v;
        phys_known[k] = true;
        if (!picked[page][k]) return;
        knob[page][k] = v;
        if (page == PAGE_SOUND && k == K_SHAPE) shape = (v * kNumShapes) >> 7;  // 0..33, 3-4 steps each
        if (page == PAGE_SOUND && k == K_LFORATE) lfo_hz = lfo_rate(v);
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
        case B_PAGE: set_page(page == PAGE_SOUND ? PAGE_FX : PAGE_SOUND); break;
        }
    }

    // Pads: top row = shapes 0..7, bottom row = 8..15; the round button right
    // of the pads (104 upper, 105 lower) selects bank 0 or 1 (+16).
    int pad_bank;
    static int pad_index(uint8_t note) {  // -1 if not a pad
        static const int8_t idx[16] = {8, 9, 10, 11, 0, 1, 2, 3, 12, 13, 14, 15, 4, 5, 6, 7};  // note 36..51
        return (note >= 36 && note <= 51) ? idx[note - 36] : -1;
    }

    // Raw MIDI from the Launchkey's MIDI port.
    void midi(uint8_t status, uint8_t d1, uint8_t d2) {
        const uint8_t type = status & 0xF0, ch = status & 0x0F;
        if (type == 0xB0 && ch == 0) {
            if (d1 >= 21 && d1 <= 28) on_knob(d1 - 21, d2);
            else if (d1 >= 71 && d1 <= 79) fader[d1 - 71] = d2;
            else if (d1 >= 11 && d1 <= 19 && d2 > 0) on_button(d1 - 11);
            else if (d1 == 1) modwheel = unit(d2);
        } else if (type == 0xE0 && ch == 0) {
            bend = (float)(((int)d2 << 7 | d1) - 8192) * (1.0f / 8192.0f);
        } else if (type == 0x90 && ch == 9 && d2 > 0) {
            const int i = pad_index(d1);
            if (i >= 0) shape = pad_bank * 16 + i;
        } else if (type == 0xB0 && ch == 15 && d2 > 0) {
            switch (d1) {
            case 51: set_page(PAGE_SOUND); break;   // arrow up
            case 52: set_page(PAGE_FX); break;      // arrow down
            case 102: shape = shape > 0 ? shape - 1 : kNumShapes - 1; break;  // track <
            case 103: shape = shape < kNumShapes - 1 ? shape + 1 : 0; break;  // track >
            case 104: pad_bank = 0; break;
            case 105: pad_bank = 1; break;
            case 76: on_tap(); break;               // Click: tap tempo
            case 77: reset_sound(); break;          // Undo: back to the power-on sound
            case 118: cycle = !cycle; break;        // Loop: cycling mod envelope
            }
        }
    }

    // Once per control tick: advance the LFO, then publish to the bus.
    void tick() {
        ticks++;
        Bus &b = bus();
        const uint8_t *s = knob[PAGE_SOUND], *x = knob[PAGE_FX];
        const float amt = clampf(unit(s[K_LFOAMT]) + modwheel, 0.0f, 1.0f);
        const float l = amt * lfo.tick(lfo_hz, lfo_wave);

        b.shape = shape;
        b.timbre = clampf(unit(s[K_TIMBRE]) + (lfo_target == LFO_TIMBRE ? 0.5f * l : 0.0f), 0.0f, 1.0f);
        b.color = clampf(unit(s[K_COLOR]) + (lfo_target == LFO_COLOR ? 0.5f * l : 0.0f), 0.0f, 1.0f);
        b.cutoff_oct = 10.0f * unit(s[K_CUTOFF]) + (lfo_target == LFO_CUTOFF ? 3.0f * l : 0.0f);
        b.q = 0.5f * fast_exp2(5.32192809f * unit(s[K_RESO]));  // 0.5 * 40^v: 0.5 .. 20
        b.env_amt_oct = 5.0f * clampf(((float)s[K_ENVAMT] - 64.0f) / 63.0f, -1.0f, 1.0f);
        b.fmode = fmode;
        b.keytrack = keytrack; b.velcut = velcut; b.modtimbre = modtimbre; b.cycle = cycle;
        b.amp_a = env_time(fader[F_AA]); b.amp_d = env_time(fader[F_AD]);
        b.amp_s = unit(fader[F_AS]);     b.amp_r = env_time(fader[F_AR]);
        b.mod_a = env_time(fader[F_MA]); b.mod_d = env_time(fader[F_MD]);
        b.mod_s = unit(fader[F_MS]);     b.mod_r = env_time(fader[F_MR]);
        b.pitch_mod = 2.0f * bend + (lfo_target == LFO_PITCH ? 2.0f * l : 0.0f);
        b.glide = glide;
        b.glide_t = unit(x[K_GLIDE]) * unit(x[K_GLIDE]);  // 0 .. 1 s, finer at the short end
        b.delay_on = delay_on;
        b.delay_t = 0.01f * fast_exp2(7.22881869f * unit(x[K_DTIME]));  // 10 ms .. 1.5 s (log2 150)
        b.delay_fb = 0.95f * unit(x[K_DFB]);
        b.delay_mix = unit(x[K_DMIX]);
        b.rev_amount = unit(x[K_RAMOUNT]); b.rev_time = unit(x[K_RTIME]);
        b.rev_diff = unit(x[K_RDIFF]);     b.rev_lp = unit(x[K_RLP]);
        const float m = unit(fader[F_MASTER]);
        b.master = m * m;
    }
};

}  // namespace freak

#endif
