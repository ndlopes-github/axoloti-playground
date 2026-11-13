// raw_ctl.h: maps a Launchkey 49 MK3 (standalone mode) onto the raw
// monosynth (raw_voice.h). This file is the single source of truth for
// docs/raw_cheatsheet.tex.
//
// Keys: mono, last-note priority; a key released returns to the one still
// held before it (without a retrigger). LEGATO (fader button 6): keys
// played over a held key do not retrigger the envelopes either. HOLD (Play)
// keeps the last note sounding after the keys are up.
// Knobs, three pages:
//   SOUND (Device Select, or the upper button left of the pads):
//     VCO2 PITCH, PW, VCO1 LEVEL, VCO2 LEVEL, HPF CUTOFF, HPF PEAK, LPF CUTOFF, LPF PEAK
//   MOD (upper round button right of the pads):
//     MG RATE, MG SHAPE, PITCH<MG, PITCH<EG1, HPF<MG, HPF<EG2, LPF<MG, LPF<EG2
//   FX (Device Lock, or the lower button left of the pads):
//     GLIDE, DRIVE, NOISE, DELAY TIME, DELAY FB, DELAY MIX, REVERB, REVERB TIME
// Faders 1-8: EG1 DELAY, ATTACK, RELEASE; EG2 HOLD, ATTACK, DECAY, SUSTAIN,
// RELEASE; fader 9: volume.
// Fader buttons: 1 VCO1 scale, 2 VCO2 wave, 3 VCO2 scale, 4 MG S&H,
// 5 KEY TRACK, 6 LEGATO, 7 VELOCITY, 8 DELAY, 9 next preset.
// Track < >: VCO1 wave. Capture MIDI: dice; Click: tap tempo (MG); Undo:
// reset; Play: HOLD. Mod wheel: vibrato (MG on pitch, up to 1 semitone);
// pitch bend +-2 semitones.
// Pads: top = presets 0-7; bottom = SCREAM, OPEN, DIVE, VIBRATO, OCTAVE,
// S&H, THROW, DRIVE (while held, by pressure).
//
// Results go on two buses: the RawBus (the voice) and freak's Bus (delay,
// reverb, volume: fx_delay_st and fx_reverb work unchanged).
// Header-only, C++11, no allocation; every type is a POD.
#ifndef RAW_CTL_H
#define RAW_CTL_H

#include <stdint.h>

#include "launchkey_ctl.h"
#include "raw_voice.h"

namespace freak {

enum { RPAGE_SOUND, RPAGE_MOD, RPAGE_FX, NUM_RPAGES };
enum { RK_PITCH2, RK_PW, RK_LVL1, RK_LVL2, RK_HPCUT, RK_HPPEAK, RK_LPCUT, RK_LPPEAK };
enum { RM_MGRATE, RM_MGSHAPE, RM_PMG, RM_PEG1, RM_HPMG, RM_HPEG, RM_LPMG, RM_LPEG };
enum { RX_GLIDE, RX_DRIVE, RX_NOISE, RX_DTIME, RX_DFB, RX_DMIX, RX_RAMOUNT, RX_RTIME };
enum { RF_E1D, RF_E1A, RF_E1R, RF_E2H, RF_E2A, RF_E2D, RF_E2S, RF_E2R };  // fader 9: F_MASTER
enum { RB_SCALE1, RB_WAVE2, RB_SCALE2, RB_SH, RB_KEYTRK, RB_LEGATO, RB_VELO, RB_DELAY, RB_PRESET };
enum { RPERF_SCREAM, RPERF_OPEN, RPERF_DIVE, RPERF_VIBRATO, RPERF_OCTAVE, RPERF_SH, RPERF_THROW, RPERF_DRIVE, NUM_RPERF };
// Log events beyond freak's EV_*
enum { EV_WAVE1 = 30, EV_WAVE2, EV_SCALE1, EV_SCALE2, EV_RTOGGLE, EV_HOLD };
enum { RT_SH, RT_KEYTRK, RT_LEGATO, RT_VELO, RT_DELAY };  // EV_RTOGGLE a

static const char *const kWave1Names[NUM_W1] = {"TRIANGLE", "SAW", "PULSE", "NOISE"};
static const char *const kWave2Names[NUM_W2] = {"SAW", "SQUARE", "NARROW PULSE", "RING"};
static const char *const kScale1Names[4] = {"32'", "16'", "8'", "4'"};  // oct1 + 2
static const char *const kScale2Names[4] = {"16'", "8'", "4'", "2'"};   // oct2 + 1
static const char *const kRToggleNames[5] = {"MG S&H", "KEY TRACK", "LEGATO", "VELOCITY", "DELAY"};

// Presets: waves, scales, toggles, the three knob pages, the 8 envelope
// faders. Chosen from the parameter meanings, not tuned by ear.
enum { RP_SH = 1, RP_KEYTRK = 2, RP_LEGATO = 4, RP_VELO = 8, RP_DELAY = 16 };
struct RawPreset {
    const char *name;
    uint8_t wave1, wave2;
    int8_t oct1, oct2;
    uint8_t flags;
    uint8_t knob[NUM_RPAGES][8];
    uint8_t fader[8];
};
// knob[SOUND]: vco2 pitch, pw, vco1 level, vco2 level, hpf cut, hpf peak, lpf cut, lpf peak
// knob[MOD]:   mg rate, mg shape, pitch<mg, pitch<eg1, hpf<mg, hpf<eg2, lpf<mg, lpf<eg2
// knob[FX]:    glide, drive, noise, delay time, fb, mix, reverb, reverb time
// fader:       eg1 delay, attack, release; eg2 hold, attack, decay, sustain, release
static const RawPreset kRawPresets[] = {
    {"init", W1_SAW, W2_SAW, 0, 0, 0,
     {{64, 0, 110, 0, 0, 0, 70, 70}, {64, 64, 0, 64, 0, 0, 0, 60}, {0, 20, 0, 70, 40, 0, 30, 64}},
     {0, 30, 50, 0, 5, 70, 60, 60}},
    {"scream lead", W1_PULSE, W2_SAW, 0, 0, RP_LEGATO | RP_DELAY,
     {{68, 40, 100, 80, 40, 90, 62, 112}, {70, 64, 0, 64, 0, 0, 0, 55}, {40, 80, 0, 75, 50, 35, 40, 70}},
     {0, 30, 50, 0, 20, 80, 80, 70}},
    {"filter sine", W1_TRI, W2_SAW, 0, 0, RP_KEYTRK | RP_LEGATO,
     {{64, 0, 12, 0, 0, 0, 47, 120}, {75, 64, 0, 64, 0, 0, 0, 0}, {30, 10, 10, 70, 45, 30, 50, 80}},
     {0, 30, 50, 0, 40, 70, 110, 80}},
    {"zap bass", W1_SAW, W2_SQUARE, -1, -1, RP_VELO,
     {{64, 0, 110, 90, 10, 0, 45, 85}, {64, 64, 0, 110, 0, 0, 0, 90}, {0, 60, 0, 70, 30, 0, 15, 50}},
     {0, 50, 45, 0, 0, 50, 20, 40}},
    {"ring bells", W1_TRI, W2_RING, 0, 0, RP_VELO | RP_DELAY,
     {{112, 0, 20, 110, 30, 30, 100, 40}, {64, 64, 0, 64, 0, 0, 0, 30}, {0, 15, 0, 80, 50, 35, 70, 90}},
     {0, 0, 60, 0, 0, 90, 0, 90}},
    {"s&h robot", W1_PULSE, W2_SAW, 0, 0, RP_SH,
     {{64, 30, 110, 60, 20, 40, 60, 105}, {85, 64, 0, 64, 0, 0, 90, 20}, {0, 50, 0, 70, 40, 0, 30, 60}},
     {0, 0, 50, 0, 5, 70, 110, 50}},
    {"wind noise", W1_NOISE, W2_SAW, 0, 0, RP_DELAY,
     {{64, 0, 0, 0, 50, 100, 80, 100}, {30, 64, 0, 64, 60, 0, 70, 0}, {0, 20, 120, 90, 60, 40, 80, 100}},
     {0, 0, 80, 0, 90, 90, 127, 100}},
    {"detuned pair", W1_PULSE, W2_SAW, 0, 1, 0,
     {{70, 20, 100, 100, 30, 60, 75, 95}, {64, 64, 0, 64, 0, 0, 0, 70}, {20, 40, 0, 70, 40, 0, 35, 64}},
     {0, 30, 50, 0, 10, 75, 70, 65}},
};
const int kNumRawPresets = sizeof(kRawPresets) / sizeof(kRawPresets[0]);

// Knob value maps (v = 0..127).
inline float raw_bipolar(uint8_t v) { return clampf(((float)v - 64.0f) / 63.0f, -1.0f, 1.0f); }
inline float raw_pitch2(uint8_t v) { const float u = raw_bipolar(v); return 12.0f * u * absf(u); }      // +-12 st, fine near 0
inline float raw_pw(uint8_t v) { return 0.5f - 0.45f * unit(v); }                                        // 0.5 .. 0.05
inline float raw_cut(uint8_t v) { return 10.0f * unit(v); }                                              // 20 Hz .. 20 kHz
inline float raw_peak(uint8_t v) { return kPeakMax * unit(v); }                                          // k 0 .. 2.2, self-oscillates from 116
inline float raw_mg_hz(uint8_t v) { return 0.1f * fast_exp2(9.0f * unit(v)); }                           // 0.1 .. 51 Hz
inline float raw_pmg(uint8_t v) { return 12.0f * unit(v) * unit(v); }                                    // 0 .. 12 st
inline float raw_peg1(uint8_t v) { const float u = raw_bipolar(v); return 24.0f * u * absf(u); }        // +-24 st
inline float raw_fmg(uint8_t v) { return 5.0f * unit(v) * unit(v); }                                     // 0 .. 5 octaves
inline float raw_glide(uint8_t v) { return 2.0f * unit(v) * unit(v); }                                   // 0 .. 2 s
inline float raw_drive(uint8_t v) { return 0.5f * fast_exp2(4.0f * unit(v)); }                           // 0.5 .. 8
inline float raw_dh(uint8_t v) { return 2.0f * unit(v) * unit(v); }                                      // EG1 delay, EG2 hold: 0 .. 2 s

const int kKeyStack = 16;

#pragma GCC push_options
#pragma GCC optimize("Os")
struct RawCtl {
    uint8_t knob[NUM_RPAGES][8];
    bool picked[NUM_RPAGES][8];
    int16_t anchor_phys[NUM_RPAGES][8];
    uint8_t anchor_val[NUM_RPAGES][8];
    uint8_t phys[8];
    bool phys_known[8];
    uint8_t fader[NUM_FADERS];
    bool fpicked[NUM_FADERS];
    int16_t fanchor_phys[NUM_FADERS];
    uint8_t fanchor_val[NUM_FADERS];
    uint8_t fphys[NUM_FADERS];
    bool fphys_known[NUM_FADERS];
    int page, preset;
    int wave1, wave2, oct1, oct2;
    bool sh, keytrk, legato, velo, delay_on, hold;
    uint8_t keys[kKeyStack];  // held keys, oldest first
    int nkeys;
    uint8_t note;             // the key that sounds
    float vel;
    bool gate;
    uint32_t trig;
    float bend, modwheel, mg_hz;
    float perf[NUM_RPERF];
    float vib_phase;
    uint32_t ticks, last_tap, rng;
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

    void load(const RawPreset &p) {
        for (int g = 0; g < NUM_RPAGES; g++)
            for (int k = 0; k < 8; k++) knob[g][k] = p.knob[g][k];
        for (int f = 0; f < 8; f++) fader[f] = p.fader[f];
        wave1 = p.wave1; wave2 = p.wave2; oct1 = p.oct1; oct2 = p.oct2;
        sh = p.flags & RP_SH; keytrk = p.flags & RP_KEYTRK; legato = p.flags & RP_LEGATO;
        velo = p.flags & RP_VELO; delay_on = p.flags & RP_DELAY;
        mg_hz = raw_mg_hz(knob[RPAGE_MOD][RM_MGRATE]);
    }

    void init() {
        for (int g = 0; g < NUM_RPAGES; g++)
            for (int k = 0; k < 8; k++) {
                picked[g][k] = (g == RPAGE_SOUND);  // power-on page: knobs jump
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
        load(kRawPresets[0]);
        for (int g = 0; g < NUM_RPAGES; g++)
            for (int k = 0; k < 8; k++) anchor_val[g][k] = knob[g][k];
        for (int f = 0; f < NUM_FADERS; f++) fanchor_val[f] = fader[f];
        page = RPAGE_SOUND;
        hold = false;
        nkeys = 0;
        note = 48;
        vel = 0.8f;
        gate = false;
        trig = 0;
        bend = 0.0f; modwheel = 0.0f;
        for (int i = 0; i < NUM_RPERF; i++) perf[i] = 0.0f;
        vib_phase = 0.0f;
        ticks = 0; last_tap = 0;
        rng = 0x7F4A7C15u;
        ev_head = ev_count = 0;
        for (int c = 0; c < 16; c++)
            for (int w = 0; w < 4; w++) unmapped_seen[c][w] = 0;
        publish();
    }

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
        load(kRawPresets[i]);
        reanchor_knobs(page);
        reanchor_faders();
        push_event(EV_PRESET, (uint8_t)i, 0, 0);
    }
    void reset_sound() {
        apply_preset(0);
        push_event(EV_RESET, 0, 0, 0);
    }
    void next_preset() { apply_preset((preset + 1) % kNumRawPresets); }

    uint32_t rnd() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    int rnd_in(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

    // Dice: waves, scales and the SOUND and MOD pages within playable ranges.
    // Envelopes, FX and volume stay; Undo gets the power-on sound back.
    void dice() {
        rng ^= ticks * 2654435761u;
        if (rng == 0) rng = 1;
        wave1 = rnd_in(0, NUM_W1 - 2);  // no noise: it would leave the keys unpitched
        wave2 = rnd_in(0, NUM_W2 - 1);
        oct1 = rnd_in(-1, 0);
        oct2 = rnd_in(-1, 1);
        uint8_t *s = knob[RPAGE_SOUND], *m = knob[RPAGE_MOD];
        s[RK_PITCH2] = (uint8_t)rnd_in(40, 90);
        s[RK_PW] = (uint8_t)rnd_in(0, 100);
        s[RK_LVL1] = (uint8_t)rnd_in(60, 127);
        s[RK_LVL2] = (uint8_t)rnd_in(0, 127);
        s[RK_HPCUT] = (uint8_t)rnd_in(0, 60);
        s[RK_HPPEAK] = (uint8_t)rnd_in(0, 110);
        s[RK_LPCUT] = (uint8_t)rnd_in(35, 100);
        s[RK_LPPEAK] = (uint8_t)rnd_in(20, 115);
        m[RM_MGRATE] = (uint8_t)rnd_in(20, 100);
        m[RM_MGSHAPE] = (uint8_t)rnd_in(0, 127);
        m[RM_PMG] = (uint8_t)(rnd_in(0, 3) == 0 ? rnd_in(0, 60) : 0);
        m[RM_PEG1] = (uint8_t)(rnd_in(0, 2) == 0 ? rnd_in(20, 108) : 64);
        m[RM_HPMG] = (uint8_t)rnd_in(0, 60);
        m[RM_HPEG] = (uint8_t)rnd_in(0, 60);
        m[RM_LPMG] = (uint8_t)rnd_in(0, 80);
        m[RM_LPEG] = (uint8_t)rnd_in(20, 100);
        sh = rnd_in(0, 4) == 0;
        mg_hz = raw_mg_hz(m[RM_MGRATE]);
        if (page != RPAGE_FX) reanchor_knobs(page);
        push_event(EV_DICE, (uint8_t)wave1, (uint8_t)wave2, 0);
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
        if (p == RPAGE_MOD && k == RM_MGRATE) mg_hz = raw_mg_hz(knob[p][k]);
    }

    void on_fader(int f, uint8_t v) {
        if (Launchkey::follow(fader[f], fpicked[f], fanchor_phys[f], fanchor_val[f], v))
            push_event(EV_PICKED, kPickedFader, (uint8_t)f, fader[f]);
        fphys[f] = v;
        fphys_known[f] = true;
    }

    // Tap tempo (Click): two taps 0.1..10 s apart set the MG period.
    void on_tap() {
        const uint32_t dt = ticks - last_tap;
        if (last_tap != 0 && dt >= (uint32_t)(0.1f * kControlRate) && dt <= (uint32_t)(10.0f * kControlRate))
            mg_hz = kControlRate / (float)dt;
        last_tap = ticks == 0 ? 1 : ticks;
    }

    void toggle(bool &b, int which) {
        b = !b;
        push_event(EV_RTOGGLE, (uint8_t)which, b, 0);
    }

    void on_button(int b) {  // fader buttons, on press
        switch (b) {
        case RB_SCALE1: oct1 = oct1 >= 1 ? -2 : oct1 + 1; push_event(EV_SCALE1, (uint8_t)(oct1 + 2), 0, 0); break;
        case RB_WAVE2: wave2 = (wave2 + 1) % NUM_W2; push_event(EV_WAVE2, (uint8_t)wave2, 0, 0); break;
        case RB_SCALE2: oct2 = oct2 >= 2 ? -1 : oct2 + 1; push_event(EV_SCALE2, (uint8_t)(oct2 + 1), 0, 0); break;
        case RB_SH: toggle(sh, RT_SH); break;
        case RB_KEYTRK: toggle(keytrk, RT_KEYTRK); break;
        case RB_LEGATO: toggle(legato, RT_LEGATO); break;
        case RB_VELO: toggle(velo, RT_VELO); break;
        case RB_DELAY: toggle(delay_on, RT_DELAY); break;
        case RB_PRESET: next_preset(); break;
        }
    }

    void step_wave1(int d) {
        wave1 = (wave1 + d + NUM_W1) % NUM_W1;
        push_event(EV_WAVE1, (uint8_t)wave1, 0, 0);
    }

    // Mono keys, last-note priority. A new key retriggers the envelopes
    // unless LEGATO is on and another key is held. Releasing the sounding
    // key falls back to the newest key still held, without a retrigger.
    // With HOLD on the gate stays on after the last key is up; a key pressed
    // then (none held) is a new phrase and retriggers.
    void key_on(uint8_t n, uint8_t v) {
        remove_key(n);
        const bool overlap = nkeys > 0;
        if (nkeys == kKeyStack) remove_at(0);
        keys[nkeys++] = n;
        note = n;
        vel = unit(v);
        if (!(legato && overlap)) trig++;
        gate = true;
    }
    void key_off(uint8_t n) {
        remove_key(n);
        if (nkeys > 0) {
            note = keys[nkeys - 1];
        } else if (!hold) {
            gate = false;
        }
    }
    void remove_at(int i) {
        for (int j = i; j + 1 < nkeys; j++) keys[j] = keys[j + 1];
        nkeys--;
    }
    void remove_key(uint8_t n) {
        for (int i = 0; i < nkeys; i++)
            if (keys[i] == n) { remove_at(i); return; }
    }
    void set_hold(bool h) {
        hold = h;
        if (!hold && nkeys == 0) gate = false;
        push_event(EV_HOLD, hold, 0, 0);
    }

    static int pad_index(uint8_t n) { return Launchkey::pad_index(n); }
    void on_pad(uint8_t n, uint8_t v, bool pressure) {
        const int i = pad_index(n);
        if (i < 0) return;
        if (i < 8) {
            if (!pressure && v > 0) apply_preset(i);
            return;
        }
        perf[i - 8] = unit(v);  // strike sets the amount, pressure then follows
    }

    void midi(uint8_t status, uint8_t d1, uint8_t d2) {
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
            else if (d1 == 123 || d1 == 120) { nkeys = 0; if (!hold) gate = false; }  // all notes / sound off
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
            case 51: case 106: set_page(RPAGE_SOUND, d1); break;  // Device Select
            case 104: set_page(RPAGE_MOD, d1); break;             // round button, upper
            case 52: case 107: set_page(RPAGE_FX, d1); break;     // Device Lock
            case 102: step_wave1(-1); break;                      // track <
            case 103: step_wave1(+1); break;                      // track >
            case 74: dice(); break;                               // Capture MIDI
            case 76: on_tap(); break;                             // Click
            case 77: reset_sound(); break;                        // Undo
            case 115: set_hold(!hold); break;                     // Play
            case 75: case 105: case 116: case 117: case 118: break;  // free
            default: unmapped(ch, d1, d2);
            }
        }
    }

    // Every control tick: the vibrato LFO; publish every other tick (1.5 kHz
    // is plenty for knobs and keys; the voice interpolates).
    void tick() {
        ticks++;
        vib_phase += 6.0f / kControlRate;
        if (vib_phase >= 1.0f) vib_phase -= 1.0f;
        if ((ticks & 1) == 0) publish();
    }

    void publish() {
        RawBus &r = rawbus();
        Bus &b = bus();
        const uint8_t *s = knob[RPAGE_SOUND], *m = knob[RPAGE_MOD], *x = knob[RPAGE_FX];
        const float *pf = perf;
        r.note = (float)note;
        r.gate = gate;
        r.trig = trig;
        r.vel = vel;
        r.glide = raw_glide(x[RX_GLIDE]);
        const float vib = 0.7f * pf[RPERF_VIBRATO] * fast_sin(2.0f * kPi * vib_phase);
        r.bend = 2.0f * bend - 12.0f * pf[RPERF_DIVE] + (pf[RPERF_OCTAVE] > 0.0f ? 12.0f : 0.0f) + vib;
        r.wave1 = wave1; r.wave2 = wave2; r.oct1 = oct1; r.oct2 = oct2;
        r.pw = raw_pw(s[RK_PW]);
        r.pitch2 = raw_pitch2(s[RK_PITCH2]);
        r.lvl1 = unit(s[RK_LVL1]);
        r.lvl2 = unit(s[RK_LVL2]);
        r.noise = unit(x[RX_NOISE]);
        r.drive = raw_drive(x[RX_DRIVE]) * (1.0f + 3.0f * pf[RPERF_DRIVE]);
        const float scream = pf[RPERF_SCREAM];
        const float khp = raw_peak(s[RK_HPPEAK]), klp = raw_peak(s[RK_LPPEAK]);
        r.hp_k = khp + scream * (kPeakMax - khp);
        r.lp_k = klp + scream * (kPeakMax - klp);
        r.hp_oct = raw_cut(s[RK_HPCUT]);
        r.lp_oct = raw_cut(s[RK_LPCUT]) + 4.0f * pf[RPERF_OPEN];
        r.hp_mg = raw_fmg(m[RM_HPMG]);
        r.lp_mg = raw_fmg(m[RM_LPMG]);
        r.hp_eg = 6.0f * unit(m[RM_HPEG]);
        r.lp_eg = 8.0f * unit(m[RM_LPEG]);
        r.keytrack = keytrk ? 1.0f : 0.0f;
        r.pitch_mg = raw_pmg(m[RM_PMG]) + modwheel;  // mod wheel: up to 1 semitone more
        r.pitch_eg1 = raw_peg1(m[RM_PEG1]);
        r.mg_hz = mg_hz;
        r.mg_shape = raw_bipolar(m[RM_MGSHAPE]);
        r.mg_sh = sh;
        r.sh_lp = 3.0f * pf[RPERF_SH];
        r.e1_d = raw_dh(fader[RF_E1D]);
        r.e1_a = env_time(fader[RF_E1A]);
        r.e1_r = env_time(fader[RF_E1R]);
        r.e2_h = raw_dh(fader[RF_E2H]);
        r.e2_a = env_time(fader[RF_E2A]);
        r.e2_d = env_time(fader[RF_E2D]);
        r.e2_s = unit(fader[RF_E2S]);
        r.e2_r = env_time(fader[RF_E2R]);
        r.velamp = velo ? 0.8f : 0.0f;
        r.level = 0.5f;
        // freak's Bus: delay, reverb, volume (fx_delay_st, fx_reverb)
        const float th = pf[RPERF_THROW];
        b.delay_on = delay_on || th > 0.0f;
        b.delay_t = 0.01f * fast_exp2(7.22881869f * unit(x[RX_DTIME]));  // 10 ms .. 1.5 s (log2 150)
        b.delay_fb = clampf(0.95f * unit(x[RX_DFB]) + 0.4f * th, 0.0f, 0.95f);
        b.delay_mix = th > 0.0f ? clampf(unit(x[RX_DMIX]) + 0.6f * th, 0.0f, 1.0f) : unit(x[RX_DMIX]);
        b.rev_amount = unit(x[RX_RAMOUNT]);
        b.rev_time = unit(x[RX_RTIME]);
        b.rev_diff = 0.7f;
        b.rev_lp = 0.6f;
        const float mv = unit(fader[F_MASTER]);
        b.master = mv * mv;
    }
};
#pragma GCC pop_options

}  // namespace freak

#endif
