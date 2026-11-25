// west_ctl.h: maps a Launchkey 49 MK3 (standalone mode) onto the four
// westcoast voices (west_voice.h). This file is the single source of truth
// for the westcoast cheat sheet.
//
// Keys: four voices, velocity strikes the gate; a new key takes the voice
// released longest ago, or the oldest note when all four sound. HOLD (Play)
// keeps the keys played while it is on.
// Faders 1-8 (the timbre is played on the faders, as on a west-coast panel):
//   MOD RATIO, FM INDEX, MOD SHAPE, TIMBRE, SYMMETRY, RISE, FALL, LPG DECAY;
//   fader 9: volume.
// Knobs (one page): FUNC>TIMBRE, FUNC>FM, UNCERTAINTY, LPG BIAS, DRIFT RATE,
//   DRIFT>TIMBRE, GLIDE, BEND RANGE.
// Faders and knobs pick up (Launchkey::follow) after a preset.
// Fader buttons (they toggle on the Launchkey: a button acts on the press
// that lights it): 1 MO TRACK (follows the keys / free: the RATIO fader is
// then a rate), 2 RATIO SNAP (to simple ratios), 3 FUNC MODE (transient,
// sustain, cycle), 4 FUNC RESPONSE (log, lin, exp), 5 LPG MODE (LP, COMBO,
// VCA), 6 PING (the gate strikes on each key / follows the function),
// 7-8 free, 9 next preset.
// Track < >: range 32' .. 4'. Capture MIDI: DICE (the oscillator and the
// folder at random, within playable ranges). Play: HOLD. Undo: preset 1.
// Top pads: presets 1-8. Bottom pads (while held): 1 FOLD (pressure adds
// timbre), 2 FM (pressure adds index), 3 STRIKE (pings the sounding voices
// again, at the pad's velocity), 4 RANDOM (new stored random values for the
// sounding voices); 5-8 free.
//
// MIDI arrives in another thread: midi() only queues it (one producer),
// tick() handles it in the DSP thread (one consumer), as quartet_ctl.h.
// Header-only, C++11, no allocation; every type is a POD.
#ifndef WEST_CTL_H
#define WEST_CTL_H

#include <stdint.h>

#include "launchkey_ctl.h"
#include "west_voice.h"

namespace freak {

// What west_voices reads, once per block.
struct WestBus {
    WestParams P;
    float drift_hz;
    uint8_t note[kWVoices];
    bool gate[kWVoices];
    uint32_t trig[kWVoices];   // a change retriggers the voice (function, strike, random)
    float vel[kWVoices];
    uint32_t restrike;         // STRIKE pad: a change re-pings the sounding voices
    float strike_vel;
    uint32_t reroll;           // RANDOM pad: a change redraws their stored random
};
static WestBus g_wbus;  // one per patch
inline WestBus &wbus() { return g_wbus; }

// Maps (v = 0..127); the curves are my choice.
inline float w_unit(uint8_t v) { return (float)v * (1.0f / 127.0f); }
inline float w_sq(uint8_t v) { const float u = w_unit(v); return u * u; }
inline float w_ratio(uint8_t v) { return 0.25f * fast_exp2(5.0f * w_unit(v)); }        // 0.25 .. 8
inline float w_mohz(uint8_t v) { return 0.05f * fast_exp2(14.0f * w_unit(v)); }        // 0.05 .. 820 Hz
inline float w_fm(uint8_t v) { return 8.0f * w_sq(v); }                                 // index 0 .. 8
inline float w_time(uint8_t v) { return 0.0005f * fast_exp2(14.2877f * w_unit(v)); }   // 0.5 ms .. 10 s
inline float w_decay(uint8_t v) { return 0.02f * fast_exp2(7.2288f * w_unit(v)); }     // 20 ms .. 3 s
inline float w_drifthz(uint8_t v) { return 0.05f * fast_exp2(8.6439f * w_unit(v)); }   // 0.05 .. 20 Hz
inline float w_level(uint8_t v) { return 0.3f * w_sq(v); }

// RATIO SNAP: the nearest simple ratio (in log distance).
static const float kWSnap[] = {0.25f, 1.0f / 3, 0.5f, 2.0f / 3, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
const int kNumWSnap = sizeof(kWSnap) / sizeof(kWSnap[0]);
inline float w_snap(float r) {
    float best = kWSnap[0], bd = 1e9f;
    for (int i = 0; i < kNumWSnap; i++) {
        const float q = r > kWSnap[i] ? r / kWSnap[i] : kWSnap[i] / r;  // >= 1
        if (q < bd) { bd = q; best = kWSnap[i]; }
    }
    return best;
}

enum { WFD_RATIO, WFD_FM, WFD_SHAPE, WFD_TIMBRE, WFD_SYM, WFD_RISE, WFD_FALL, WFD_DECAY, WFD_VOL, NUM_WFD };
enum { WK_FTIMBRE, WK_FFM, WK_RND, WK_BIAS, WK_DRIFTHZ, WK_DRIFT, WK_GLIDE, WK_BEND };
enum { WEV_TRACK, WEV_SNAP, WEV_FMODE, WEV_FRESP, WEV_LPG, WEV_PING, WEV_RANGE, WEV_HOLD, WEV_DICE,
       WEV_RESET, WEV_PRESET, WEV_PICKED, WEV_UNMAPPED };
const uint8_t kWPickedFader = 2;

static const char *const kWFModeNames[NUM_WF] = {"TRANSIENT", "SUSTAIN", "CYCLE"};
static const char *const kWFRespNames[NUM_WR] = {"LOG", "LIN", "EXP"};
static const char *const kWLpgNames[NUM_WL] = {"LP", "COMBO", "VCA"};
static const char *const kWRangeNames[4] = {"32'", "16'", "8'", "4'"};

// Presets: switches, faders 1-8, knobs. Set from the parameter meanings,
// not tuned by ear.
enum { WP_TRACK = 1, WP_SNAP = 2, WP_PING = 4 };
struct WestPreset {
    const char *name;
    uint8_t flags, fmode, fresp, lpg, range;
    uint8_t fader[8];  // ratio, fm, shape, timbre, sym, rise, fall, decay
    uint8_t knob[8];   // f>timbre, f>fm, uncertainty, bias, drift hz, drift, glide, bend
};
static const WestPreset kWPresets[] = {
    {"bongo", WP_TRACK | WP_SNAP | WP_PING, WF_TRANSIENT, WR_EXP, WL_COMBO, 2,
     {51, 25, 0, 20, 64, 20, 75, 60}, {0, 0, 20, 0, 50, 0, 0, 21}},
    {"marimba", WP_TRACK | WP_SNAP | WP_PING, WF_TRANSIENT, WR_EXP, WL_COMBO, 2,
     {102, 45, 0, 10, 64, 20, 70, 72}, {0, 40, 15, 0, 50, 0, 0, 21}},
    {"bell", WP_TRACK | WP_PING, WF_TRANSIENT, WR_EXP, WL_VCA, 3,
     {98, 70, 0, 0, 64, 20, 95, 105}, {0, 30, 10, 0, 50, 0, 0, 21}},
    {"folded lead", WP_TRACK | WP_SNAP, WF_SUSTAIN, WR_LOG, WL_COMBO, 2,
     {51, 0, 30, 60, 75, 45, 85, 70}, {40, 0, 10, 0, 60, 30, 35, 21}},
    {"bouncing cycle", WP_TRACK | WP_SNAP, WF_CYCLE, WR_EXP, WL_COMBO, 2,
     {76, 45, 50, 30, 64, 25, 60, 40}, {50, 30, 20, 0, 50, 0, 0, 21}},
    {"metal drone", WP_SNAP, WF_SUSTAIN, WR_LIN, WL_LP, 1,
     {60, 90, 60, 75, 90, 80, 100, 90}, {20, 40, 0, 30, 30, 60, 0, 21}},
    {"uncertain plonk", WP_TRACK | WP_PING, WF_TRANSIENT, WR_EXP, WL_COMBO, 2,
     {64, 40, 20, 30, 64, 20, 70, 65}, {30, 30, 110, 0, 80, 70, 0, 21}},
    {"low thump", WP_TRACK | WP_SNAP | WP_PING, WF_TRANSIENT, WR_EXP, WL_COMBO, 1,
     {26, 55, 0, 35, 70, 20, 60, 80}, {20, 20, 20, 0, 50, 0, 0, 21}},
};
const int kNumWPresets = sizeof(kWPresets) / sizeof(kWPresets[0]);
const uint8_t kWVolumeInit = 100;

struct WEvent { uint8_t type, a, b, c; };
struct WMidi { uint8_t s, d1, d2, pad_; };
const int kWRing = 64;
const int kWEvents = 8;

#pragma GCC push_options
#pragma GCC optimize("Os")
struct WestCtl {
    WMidi ring[kWRing];
    volatile uint8_t rhead, rtail;
    uint8_t fader[NUM_WFD];
    bool fpicked[NUM_WFD];
    int16_t fanchor_phys[NUM_WFD];
    uint8_t fanchor_val[NUM_WFD];
    uint8_t fphys[NUM_WFD];
    bool fphys_known[NUM_WFD];
    uint8_t knob[8];
    bool picked[8];
    int16_t anchor_phys[8];
    uint8_t anchor_val[8];
    uint8_t phys[8];
    bool phys_known[8];
    int preset, fmode, fresp, lpg, range;
    bool track, snap, ping, hold;
    float bend, modwheel, fold_pad, fm_pad;
    uint8_t down[128], latched[128];
    int held;
    uint8_t vnote[kWVoices];
    bool vgate[kWVoices];
    uint32_t vtrig[kWVoices], vstamp[kWVoices];
    float vvel[kWVoices];
    uint32_t stamp, ticks, restrike, reroll, rng;
    float strike_vel;
    WEvent events[kWEvents];
    uint8_t ev_head, ev_count;
    uint32_t unmapped_seen[16][4];

    void push_event(uint8_t type, uint8_t a, uint8_t b = 0, uint8_t c = 0) {
        if (ev_count == kWEvents) return;
        WEvent &e = events[(ev_head + ev_count) % kWEvents];
        e.type = type; e.a = a; e.b = b; e.c = c;
        ev_count++;
    }
    bool pop_event(WEvent &e) {
        if (ev_count == 0) return false;
        e = events[ev_head];
        ev_head = (uint8_t)((ev_head + 1) % kWEvents);
        ev_count--;
        return true;
    }

    // ---- presets and pickup
    void load(const WestPreset &p) {
        for (int f = 0; f < 8; f++) fader[f] = p.fader[f];
        for (int k = 0; k < 8; k++) knob[k] = p.knob[k];
        track = p.flags & WP_TRACK; snap = p.flags & WP_SNAP; ping = p.flags & WP_PING;
        fmode = p.fmode; fresp = p.fresp; lpg = p.lpg; range = p.range;
    }
    void reanchor() {
        for (int k = 0; k < 8; k++) {
            const int d = (int)phys[k] - (int)knob[k];
            picked[k] = phys_known[k] && d >= -2 && d <= 2;
            anchor_phys[k] = phys_known[k] ? phys[k] : -1;
            anchor_val[k] = knob[k];
        }
        for (int f = 0; f < 8; f++) {
            const int d = (int)fphys[f] - (int)fader[f];
            fpicked[f] = fphys_known[f] && d >= -2 && d <= 2;
            fanchor_phys[f] = fphys_known[f] ? fphys[f] : -1;
            fanchor_val[f] = fader[f];
        }
    }
    void apply_preset(int i) {
        preset = i;
        load(kWPresets[i]);
        reanchor();
        push_event(WEV_PRESET, (uint8_t)i);
    }

    void init() {
        rhead = rtail = 0;
        for (int k = 0; k < 8; k++) { phys[k] = 0; phys_known[k] = false; picked[k] = true; anchor_phys[k] = -1; }
        for (int f = 0; f < NUM_WFD; f++) { fphys[f] = 0; fphys_known[f] = false; fpicked[f] = true; fanchor_phys[f] = -1; }
        hold = false;
        bend = modwheel = fold_pad = fm_pad = 0.0f;
        for (int n = 0; n < 128; n++) down[n] = latched[n] = 0;
        held = 0;
        for (int i = 0; i < kWVoices; i++) { vnote[i] = 60; vgate[i] = false; vtrig[i] = 0; vstamp[i] = 0; vvel[i] = 0.8f; }
        stamp = ticks = restrike = reroll = 0;
        rng = 0x1B873593u;
        strike_vel = 0.8f;
        ev_head = ev_count = 0;
        for (int c = 0; c < 16; c++)
            for (int w = 0; w < 4; w++) unmapped_seen[c][w] = 0;
        preset = 0;
        load(kWPresets[0]);
        fader[WFD_VOL] = kWVolumeInit;
        for (int k = 0; k < 8; k++) anchor_val[k] = knob[k];
        for (int f = 0; f < NUM_WFD; f++) fanchor_val[f] = fader[f];
        publish();
    }

    void midi(uint8_t s, uint8_t d1, uint8_t d2) {
        const uint8_t next = (uint8_t)((rtail + 1) % kWRing);
        if (next == rhead) return;
        ring[rtail].s = s; ring[rtail].d1 = d1; ring[rtail].d2 = d2;
        __asm__ volatile("" ::: "memory");
        rtail = next;
    }

    // ---- keys
    bool is_held(int n) const { return down[n] || latched[n]; }
    void voice_on(int i, int n, float vel) {
        vnote[i] = (uint8_t)n; vgate[i] = true; vtrig[i]++; vstamp[i] = ++stamp; vvel[i] = vel;
    }
    void voice_off(int i) { if (vgate[i]) { vgate[i] = false; vstamp[i] = ++stamp; } }
    void key_on(int n, float vel) {
        if (!is_held(n)) held++;
        down[n] = 1;
        if (hold) latched[n] = 1;
        for (int i = 0; i < kWVoices; i++)
            if (vgate[i] && vnote[i] == n) { voice_on(i, n, vel); return; }
        int best = -1;
        for (int i = 0; i < kWVoices; i++)
            if (!vgate[i] && (best < 0 || vstamp[i] < vstamp[best])) best = i;
        if (best < 0)
            for (int i = 0; i < kWVoices; i++)
                if (best < 0 || vstamp[i] < vstamp[best]) best = i;
        voice_on(best, n, vel);
    }
    void key_off(int n) {
        if (!down[n]) return;
        down[n] = 0;
        if (latched[n]) return;
        held--;
        for (int i = 0; i < kWVoices; i++)
            if (vgate[i] && !is_held(vnote[i])) voice_off(i);
    }
    void set_hold(bool on) {
        hold = on;
        push_event(WEV_HOLD, on);
        for (int n = 0; n < 128; n++) latched[n] = on ? down[n] : 0;
        if (on) return;
        held = 0;
        for (int n = 0; n < 128; n++) held += down[n];
        for (int i = 0; i < kWVoices; i++)
            if (vgate[i] && !is_held(vnote[i])) voice_off(i);
    }
    void all_off() {
        for (int n = 0; n < 128; n++) down[n] = latched[n] = 0;
        held = 0;
        for (int i = 0; i < kWVoices; i++) voice_off(i);
    }

    // ---- controls
    uint32_t rnd() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    uint8_t rnd_in(int lo, int hi) { return (uint8_t)(lo + (int)(rnd() % (uint32_t)(hi - lo + 1))); }
    // DICE: the oscillator and the folder (faders 1-5) at random, within
    // playable ranges; the function, the gate and the volume stay.
    void dice() {
        rng ^= ticks * 2654435761u;
        if (rng == 0) rng = 1;
        fader[WFD_RATIO] = rnd_in(20, 110);
        fader[WFD_FM] = rnd_in(0, 90);
        fader[WFD_SHAPE] = rnd_in(0, 127);
        fader[WFD_TIMBRE] = rnd_in(0, 100);
        fader[WFD_SYM] = rnd_in(30, 100);
        snap = (rnd() & 3) != 0;
        reanchor();
        push_event(WEV_DICE, fader[WFD_RATIO], fader[WFD_FM], fader[WFD_TIMBRE]);
    }
    void on_knob(int k, uint8_t v) {
        if (Launchkey::follow(knob[k], picked[k], anchor_phys[k], anchor_val[k], v))
            push_event(WEV_PICKED, 0, (uint8_t)k, knob[k]);
        phys[k] = v;
        phys_known[k] = true;
    }
    void on_fader(int f, uint8_t v) {
        if (Launchkey::follow(fader[f], fpicked[f], fanchor_phys[f], fanchor_val[f], v))
            push_event(WEV_PICKED, kWPickedFader, (uint8_t)f, fader[f]);
        fphys[f] = v;
        fphys_known[f] = true;
    }
    void on_button(int b) {
        switch (b) {
        case 0: track = !track; push_event(WEV_TRACK, track); break;
        case 1: snap = !snap; push_event(WEV_SNAP, snap); break;
        case 2: fmode = (fmode + 1) % NUM_WF; push_event(WEV_FMODE, (uint8_t)fmode); break;
        case 3: fresp = (fresp + 1) % NUM_WR; push_event(WEV_FRESP, (uint8_t)fresp); break;
        case 4: lpg = (lpg + 1) % NUM_WL; push_event(WEV_LPG, (uint8_t)lpg); break;
        case 5: ping = !ping; push_event(WEV_PING, ping); break;
        case 8: apply_preset((preset + 1) % kNumWPresets); break;
        default: break;  // 7, 8 free
        }
    }
    void step_range(int d) {
        const int r = range + d;
        if (r < 0 || r > 3) return;
        range = r;
        push_event(WEV_RANGE, (uint8_t)range);
    }
    void unmapped(uint8_t ch, uint8_t num, uint8_t v) {
        uint32_t &w = unmapped_seen[ch][num >> 5];
        const uint32_t bit = 1u << (num & 31);
        if (w & bit) return;
        w |= bit;
        push_event(WEV_UNMAPPED, (uint8_t)(ch + 1), num, v);
    }

    void handle(uint8_t status, uint8_t d1, uint8_t d2) {
        const uint8_t type = status & 0xF0, ch = status & 0x0F;
        if (type == 0x90 && ch == 0) {
            if (d2 > 0) key_on(d1 & 127, w_unit(d2)); else key_off(d1 & 127);
        } else if (type == 0x80 && ch == 0) {
            key_off(d1 & 127);
        } else if (type == 0xB0 && ch == 0) {
            if (d1 >= 21 && d1 <= 28) on_knob(d1 - 21, d2);
            else if (d1 >= 71 && d1 <= 79) on_fader(d1 - 71, d2);
            else if (d1 >= 11 && d1 <= 19) { if (d2 > 0) on_button(d1 - 11); }
            else if (d1 == 1) modwheel = w_unit(d2);
            else if (d1 == 123 || d1 == 120) all_off();
            else unmapped(ch, d1, d2);
        } else if (type == 0xE0 && ch == 0) {
            bend = clampf(((float)(((int)d2 << 7) | d1) - 8192.0f) / 8191.0f, -1.0f, 1.0f);
        } else if ((type == 0x90 || type == 0x80) && ch == 9) {
            const bool on = type == 0x90 && d2 > 0;
            if (on && d1 >= 40 && d1 <= 43) apply_preset(d1 - 40);       // top pads 1-4
            else if (on && d1 >= 48 && d1 <= 51) apply_preset(d1 - 44);  // top pads 5-8
            else if (d1 == 36) fold_pad = on ? w_unit(d2) : 0.0f;         // bottom 1, then pressure
            else if (d1 == 37) fm_pad = on ? w_unit(d2) : 0.0f;           // bottom 2
            else if (on && d1 == 38) { strike_vel = w_unit(d2); restrike++; }  // bottom 3
            else if (on && d1 == 39) reroll++;                              // bottom 4
        } else if (type == 0xA0 && ch == 9) {  // pad pressure
            if (d1 == 36 && fold_pad > 0.0f) fold_pad = w_unit(d2);
            else if (d1 == 37 && fm_pad > 0.0f) fm_pad = w_unit(d2);
        } else if (type == 0xB0 && ch == 15 && d2 > 0) {
            switch (d1) {
            case 102: step_range(-1); break;
            case 103: step_range(+1); break;
            case 74: dice(); break;
            case 115: set_hold(!hold); break;
            case 77: apply_preset(0); push_event(WEV_RESET, 0); break;
            case 51: case 52: case 104: case 105: case 106: case 107:
            case 75: case 76: case 116: case 117: case 118: break;  // free (106 + 107: the patch switch)
            default: unmapped(ch, d1, d2);
            }
        }
    }

    void tick() {
        ticks++;
        while (rhead != rtail) {
            const WMidi m = ring[rhead];
            __asm__ volatile("" ::: "memory");
            rhead = (uint8_t)((rhead + 1) % kWRing);
            handle(m.s, m.d1, m.d2);
        }
        publish();
    }

    void publish() {
        WestBus &b = wbus();
        WestParams &P = b.P;
        const float rr = w_ratio(fader[WFD_RATIO]);
        P.ratio = snap ? w_snap(rr) : rr;
        P.mo_track = track;
        P.mo_hz = w_mohz(fader[WFD_RATIO]);
        P.mo_shape = w_unit(fader[WFD_SHAPE]);
        P.fm = w_fm(fader[WFD_FM]) + 4.0f * fm_pad;
        P.timbre = clampf(w_unit(fader[WFD_TIMBRE]) + fold_pad, 0.0f, 1.0f);
        P.sym = 2.0f * w_unit(fader[WFD_SYM]) - 1.0f;
        P.range = range - 2;
        P.bend = bend * 12.0f * w_unit(knob[WK_BEND]) + modwheel * 0.0f;
        P.glide = 2.0f * w_sq(knob[WK_GLIDE]);
        P.rise = w_time(fader[WFD_RISE]);
        P.fall = w_time(fader[WFD_FALL]);
        P.fmode = fmode;
        P.fresp = fresp;
        P.f_timbre = w_unit(knob[WK_FTIMBRE]);
        P.f_fm = 4.0f * w_sq(knob[WK_FFM]);
        P.lpg_mode = lpg;
        P.ping = ping;
        P.lpg_decay = w_decay(fader[WFD_DECAY]);
        P.lpg_bias = w_sq(knob[WK_BIAS]);
        P.rnd_note = w_unit(knob[WK_RND]);
        P.drift = w_unit(knob[WK_DRIFT]);
        P.level = w_level(fader[WFD_VOL]);
        b.drift_hz = w_drifthz(knob[WK_DRIFTHZ]);
        for (int i = 0; i < kWVoices; i++) {
            b.note[i] = vnote[i];
            b.gate[i] = vgate[i];
            b.trig[i] = vtrig[i];
            b.vel[i] = vvel[i];
        }
        b.restrike = restrike;
        b.strike_vel = strike_vel;
        b.reroll = reroll;
    }
};
#pragma GCC pop_options

}  // namespace freak

#endif
