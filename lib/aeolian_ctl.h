// aeolian_ctl.h: maps a Launchkey 49 MK3 (standalone mode) onto the aeolian
// wind harp (aeolian_harp.h). This file is the single source of truth for
// docs/aeolian_cheatsheet.tex.
//
// The keys tune the eight strings (they do not play notes: the wind does);
// the tuning mode (Track < >) decides how: UNISON (every string on the last
// key, as an aeolian harp is strung), HARMONIC (the last key's harmonic
// series 1..8), FIFTHS (stacked fifths and octaves), CHORD (the last eight
// keys, latched). A key also plucks the strings it retunes, by velocity.
// Faders 1-8 are the eight string diameters (thinner = higher shedding
// frequency = higher harmonics), their buttons switch strings on and off.
// Knobs: SOUND page (WIND, GUSTS, GUST TIME, COUPLING, RING, BRIGHT, AIR,
// BODY) and FX page (GLIDE, delay, reverb, as freak). Play / Stop / Record
// pick the weather: steady, gusty, Lorenz. Quantise holds the wind, Click
// plucks every string, Capture MIDI = dice, Undo = reset. Mod wheel: more
// wind (up to x2); pitch bend: string tension, +-2 semitones.
// Pads: top = presets 0-7, bottom = GUST, CALM, STORM, PLUCK, MUTE, SLACK,
// THROW, SWELL (while held, by pressure).
//
// Results go on two buses: the AeoBus (the harp) and freak's Bus (delay,
// reverb, volume: fx_delay_st and fx_reverb work unchanged).
// Header-only, C++11, no allocation; every type is a POD.
#ifndef AEOLIAN_CTL_H
#define AEOLIAN_CTL_H

#include <stdint.h>

#include "aeolian_harp.h"
#include "launchkey_ctl.h"

namespace freak {

enum { APAGE_SOUND, APAGE_FX, NUM_APAGES };
// SOUND page knobs
enum { AK_WIND, AK_GUSTS, AK_GUSTTIME, AK_COUPLING, AK_RING, AK_BRIGHT, AK_AIR, AK_BODY };
// FX page: freak's K_GLIDE .. K_RLP
enum { TUNE_UNISON, TUNE_HARMONIC, TUNE_FIFTHS, TUNE_CHORD, NUM_TUNINGS };
// Performance pads, bottom row left to right
enum { APERF_GUST, APERF_CALM, APERF_STORM, APERF_PLUCK, APERF_MUTE, APERF_SLACK, APERF_THROW, APERF_SWELL, NUM_APERF };
// Log events beyond freak's EV_*
enum { EV_TUNING = 20, EV_WEATHER, EV_FREEZE_WIND, EV_STRING };

static const char *const kTuningNames[NUM_TUNINGS] = {"UNISON", "HARMONIC", "FIFTHS", "CHORD"};
static const char *const kWeatherNames[NUM_WEATHER] = {"STEADY", "GUSTY", "LORENZ"};

// Interval of each string above the root (semitones), by tuning mode.
// HARMONIC: 12 log2 n, n = 1..8 (just intonation).
static const float kTuneOffsets[3][kHarpStrings] = {
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 12.0f, 19.0195500f, 24.0f, 27.8631371f, 31.0195500f, 33.6882590f, 36.0f},
    {0.0f, 7.0f, 12.0f, 19.0f, 24.0f, 31.0f, -12.0f, -5.0f},
};
// UNISON detunes each string a little (cents), so they beat slowly.
static const float kUnisonCents[kHarpStrings] = {0.0f, 3.0f, -3.0f, 5.0f, -5.0f, 2.0f, -2.0f, 7.0f};

// Presets: tuning, weather, root, string mask, both knob pages, the eight
// diameter faders. Chosen from the parameter meanings, not tuned by ear.
struct AeoPreset {
    const char *name;
    uint8_t tuning, weather, root, mask;
    uint8_t knob[NUM_APAGES][8];
    uint8_t fader[kHarpStrings];
};
// knob[SOUND]: wind, gusts, gust time, coupling, ring, bright, air, body
// knob[FX]:    glide, delay time, fb, mix, reverb amount, time, diffusion, lowpass
static const AeoPreset kAeoPresets[] = {
    {"breeze", TUNE_UNISON, WX_GUSTY, 45, 0xFF,
     {{60, 40, 70, 70, 95, 70, 20, 60}, {60, 80, 60, 30, 70, 100, 90, 80}},
     {20, 35, 50, 60, 75, 85, 95, 110}},
    {"still air", TUNE_HARMONIC, WX_STEADY, 38, 0xFF,
     {{38, 10, 90, 85, 115, 60, 10, 70}, {90, 90, 70, 35, 90, 120, 100, 70}},
     {60, 64, 68, 72, 76, 80, 84, 88}},
    {"gale", TUNE_UNISON, WX_GUSTY, 40, 0xFF,
     {{100, 70, 35, 60, 80, 90, 50, 40}, {30, 60, 50, 25, 60, 90, 80, 90}},
     {10, 25, 40, 55, 70, 85, 100, 115}},
    {"weather", TUNE_FIFTHS, WX_LORENZ, 43, 0xFF,
     {{70, 60, 60, 75, 100, 70, 25, 60}, {80, 100, 70, 35, 80, 110, 100, 80}},
     {30, 40, 50, 60, 70, 80, 90, 100}},
    {"fifths tower", TUNE_FIFTHS, WX_GUSTY, 36, 0xFF,
     {{64, 35, 90, 90, 110, 60, 15, 80}, {100, 70, 55, 30, 80, 115, 100, 70}},
     {70, 66, 62, 58, 54, 50, 90, 86}},
    {"glass strings", TUNE_UNISON, WX_GUSTY, 57, 0xFF,
     {{75, 45, 50, 80, 100, 110, 5, 30}, {50, 90, 70, 40, 85, 110, 110, 110}},
     {0, 6, 12, 18, 24, 30, 36, 42}},
    {"low cables", TUNE_HARMONIC, WX_GUSTY, 33, 0xFF,
     {{95, 40, 100, 90, 120, 40, 30, 90}, {110, 60, 50, 20, 70, 110, 90, 50}},
     {90, 95, 100, 105, 110, 115, 120, 127}},
    {"chord field", TUNE_CHORD, WX_LORENZ, 48, 0xFF,
     {{64, 50, 80, 75, 105, 75, 20, 60}, {70, 85, 65, 35, 85, 115, 100, 80}},
     {25, 40, 55, 70, 25, 40, 55, 70}},
};
const int kNumAeoPresets = sizeof(kAeoPresets) / sizeof(kAeoPresets[0]);

// Knob value maps (v = 0..127).
inline float aeo_wind(uint8_t v) { return 0.5f * fast_exp2(4.90689060f * unit(v)); }       // 0.5 .. 15 m/s (log2 30)
inline float aeo_sigma(uint8_t v) { return 0.6f * unit(v); }                               // 0 .. 0.6
inline float aeo_tau(uint8_t v) { return 0.2f * fast_exp2(5.64385619f * unit(v)); }        // 0.2 .. 10 s (log2 50)
inline float aeo_coupling(uint8_t v) { return 0.001f * fast_exp2(4.90689060f * unit(v)); } // 0.001 .. 0.03
inline float aeo_ring(uint8_t v) { return 0.3f * fast_exp2(5.90689060f * unit(v)); }       // 0.3 .. 18 s (log2 60)
inline float aeo_bright(uint8_t v) { return 0.02f * fast_exp2(5.64385619f * unit(v)); }    // kappa: 0.02 dark .. 1 flat (log2 50)
inline float aeo_diam(uint8_t v) { return 0.25f * fast_exp2(3.58496250f * unit(v)); }      // 0.25 .. 3 mm (log2 12)
inline float aeo_glide(uint8_t v) { return 4.0f * unit(v) * unit(v); }                     // 0 .. 4 s

const float kRootLow = 33.0f;  // A1: roots below are raised by octaves

#pragma GCC push_options
#pragma GCC optimize("Os")
struct AeolianCtl {
    uint8_t knob[NUM_APAGES][8];
    bool picked[NUM_APAGES][8];
    int16_t anchor_phys[NUM_APAGES][8];
    uint8_t anchor_val[NUM_APAGES][8];
    uint8_t phys[8];
    bool phys_known[8];
    uint8_t fader[NUM_FADERS];
    bool fpicked[NUM_FADERS];
    int16_t fanchor_phys[NUM_FADERS];
    uint8_t fanchor_val[NUM_FADERS];
    uint8_t fphys[NUM_FADERS];
    bool fphys_known[NUM_FADERS];
    int page, preset, tuning, weather;
    uint8_t mask;              // strings on
    bool freeze;
    float root;                // MIDI note
    uint8_t chord[kHarpStrings];  // the last keys, newest first (CHORD)
    int nchord;
    float note[kHarpStrings];  // the strings' notes
    uint32_t plucks[kHarpStrings];
    float pluck_amt;
    float bend, modwheel;
    float perf[NUM_APERF];
    uint32_t ticks, rng;
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

    // The strings' notes from the tuning mode, the root and the chord.
    // pluck_changed: pluck the strings whose note changed, by amount.
    void retune(float amount) {
        for (int i = 0; i < kHarpStrings; i++) {
            float n;
            if (tuning == TUNE_CHORD && nchord > 0)
                n = (float)chord[i % nchord] + 12.0f * (float)(i / nchord);
            else if (tuning == TUNE_CHORD)
                n = root;
            else if (tuning == TUNE_UNISON)
                n = root + 0.01f * kUnisonCents[i];
            else
                n = root + kTuneOffsets[tuning][i];
            if (n != note[i] && amount > 0.0f) plucks[i]++;
            note[i] = n;
        }
        if (amount > 0.0f) pluck_amt = amount;
    }

    void load(const AeoPreset &p) {
        for (int g = 0; g < NUM_APAGES; g++)
            for (int k = 0; k < 8; k++) knob[g][k] = p.knob[g][k];
        for (int f = 0; f < kHarpStrings; f++) fader[f] = p.fader[f];
        tuning = p.tuning;
        weather = p.weather;
        root = (float)p.root;
        mask = p.mask;
        retune(0.0f);
    }

    void init() {
        for (int g = 0; g < NUM_APAGES; g++)
            for (int k = 0; k < 8; k++) {
                picked[g][k] = (g == APAGE_SOUND);
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
        nchord = 0;
        for (int i = 0; i < kHarpStrings; i++) { chord[i] = 0; note[i] = 45.0f; plucks[i] = 0; }
        pluck_amt = 0.0f;
        preset = 0;
        load(kAeoPresets[0]);
        for (int g = 0; g < NUM_APAGES; g++)
            for (int k = 0; k < 8; k++) anchor_val[g][k] = knob[g][k];
        for (int f = 0; f < NUM_FADERS; f++) fanchor_val[f] = fader[f];
        page = APAGE_SOUND;
        freeze = false;
        bend = 0.0f; modwheel = 0.0f;
        for (int i = 0; i < NUM_APERF; i++) perf[i] = 0.0f;
        ticks = 0;
        rng = 0x3C6EF372u;
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
        load(kAeoPresets[i]);
        reanchor_knobs(page);
        reanchor_faders();
        push_event(EV_PRESET, (uint8_t)i, 0, 0);
    }
    void reset_sound() {
        apply_preset(0);
        push_event(EV_RESET, 0, 0, 0);
    }
    void next_preset() { apply_preset((preset + 1) % kNumAeoPresets); }

    uint32_t rnd() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    int rnd_in(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

    // Dice: tuning, weather, the SOUND page and the gauges. FX and volume stay.
    void dice() {
        rng ^= ticks * 2654435761u;
        if (rng == 0) rng = 1;
        tuning = rnd_in(0, NUM_TUNINGS - 1);
        weather = rnd_in(0, NUM_WEATHER - 1);
        uint8_t *s = knob[APAGE_SOUND];
        s[AK_WIND] = (uint8_t)rnd_in(40, 110);
        s[AK_GUSTS] = (uint8_t)rnd_in(10, 90);
        s[AK_GUSTTIME] = (uint8_t)rnd_in(20, 110);
        s[AK_COUPLING] = (uint8_t)rnd_in(40, 110);
        s[AK_RING] = (uint8_t)rnd_in(60, 127);
        s[AK_BRIGHT] = (uint8_t)rnd_in(30, 120);
        s[AK_AIR] = (uint8_t)rnd_in(0, 60);
        s[AK_BODY] = (uint8_t)rnd_in(0, 100);
        for (int f = 0; f < kHarpStrings; f++) fader[f] = (uint8_t)rnd_in(0, 127);
        retune(0.0f);
        if (page == APAGE_SOUND) reanchor_knobs(APAGE_SOUND);
        reanchor_faders();
        push_event(EV_DICE, (uint8_t)tuning, (uint8_t)weather, 0);
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
    }

    void on_fader(int f, uint8_t v) {
        if (Launchkey::follow(fader[f], fpicked[f], fanchor_phys[f], fanchor_val[f], v))
            push_event(EV_PICKED, kPickedFader, (uint8_t)f, fader[f]);
        fphys[f] = v;
        fphys_known[f] = true;
    }

    void on_button(int b) {  // fader buttons: 1-8 strings on/off, 9 next preset
        if (b == 8) { next_preset(); return; }
        mask ^= (uint8_t)(1u << b);
        push_event(EV_STRING, (uint8_t)b, (mask >> b) & 1, 0);
    }

    void set_tuning(int t) {
        tuning = (t + NUM_TUNINGS) % NUM_TUNINGS;
        retune(0.4f);
        push_event(EV_TUNING, (uint8_t)tuning, 0, 0);
    }

    void pluck_all(float amount) {
        for (int i = 0; i < kHarpStrings; i++) plucks[i]++;
        pluck_amt = amount;
    }

    // Keys: the root is the last key (raised by octaves to at least A1);
    // CHORD keeps the last eight keys.
    void key_on(uint8_t n, uint8_t v) {
        float r = (float)n;
        while (r < kRootLow) r += 12.0f;
        root = r;
        int j = 0;  // CHORD: n to the front, without repeats
        uint8_t keep[kHarpStrings];
        keep[j++] = (uint8_t)r;
        for (int i = 0; i < nchord && j < kHarpStrings; i++)
            if (chord[i] != (uint8_t)r) keep[j++] = chord[i];
        for (int i = 0; i < j; i++) chord[i] = keep[i];
        nchord = j;
        retune(unit(v));
    }

    static int pad_index(uint8_t note) { return Launchkey::pad_index(note); }
    void on_pad(uint8_t n, uint8_t v, bool pressure) {
        const int i = pad_index(n);
        if (i < 0) return;
        if (i < 8) {
            if (!pressure && v > 0) apply_preset(i);
            return;
        }
        const int p = i - 8;
        if (p == APERF_PLUCK && !pressure && v > 0) pluck_all(unit(v));
        perf[p] = unit(v);
    }

    void midi(uint8_t status, uint8_t d1, uint8_t d2) {
        const uint8_t type = status & 0xF0, ch = status & 0x0F;
        if (type == 0x90 && ch == 0) {
            if (d2 > 0) key_on(d1, d2);  // releases do nothing: the harp keeps its tuning
        } else if (type == 0xB0 && ch == 0) {
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
            case 51: case 106: set_page(APAGE_SOUND, d1); break;  // Device Select
            case 52: case 107: set_page(APAGE_FX, d1); break;     // Device Lock
            case 102: set_tuning(tuning - 1); break;              // track <
            case 103: set_tuning(tuning + 1); break;              // track >
            case 115: case 116: case 117:                          // Play, Stop, Record
                weather = d1 - 115;
                push_event(EV_WEATHER, (uint8_t)weather, 0, 0);
                break;
            case 75:                                               // Quantise
                freeze = !freeze;
                push_event(EV_FREEZE_WIND, freeze, 0, 0);
                break;
            case 76: pluck_all(0.8f); break;   // Click
            case 74: dice(); break;            // Capture MIDI
            case 77: reset_sound(); break;     // Undo
            case 104: case 105: case 118: break;  // free
            default: unmapped(ch, d1, d2);
            }
        }
    }

    // Every other control tick (1.5 kHz is plenty for knobs and keys, and
    // it saves ~2 % DSP): publish to both buses.
    void tick() {
        ticks++;
        if ((ticks & 1) == 0) publish();
    }

    void publish() {
        AeoBus &a = aeobus();
        Bus &b = bus();
        const uint8_t *s = knob[APAGE_SOUND], *x = knob[APAGE_FX];
        const float *pf = perf;
        for (int i = 0; i < kHarpStrings; i++) {
            a.note[i] = note[i];
            a.d[i] = aeo_diam(fader[i]);
            a.on[i] = (mask >> i) & 1;
            a.plucks[i] = plucks[i];
        }
        a.pluck_amt = pluck_amt;
        // wind: knob, mod wheel (up to x2), GUST (up to x2.5), STORM (x1.6), CALM (to 0)
        a.wind = aeo_wind(s[AK_WIND]) * fast_exp2(modwheel) * (1.0f + 1.5f * pf[APERF_GUST]) *
                 (1.0f + 0.6f * pf[APERF_STORM]) * (1.0f - pf[APERF_CALM]);
        a.sigma = aeo_sigma(s[AK_GUSTS]) + 0.5f * pf[APERF_STORM];
        a.tau = aeo_tau(s[AK_GUSTTIME]);
        a.weather = weather;
        a.freeze = freeze;
        a.coupling = aeo_coupling(s[AK_COUPLING]);
        a.t60 = aeo_ring(s[AK_RING]) * (1.0f - 0.97f * pf[APERF_MUTE]);
        a.bright = aeo_bright(s[AK_BRIGHT]);
        a.air = unit(s[AK_AIR]);
        a.body = unit(s[AK_BODY]);
        a.glide = aeo_glide(x[K_GLIDE]);
        a.bend = 2.0f * bend - 2.0f * pf[APERF_SLACK];
        // freak's Bus: delay, reverb, volume (fx_delay_st, fx_reverb)
        const float th = pf[APERF_THROW];
        b.delay_on = true;
        b.delay_t = 0.01f * fast_exp2(7.22881869f * unit(x[K_DTIME]));
        b.delay_fb = clampf(0.95f * unit(x[K_DFB]) + 0.4f * th, 0.0f, 0.95f);
        b.delay_mix = clampf(unit(x[K_DMIX]) + 0.6f * th, 0.0f, 1.0f);
        const float sw = pf[APERF_SWELL];
        b.rev_amount = clampf(unit(x[K_RAMOUNT]) + 0.7f * sw, 0.0f, 1.0f);
        b.rev_time = clampf(unit(x[K_RTIME]) + 0.3f * sw, 0.0f, 1.0f);
        b.rev_diff = unit(x[K_RDIFF]);
        b.rev_lp = unit(x[K_RLP]);
        const float m = unit(fader[F_MASTER]);
        b.master = m * m;
    }
};
#pragma GCC pop_options

}  // namespace freak

#endif
