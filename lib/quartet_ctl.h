// quartet_ctl.h: maps a Launchkey 49 MK3 (standalone mode) onto the four
// quartet voices (quartet_voice.h), after the Roland Jupiter-4. This file is
// the single source of truth for the quartet cheat sheet.
//
// Keys: POLY gives each new key the voice released longest ago, or steals
// the oldest note when all four sound; a key already sounding retriggers
// its voice. UNISON puts all four voices on the lowest held key (the
// Jupiter-4 manual's UNISON 1), spread by DETUNE; a new lowest key
// retriggers, releasing it glides back to the next lowest without one.
// HOLD (Play) keeps the keys played while it is on. No arpeggiator (Nuno:
// the Launchkey has its own) and no delay or reverb (an external pedal).
// Faders 1-8: ATTACK, DECAY, SUSTAIN, RELEASE, HPF, LPF CUTOFF, RESONANCE,
// ENV->VCF; fader 9: volume.
// Knobs, SOUND page (Device Select, or the upper button left of the pads):
//   PW (pulse width, or the PWM depth), DETUNE, VCO<LFO, VCF<LFO, VCA<LFO,
//   LFO RATE, LFO DELAY, PORTAMENTO time
// Knobs, MOD page (Device Lock, the lower button left of the pads, or the
//   upper round button right of the pads):
//   TRIG RATE, S&H DEPTH, -, -, BEND->VCO, BEND->VCF, BEND->VCA, VIBRATO
//   (the mod wheel's depth)
// Knobs and faders pick up (Launchkey::follow): after a preset or a page
// change, a control moves its value from where it is, without a jump.
// Fader buttons: 1 WAVE (saw, square, pulse, PWM, off), 2 SUB, 3 NOISE,
// (2 SUB cycles OFF, LOW, MID, HIGH: Nuno asked for four levels)
// 4 KYBD FOLLOW (10, 40, 70, 100 %), 5 ENV POLARITY, 6 VCA GATE, 7 LFO WAVE
// (sine, square, saw, inverted saw), 8 ENSEMBLE, 9 next preset.
// Track < >: VCO range 32', 16', 8', 4'. Play: HOLD. Stop: PORTAMENTO on/off.
// Click: tap the trigger rate. Undo: back to the power-on sound (preset 1).
// Top pads: presets 1-8. Bottom pads: 6 POLY, 7 UNISON, 8 S&H on/off (Nuno:
// a switch); 1-5 free.
//
// MIDI arrives in another thread: midi() only queues it (one producer),
// tick() handles it in the DSP thread (one consumer), as groove_ctl.h.
// Header-only, C++11, no allocation; every type is a POD.
#ifndef QUARTET_CTL_H
#define QUARTET_CTL_H

#include <stdint.h>

#include "launchkey_ctl.h"
#include "quartet_voice.h"

namespace freak {

const int kQVoices = 4;

// What quartet_voices reads, once per block.
struct QuartetBus {
    QuartetParams P;
    bool ensemble;
    uint8_t note[kQVoices];
    bool gate[kQVoices];
    uint32_t trig[kQVoices];   // a change retriggers the voice
    float detune[kQVoices];    // semitones
    uint32_t restart;          // counts fresh keys (all keys were up): the LFO delay starts over
};
static QuartetBus g_qbus;  // one per patch (the patch is one translation unit)
inline QuartetBus &qbus() { return g_qbus; }

// Knob and fader maps (v = 0..127), ranges from the Jupiter-4's spec where
// it gives one; the curves between the ends are my choice.
inline float q_unit(uint8_t v) { return (float)v * (1.0f / 127.0f); }
inline float q_sq(uint8_t v) { const float u = q_unit(v); return u * u; }
inline float q_attack(uint8_t v) { return 0.0006f * fast_exp2(12.2877124f * q_unit(v)); }  // 0.6 ms .. 3 s
inline float q_decay(uint8_t v) { return 0.014f * fast_exp2(9.48044f * q_unit(v)); }       // 14 ms .. 10 s (decay, release)
inline float q_hpf(uint8_t v) { return v == 0 ? 10.0f : 40.0f * fast_exp2(6.9657843f * q_unit(v)); }  // off; 40 Hz .. 5 kHz
inline float q_lpf(uint8_t v) { return 9.9657843f * q_unit(v); }      // lp_oct: 20 Hz .. 20 kHz at C4
inline float q_env(uint8_t v) { return 7.0f * q_unit(v); }            // octaves
inline float q_level(uint8_t v) { return 0.35f * q_sq(v); }           // four voices sum to about 1
inline float q_pw(uint8_t v) { static const float w[4] = {0.5f, 0.4f, 0.2f, 0.1f}; return w[(v * 4) >> 7]; }
inline float q_detune(uint8_t v) { return 0.2f * q_unit(v); }         // outer voices +-20 cents
inline float q_lfohz(uint8_t v) { return 0.1f * fast_exp2(9.6438562f * q_unit(v)); }  // 0.1 .. 80 Hz
inline float q_trighz(uint8_t v) { return fast_exp2(4.6438562f * q_unit(v)); }        // 1 .. 25 Hz
inline float q_shdepth(uint8_t v) { return 3.0f * q_unit(v); }        // octaves at S&H +-1

enum { QPAGE_SOUND, QPAGE_MOD, NUM_QPAGES };
enum { QK_PW, QK_DETUNE, QK_VCOLFO, QK_VCFLFO, QK_VCALFO, QK_LFORATE, QK_LFODELAY, QK_GLIDE };
enum { QM_TRIG, QM_SH, QM_FREE3, QM_FREE4, QM_BENDVCO, QM_BENDVCF, QM_BENDVCA, QM_VIB };
enum { QF_A, QF_D, QF_S, QF_R, QF_HPF, QF_LPF, QF_RES, QF_ENV, QF_VOL, NUM_QF };
enum { QMODE_POLY, QMODE_UNISON };
enum { QEV_PAGE, QEV_WAVE, QEV_SUB, QEV_NOISE, QEV_KYBD, QEV_ENVPOL, QEV_VCAGATE, QEV_LFOWAVE,
       QEV_RANGE, QEV_MODE, QEV_HOLD, QEV_PORTA, QEV_TAP, QEV_RESET, QEV_UNMAPPED, QEV_SH,
       QEV_ENSEMBLE, QEV_PRESET, QEV_PICKED };
const uint8_t kQPickedFader = 2;  // QEV_PICKED a: page 0/1 for knobs, 2 for faders

static const char *const kQWaveNames[NUM_QW] = {"SAW", "SQUARE", "PULSE", "PWM", "OFF"};
static const char *const kQLfoNames[NUM_QL] = {"SINE", "SQUARE", "SAW", "INVERTED SAW"};
static const char *const kQRangeNames[4] = {"32'", "16'", "8'", "4'"};
static const char *const kQKybdNames[4] = {"10%", "40%", "70%", "100%"};
static const char *const kQSubNames[4] = {"OFF", "LOW", "MID", "HIGH"};
static const float kQKybd[4] = {0.1f, 0.4f, 0.7f, 1.0f};

// Presets: switches, the two knob pages, faders 1-8 (not the volume).
// 1 is the test sound Nuno liked (2026-11-13); 2-8 are set from the
// parameter meanings, not tuned by ear. 2, sweep pad: a 1.5 s attack opens
// the cutoff 4 octaves from 250 Hz at RESONANCE 0.6, a 0.3 Hz LFO moves it
// half an octave.
enum { QP_SUB = 1 /* sub at MID */, QP_NOISE = 2, QP_ENVINV = 4, QP_VCAGATE = 8, QP_PORTA = 16, QP_ENSEMBLE = 32, QP_SH = 64 };
struct QuartetPreset {
    const char *name;
    uint8_t wave, kybd, lfo_wave, range, mode, flags;
    uint8_t knob[NUM_QPAGES][8];
    uint8_t fader[8];
};
// knob[SOUND]: pw, detune, vco<lfo, vcf<lfo, vca<lfo, lfo rate, lfo delay, portamento
// knob[MOD]:   trig rate, s&h depth, -, -, bend->vco, bend->vcf, bend->vca, vibrato
// fader:       attack, decay, sustain, release, hpf, lpf, resonance, env->vcf
static const QuartetPreset kQPresets[] = {
    {"power-on chord", QW_SAW, 1, QL_SINE, 2, QMODE_POLY, QP_PORTA,
     {{0, 40, 0, 0, 0, 70, 0, 30}, {60, 64, 0, 0, 21, 0, 0, 60}},
     {10, 70, 90, 60, 0, 80, 30, 50}},
    {"sweep pad", QW_SAW, 1, QL_SINE, 2, QMODE_POLY, QP_PORTA | QP_SUB | QP_ENSEMBLE,
     {{0, 40, 0, 45, 0, 21, 0, 30}, {60, 64, 0, 0, 21, 0, 0, 60}},
     {117, 96, 76, 90, 0, 46, 75, 73}},
    {"pwm strings", QW_PWM, 1, QL_SINE, 2, QMODE_POLY, QP_PORTA | QP_ENSEMBLE,
     {{90, 40, 0, 0, 0, 37, 0, 20}, {60, 64, 0, 0, 21, 0, 0, 60}},
     {103, 83, 114, 86, 20, 95, 15, 20}},
    {"unison lead", QW_SAW, 2, QL_SINE, 2, QMODE_UNISON, QP_PORTA,
     {{0, 60, 18, 0, 0, 76, 31, 40}, {60, 64, 0, 0, 21, 0, 0, 60}},
     {10, 75, 75, 60, 0, 70, 57, 45}},
    {"s&h burble", QW_SAW, 1, QL_SINE, 2, QMODE_POLY, QP_PORTA | QP_SUB | QP_SH,
     {{0, 40, 0, 0, 0, 70, 0, 30}, {82, 80, 0, 0, 21, 0, 0, 60}},
     {10, 70, 110, 50, 0, 70, 100, 30}},
    {"pluck bass", QW_SAW, 1, QL_SINE, 1, QMODE_POLY, QP_SUB,
     {{0, 40, 0, 0, 0, 70, 0, 30}, {60, 64, 0, 0, 21, 0, 0, 60}},
     {0, 56, 0, 51, 0, 45, 76, 90}},
    {"resonance keys", QW_OFF, 3, QL_SINE, 2, QMODE_POLY, QP_PORTA,
     {{0, 40, 0, 0, 0, 70, 0, 30}, {60, 64, 0, 0, 21, 0, 0, 60}},
     {5, 70, 127, 60, 0, 47, 127, 0}},
    {"gate stabs", QW_SAW, 1, QL_SINE, 2, QMODE_POLY, QP_VCAGATE | QP_ENVINV,
     {{0, 40, 0, 0, 0, 70, 0, 30}, {60, 64, 0, 0, 21, 0, 0, 60}},
     {18, 62, 0, 38, 0, 85, 64, 54}},
};
const int kNumQPresets = sizeof(kQPresets) / sizeof(kQPresets[0]);
const uint8_t kQVolumeInit = 100;

struct QEvent { uint8_t type, a, b, c; };
struct QMidi { uint8_t s, d1, d2, pad_; };
const int kQRing = 64;
const int kQEvents = 8;

#pragma GCC push_options
#pragma GCC optimize("Os")
struct QuartetCtl {
    QMidi ring[kQRing];
    volatile uint8_t rhead, rtail;
    uint8_t knob[NUM_QPAGES][8];
    bool picked[NUM_QPAGES][8];
    int16_t anchor_phys[NUM_QPAGES][8];
    uint8_t anchor_val[NUM_QPAGES][8];
    uint8_t phys[8];
    bool phys_known[8];
    uint8_t fader[NUM_QF];
    bool fpicked[NUM_QF];
    int16_t fanchor_phys[NUM_QF];
    uint8_t fanchor_val[NUM_QF];
    uint8_t fphys[NUM_QF];
    bool fphys_known[NUM_QF];
    int page, preset, wave, kybd, lfo_wave, range, mode;
    int sub;                    // sub level 0..3 (kQSubLevels)
    bool noise, env_inv, vca_gate, hold, porta, ensemble, sh_on;
    float bend, modwheel, tap_hz;
    uint8_t down[128], latched[128];
    int held;                   // keys down or latched
    uint8_t vnote[kQVoices];
    bool vgate[kQVoices];
    uint32_t vtrig[kQVoices], vstamp[kQVoices];  // stamp: last note-on or release
    int unison_note;            // -1: none
    uint32_t ticks, stamp, restart, last_tap;
    QEvent events[kQEvents];
    uint8_t ev_head, ev_count;
    uint32_t unmapped_seen[16][4];

    void push_event(uint8_t type, uint8_t a, uint8_t b = 0, uint8_t c = 0) {
        if (ev_count == kQEvents) return;
        QEvent &e = events[(ev_head + ev_count) % kQEvents];
        e.type = type; e.a = a; e.b = b; e.c = c;
        ev_count++;
    }
    bool pop_event(QEvent &e) {
        if (ev_count == 0) return false;
        e = events[ev_head];
        ev_head = (uint8_t)((ev_head + 1) % kQEvents);
        ev_count--;
        return true;
    }

    // ---- presets and pickup
    void load(const QuartetPreset &p) {
        for (int g = 0; g < NUM_QPAGES; g++)
            for (int k = 0; k < 8; k++) knob[g][k] = p.knob[g][k];
        for (int f = 0; f < 8; f++) fader[f] = p.fader[f];
        wave = p.wave; kybd = p.kybd; lfo_wave = p.lfo_wave; range = p.range;
        sub = (p.flags & QP_SUB) ? 2 : 0; noise = p.flags & QP_NOISE; env_inv = p.flags & QP_ENVINV;
        vca_gate = p.flags & QP_VCAGATE; porta = p.flags & QP_PORTA;
        ensemble = p.flags & QP_ENSEMBLE; sh_on = p.flags & QP_SH;
        tap_hz = 0.0f;
        set_mode(p.mode);
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
        load(kQPresets[i]);
        for (int g = 0; g < NUM_QPAGES; g++) reanchor_knobs(g);
        reanchor_faders();
        push_event(QEV_PRESET, (uint8_t)i);
    }

    void init() {
        rhead = rtail = 0;
        for (int k = 0; k < 8; k++) { phys[k] = 0; phys_known[k] = false; }
        for (int f = 0; f < NUM_QF; f++) {
            fphys[f] = 0;
            fphys_known[f] = false;
            fpicked[f] = true;  // power-on: faders jump
            fanchor_phys[f] = -1;
        }
        page = QPAGE_SOUND;
        mode = QMODE_POLY;
        hold = false;
        bend = modwheel = 0.0f;
        for (int n = 0; n < 128; n++) down[n] = latched[n] = 0;
        held = 0;
        for (int i = 0; i < kQVoices; i++) {
            vnote[i] = 60; vgate[i] = false; vtrig[i] = 0; vstamp[i] = 0;
        }
        unison_note = -1;
        ticks = stamp = restart = last_tap = 0;
        ev_head = ev_count = 0;
        for (int c = 0; c < 16; c++)
            for (int w = 0; w < 4; w++) unmapped_seen[c][w] = 0;
        preset = 0;
        load(kQPresets[0]);
        fader[QF_VOL] = kQVolumeInit;
        for (int g = 0; g < NUM_QPAGES; g++)
            for (int k = 0; k < 8; k++) {
                picked[g][k] = (g == QPAGE_SOUND);  // power-on page: knobs jump
                anchor_phys[g][k] = -1;
                anchor_val[g][k] = knob[g][k];
            }
        for (int f = 0; f < NUM_QF; f++) fanchor_val[f] = fader[f];
        publish();
    }

    // ---- MIDI in: queued here, handled in tick()
    void midi(uint8_t s, uint8_t d1, uint8_t d2) {
        const uint8_t next = (uint8_t)((rtail + 1) % kQRing);
        if (next == rhead) return;  // full: drop
        ring[rtail].s = s; ring[rtail].d1 = d1; ring[rtail].d2 = d2;
        __asm__ volatile("" ::: "memory");
        rtail = next;
    }

    // ---- keys
    bool is_held(int n) const { return down[n] || latched[n]; }
    int lowest_held() const {
        for (int n = 0; n < 128; n++) if (is_held(n)) return n;
        return -1;
    }
    void voice_on(int i, int n) {
        vnote[i] = (uint8_t)n; vgate[i] = true; vtrig[i]++; vstamp[i] = ++stamp;
    }
    void voice_off(int i) { if (vgate[i]) { vgate[i] = false; vstamp[i] = ++stamp; } }
    void unison_to(int n, bool retrig) {
        unison_note = n;
        for (int i = 0; i < kQVoices; i++) {
            if (n < 0) { voice_off(i); continue; }
            vnote[i] = (uint8_t)n;
            vgate[i] = true;
            if (retrig) { vtrig[i]++; vstamp[i] = ++stamp; }
        }
    }
    void key_on(int n) {
        if (held == 0) restart++;
        if (!is_held(n)) held++;
        down[n] = 1;
        if (hold) latched[n] = 1;
        if (mode == QMODE_UNISON) {
            if (lowest_held() == n) unison_to(n, true);
            return;
        }
        for (int i = 0; i < kQVoices; i++)
            if (vgate[i] && vnote[i] == n) { voice_on(i, n); return; }
        int best = -1;
        for (int i = 0; i < kQVoices; i++)  // the voice released longest ago
            if (!vgate[i] && (best < 0 || vstamp[i] < vstamp[best])) best = i;
        if (best < 0)
            for (int i = 0; i < kQVoices; i++)  // all sound: the oldest note
                if (best < 0 || vstamp[i] < vstamp[best]) best = i;
        voice_on(best, n);
    }
    void key_off(int n) {
        if (!down[n]) return;
        down[n] = 0;
        if (latched[n]) return;  // HOLD keeps it
        held--;
        release_unheld();
    }
    // release whatever no longer has a key (or a latch) behind it
    void release_unheld() {
        if (mode == QMODE_UNISON) {
            const int l = lowest_held();
            if (l != unison_note) unison_to(l, false);
            return;
        }
        for (int i = 0; i < kQVoices; i++)
            if (vgate[i] && !is_held(vnote[i])) voice_off(i);
    }
    void set_hold(bool on) {
        hold = on;
        push_event(QEV_HOLD, on);
        if (on) {
            for (int n = 0; n < 128; n++) latched[n] = down[n];
            return;
        }
        for (int n = 0; n < 128; n++) latched[n] = 0;
        held = 0;
        for (int n = 0; n < 128; n++) held += down[n];
        release_unheld();
    }
    void set_mode(int m) {
        if (m == mode) return;
        mode = m;
        push_event(QEV_MODE, (uint8_t)m);
        for (int i = 0; i < kQVoices; i++) voice_off(i);
        unison_note = -1;
        if (m == QMODE_UNISON) {
            unison_to(lowest_held(), true);
        } else {
            int i = 0;
            for (int n = 0; n < 128 && i < kQVoices; n++)
                if (is_held(n)) voice_on(i++, n);
        }
    }
    void all_off() {
        for (int n = 0; n < 128; n++) down[n] = latched[n] = 0;
        held = 0;
        release_unheld();
    }

    // ---- controls
    void set_page(int p, uint8_t src) {
        if (p != page) push_event(QEV_PAGE, (uint8_t)p, src);
        page = p;
        reanchor_knobs(p);
    }
    void on_knob(int k, uint8_t v) {
        const int p = page;
        if (Launchkey::follow(knob[p][k], picked[p][k], anchor_phys[p][k], anchor_val[p][k], v))
            push_event(QEV_PICKED, (uint8_t)p, (uint8_t)k, knob[p][k]);
        phys[k] = v;
        phys_known[k] = true;
        if (p == QPAGE_MOD && k == QM_TRIG) tap_hz = 0.0f;
    }
    void on_fader(int f, uint8_t v) {
        if (Launchkey::follow(fader[f], fpicked[f], fanchor_phys[f], fanchor_val[f], v))
            push_event(QEV_PICKED, kQPickedFader, (uint8_t)f, fader[f]);
        fphys[f] = v;
        fphys_known[f] = true;
    }
    void on_button(int b) {
        switch (b) {
        case 0: wave = (wave + 1) % NUM_QW; push_event(QEV_WAVE, (uint8_t)wave); break;
        case 1: sub = (sub + 1) % 4; push_event(QEV_SUB, (uint8_t)sub); break;
        case 2: noise = !noise; push_event(QEV_NOISE, noise); break;
        case 3: kybd = (kybd + 1) % 4; push_event(QEV_KYBD, (uint8_t)kybd); break;
        case 4: env_inv = !env_inv; push_event(QEV_ENVPOL, env_inv); break;
        case 5: vca_gate = !vca_gate; push_event(QEV_VCAGATE, vca_gate); break;
        case 6: lfo_wave = (lfo_wave + 1) % NUM_QL; push_event(QEV_LFOWAVE, (uint8_t)lfo_wave); break;
        case 7: ensemble = !ensemble; push_event(QEV_ENSEMBLE, ensemble); break;
        default: apply_preset((preset + 1) % kNumQPresets); break;  // 9: next preset
        }
    }
    void step_range(int d) {
        const int r = range + d;
        if (r < 0 || r > 3) return;
        range = r;
        push_event(QEV_RANGE, (uint8_t)range);
    }
    void on_tap() {
        // two taps 40 ms .. 1 s apart set the trigger rate (25 .. 1 Hz)
        const uint32_t dt = ticks - last_tap;
        if (last_tap != 0 && dt >= (uint32_t)(0.04f * kControlRate) && dt <= (uint32_t)kControlRate) {
            tap_hz = clampf(kControlRate / (float)dt, 1.0f, 25.0f);
            push_event(QEV_TAP, (uint8_t)(tap_hz + 0.5f));
        }
        last_tap = ticks == 0 ? 1 : ticks;
    }
    void unmapped(uint8_t ch, uint8_t num, uint8_t v) {
        uint32_t &w = unmapped_seen[ch][num >> 5];
        const uint32_t bit = 1u << (num & 31);
        if (w & bit) return;
        w |= bit;
        push_event(QEV_UNMAPPED, (uint8_t)(ch + 1), num, v);
    }

    void handle(uint8_t status, uint8_t d1, uint8_t d2) {
        const uint8_t type = status & 0xF0, ch = status & 0x0F;
        if (type == 0x90 && ch == 0) {
            if (d2 > 0) key_on(d1 & 127); else key_off(d1 & 127);
        } else if (type == 0x80 && ch == 0) {
            key_off(d1 & 127);
        } else if (type == 0xB0 && ch == 0) {
            if (d1 >= 21 && d1 <= 28) on_knob(d1 - 21, d2);
            else if (d1 >= 71 && d1 <= 79) on_fader(d1 - 71, d2);
            else if (d1 >= 11 && d1 <= 19) { if (d2 > 0) on_button(d1 - 11); }
            else if (d1 == 1) modwheel = q_unit(d2);
            else if (d1 == 123 || d1 == 120) all_off();
            else unmapped(ch, d1, d2);
        } else if (type == 0xE0 && ch == 0) {
            bend = clampf(((float)(((int)d2 << 7) | d1) - 8192.0f) / 8191.0f, -1.0f, 1.0f);
        } else if (type == 0x90 && ch == 9 && d2 > 0) {
            if (d1 >= 40 && d1 <= 43) apply_preset(d1 - 40);        // top pads 1-4
            else if (d1 >= 48 && d1 <= 51) apply_preset(d1 - 44);   // top pads 5-8
            else if (d1 == 45) set_mode(QMODE_POLY);                // bottom pad 6
            else if (d1 == 46) set_mode(QMODE_UNISON);              // bottom pad 7
            else if (d1 == 47) { sh_on = !sh_on; push_event(QEV_SH, sh_on); }  // bottom pad 8
        } else if (type == 0xB0 && ch == 15 && d2 > 0) {
            switch (d1) {
            case 51: case 106: set_page(QPAGE_SOUND, d1); break;
            case 52: case 107: case 104: set_page(QPAGE_MOD, d1); break;
            case 102: step_range(-1); break;   // track <
            case 103: step_range(+1); break;   // track >
            case 115: set_hold(!hold); break;  // Play
            case 116: porta = !porta; push_event(QEV_PORTA, porta); break;  // Stop
            case 76: on_tap(); break;          // Click
            case 77: apply_preset(0); push_event(QEV_RESET, 0); break;  // Undo
            case 105: case 74: case 75: case 117: case 118: break;  // free
            default: unmapped(ch, d1, d2);
            }
        }
    }

    // Once per block: MIDI in, then the bus.
    void tick() {
        ticks++;
        while (rhead != rtail) {
            const QMidi m = ring[rhead];
            __asm__ volatile("" ::: "memory");
            rhead = (uint8_t)((rhead + 1) % kQRing);
            handle(m.s, m.d1, m.d2);
        }
        publish();
    }

    void publish() {
        QuartetBus &b = qbus();
        QuartetParams &P = b.P;
        const uint8_t *s = knob[QPAGE_SOUND], *m = knob[QPAGE_MOD];
        P.wave = wave;
        P.pw = q_pw(s[QK_PW]);
        P.pwm = q_unit(s[QK_PW]);
        P.sub = kQSubLevels[sub & 3];
        P.noise = noise;
        P.range = range - 2;
        P.hp_hz = q_hpf(fader[QF_HPF]);
        P.lp_oct = q_lpf(fader[QF_LPF]);
        P.res = q_unit(fader[QF_RES]);
        P.env_vcf = q_env(fader[QF_ENV]);
        P.env_inv = env_inv;
        P.kybd = kQKybd[kybd];
        P.vco_lfo = 12.0f * q_sq(s[QK_VCOLFO]);
        P.vcf_lfo = 4.0f * q_sq(s[QK_VCFLFO]);
        P.vca_lfo = q_unit(s[QK_VCALFO]);
        P.sh_vcf = sh_on ? q_shdepth(m[QM_SH]) : 0.0f;
        P.a = q_attack(fader[QF_A]);
        P.d = q_decay(fader[QF_D]);
        P.s = q_unit(fader[QF_S]);
        P.r = q_decay(fader[QF_R]);
        P.vca_gate = vca_gate;
        P.glide = porta ? 2.0f * q_sq(s[QK_GLIDE]) : 0.0f;
        P.bend_st = bend * 12.0f * q_unit(m[QM_BENDVCO]);
        P.bend_oct = bend * 2.0f * q_unit(m[QM_BENDVCF]);
        P.bend_db = bend * 12.0f * q_unit(m[QM_BENDVCA]);
        P.vib = modwheel * q_unit(m[QM_VIB]);
        P.lfo_hz = q_lfohz(s[QK_LFORATE]);
        P.lfo_wave = lfo_wave;
        P.lfo_delay = 10.0f * q_sq(s[QK_LFODELAY]);
        P.trig_hz = tap_hz > 0.0f ? tap_hz : q_trighz(m[QM_TRIG]);
        P.level = q_level(fader[QF_VOL]);
        b.ensemble = ensemble;
        const float d = mode == QMODE_UNISON ? q_detune(s[QK_DETUNE]) : 0.0f;
        static const float spread[kQVoices] = {-1.0f, -1.0f / 3.0f, 1.0f / 3.0f, 1.0f};
        for (int i = 0; i < kQVoices; i++) {
            b.note[i] = vnote[i];
            b.gate[i] = vgate[i];
            b.trig[i] = vtrig[i];
            b.detune[i] = d * spread[i];
        }
        b.restart = restart;
    }
};
#pragma GCC pop_options

}  // namespace freak

#endif
