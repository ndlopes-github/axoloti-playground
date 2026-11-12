// groove_seq.h: timing, loop and drum generator of the "groove" patch: a
// real-time looper for drums (the 16 pads) and chords (the keys, played on a
// 4-voice synth).
//
//  - Clock: ticks at 960 per quarter note (about 0.5 ms at 120 BPM), in
//    32.32 fixed point, so the tempo never drifts.
//  - Loop: 4 bars of events (a drum hit, a note on or off) sorted by
//    tick. Recording adds a pass; Undo swaps with a snapshot taken before
//    each change, so a second Undo is a redo. Capture puts the last bars
//    played (while the loop ran, not recording) into the loop.
//  - Player: plays the events as the clock passes them. Drum events on the
//    16th grid get swing and "drunk" lateness; both only ever delay, so a
//    small queue of pending events schedules them.
//  - Generator: 16 drum styles (funk, neo-soul, boom-bap, broken beat), in
//    apc-chord's notation and variation rules (src/grooves.c), each with
//    its own pads, tempo, swing and drunk.
//
// Header-only, C++11, no allocation, no libm, every struct a POD.
#ifndef GROOVE_SEQ_H
#define GROOVE_SEQ_H

#include <stdint.h>
#include <string.h>

#include "freak_dsp.h"

namespace freak {

const int kPPQ = 960;                              // ticks per quarter note
const int kStepTicks = kPPQ / 4;                   // a 16th: 240
const int kStepsPerBar = 16;
const int kBarTicks = kStepTicks * kStepsPerBar;   // 3840
const int kMaxBars = 4;                            // loop at most 15360 ticks: fits uint16
const int kNumPads = 16;
const int kMaxEvents = 768;                        // 4.6 KB, plus as much for undo
const int kPending = 48;
const int kHistory = 512;                          // played events kept for Capture

// apc-chord's eng_hash32: a 32-bit mix of two words.
inline uint32_t hash32(uint32_t a, uint32_t b) {
    uint32_t x = a ^ (b * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// Swing as on an MPC: the first 16th of each pair takes 50 % (swing 0) to
// 75 % (swing 1) of the pair, so a grid event on an odd step is late by
// swing / 2 of a step (at most 120 ticks).
inline uint32_t swing_delay(uint32_t step, float swing) {
    return (step & 1u) ? (uint32_t)(0.5f * clampf(swing, 0.0f, 1.0f) * kStepTicks + 0.5f) : 0u;
}

// The kit's pads (files NN_name.wav are pad NN - 1).
enum { P_KICK, P_SNARE, P_GHOST, P_CLAP, P_CHAT, P_OHAT, P_RIM, P_SHAKE,
       P_KICK2, P_SNR2, P_SNAP, P_RIM8, P_CHAT9, P_CONGA, P_TOM, P_RIDE };
inline bool is_kick(int pad) { return pad == P_KICK || pad == P_KICK2; }

// Drunk, as apc-chord's lateness(): how late pad's hit at absolute step g
// lands, a pure function of (pad, g), so it changes every bar; up to a third
// of a step at drunk 1, never for the kicks, which hold the groove.
inline uint32_t drunk_delay(uint32_t seed, int pad, uint32_t g, float drunk) {
    const uint32_t most = (uint32_t)(clampf(drunk, 0.0f, 1.0f) * (kStepTicks / 3));
    if (is_kick(pad) || most == 0) return 0;
    return hash32(seed ^ 0x6472756Bu, g * kNumPads + (uint32_t)pad) % (most + 1u);
}

struct Clock {
    uint64_t pos;   // ticks since Play, 32.32
    uint64_t inc;   // per block
    float bpm;
    bool running;

    // Ticks per block (at most 1.07 at 200 BPM) to 2^-24 of a tick, then
    // shifted to 32.32: a relative tempo error below 1.4e-7 at 40 BPM.
    void set_bpm(float b) {
        bpm = clampf(b, 40.0f, 200.0f);
        const float tpb = bpm * ((float)kPPQ / 60.0f) * (float)kBlock / kSampleRate;
        inc = (uint64_t)(uint32_t)(tpb * 16777216.0f + 0.5f) << 8;
    }
    void init() { pos = 0; running = false; set_bpm(120.0f); }
    uint32_t now() const { return (uint32_t)(pos >> 32); }
};

enum { EV_DRUM, EV_ON, EV_OFF };
enum { EVF_GRID = 1, EVF_SKIP = 2 };  // on the 16th grid (swing, drunk apply); do not play in this lap
struct Ev {
    uint16_t tick;   // within the loop
    uint8_t type;    // EV_*
    uint8_t a;       // pad, or MIDI note
    uint8_t vel;
    uint8_t flags;   // EVF_*
};

// What one block plays: read by the drum and synth objects in the same block.
const int kOutMax = 24;
struct BlockOut {
    uint8_t nd, nn;
    uint8_t dpad[kOutMax], dvel[kOutMax];
    uint8_t ntype[kOutMax], nnote[kOutMax], nvel[kOutMax];
    bool all_off;    // release every synth voice
    uint8_t click;   // metronome: 0, 1 beat, 2 bar
    void clear() { nd = nn = 0; all_off = false; click = 0; }
    void drum(int pad, int vel) {
        if (nd < kOutMax) { dpad[nd] = (uint8_t)pad; dvel[nd] = (uint8_t)vel; nd++; }
    }
    void note(int type, int n, int vel) {
        if (nn < kOutMax) { ntype[nn] = (uint8_t)type; nnote[nn] = (uint8_t)n; nvel[nn] = (uint8_t)vel; nn++; }
    }
};

// Everything below runs once per block or per event, not per sample, so it
// is compiled for size: at the patch's -O3 the groove patch did not fit in
// its 44 KB of SRAM1. GCC does not inline across differing optimisation
// options, so callers keep their own.
#pragma GCC push_options
#pragma GCC optimize("Os")

// ------------------------------------------------------------------ styles

// 16 drum styles, four each of funk, neo-soul, boom-bap and broken beat, in
// that order (on the pads: bottom row funk, neo-soul; top row boom-bap,
// broken beat). A style is a phrase of 1 or 2 bars: up to kMaxParts parts,
// each a pad and 16 characters per bar in apc-chord's notation: X accent,
// x hit, g ghost (always); O o q optional (a variation keeps each with
// p 1/2); * + a variation may add a hit / a ghost (p 1/3); . rest. A ghost
// stroke on the snare pad plays the ghost-snare pad instead. The patterns
// follow each genre's conventions (backbeat, kick placement, hat feel,
// swing, tempo); they are written here, not transcribed from records.
const int kMaxParts = 6;
struct StylePart { int8_t pad; const char *steps; };  // steps null: unused
struct GrooveStyle {
    const char *name;
    uint8_t bpm, swing, drunk;  // swing and drunk 0..127, as the knobs
    uint8_t bars;               // phrase length: 1 or 2
    StylePart part[kMaxParts];
};
const int kNumStyles = 16;
static const GrooveStyle kStyles[kNumStyles] = {
    // ---- funk: 16th hats, ghost-note snare, syncopated kick, near straight
    {"funk", 100, 10, 0, 1, {{P_KICK, "X.x....*..x..o.."}, {P_SNARE, "....X.+g.g..X..g"},
                             {P_CHAT9, "xgxgxgxgxgxgxg.g"}, {P_OHAT, "..............x."}}},
    {"funk break", 104, 0, 0, 2, {{P_KICK, "X..x..x...X.....X..x......x..x.."},
                                  {P_SNR2, "....X..g.+..X.......X..g.g..X.+g"},
                                  {P_CHAT, "x.x.x...x.x.x.x.x.x.x.x.x.x.x..."},
                                  {P_OHAT, "......x.......................x."},
                                  {P_CONGA, "...*.......*.......*.......*..*."}}},
    {"electro funk", 112, 0, 0, 1, {{P_KICK2, "X.....x.x.....o."}, {P_CLAP, "....X.......X..."},
                                    {P_SNARE, "....x.......x..."}, {P_CHAT, "XgxgXgxgXgxgXgxg"},
                                    {P_RIM8, "...*......x..*.."}}},
    {"funk shuffle", 96, 61, 10, 1, {{P_KICK, "X..x......x....o"}, {P_SNARE, ".g...g.gX..g.g.+"},
                                     {P_CHAT, "x.xxx.xxx.xxx.xx"}}},
    // ---- neo-soul: slow, laid back (swing and drunk high), rim or clap
    {"neo soul", 84, 61, 64, 2, {{P_KICK2, "X.........x.....X......x..x....o"},
                                 {P_RIM8, "....X.......X.......X.......X..+"},
                                 {P_CLAP, "............q...............q..."},
                                 {P_CHAT, "x.xxx.x.x.xxx.x.x.xxx.x.x.xxx.x."},
                                 {P_SHAKE, ".g.g.g.g.g.g.g.g.g.g.g.g.g.g.g.g"}}},
    {"neo soul slow", 76, 71, 50, 1, {{P_KICK, "X.....x..x......"}, {P_SNAP, "....X.......X..."},
                                      {P_CLAP, "....x.......x..."}, {P_CHAT, "x.x.x.x+x.x.x.x+"},
                                      {P_SHAKE, "..g...g...g...g."}}},
    {"neo soul drag", 88, 51, 90, 1, {{P_KICK, "X..x.....x.x...."}, {P_SNARE, "....X..+...+X..g"},
                                      {P_CHAT9, "x.x+x.x+x.x+x.x+"}, {P_SHAKE, "..x...x...x...x."}}},
    {"neo soul triplet", 80, 82, 40, 1, {{P_KICK2, "X......x..x....."}, {P_RIM, "....X.......X..."},
                                         {P_RIDE, "x...x..xx...x..x"}, {P_SNARE, "......g.....+.g."},
                                         {P_CONGA, "...o.......o...."}}},
    // ---- boom-bap: punchy kick, cracking snare on 2 and 4, swung
    {"boom bap", 90, 51, 10, 2, {{P_KICK, "X......x..X.....X.x.......X..o.."},
                                 {P_SNARE, "....X.......X.......X.......X..+"},
                                 {P_CHAT, "x.x.x.x+x.x.x.x.x.x.x.x+x.x.x..."},
                                 {P_OHAT, "..............................o."}}},
    {"boom bap dusty", 86, 61, 20, 1, {{P_KICK2, "X........xX....."}, {P_SNARE, "....X.......X..."},
                                       {P_RIM8, "......q.......q."}, {P_CHAT, "x.xxx.x.x.xxx.x."}}},
    {"boom bap shuffle", 92, 71, 0, 1, {{P_KICK, "X.....xX..x....."}, {P_SNARE, "....X.......X..."},
                                        {P_CLAP, "....x.......x..."}, {P_CHAT9, "xgxgxgxgxgxgxgxg"}}},
    {"boom bap jazzy", 88, 61, 30, 2, {{P_KICK, "X......x..x.....X.....x...x..x.."},
                                       {P_SNR2, "....X.......X.......X.......X..+"},
                                       {P_RIDE, "x...x..xx...x..xx...x..xx...x..x"},
                                       {P_CHAT, "....x.......x.......x.......x..."}}},
    // ---- broken beat: displaced snares and kicks, busy hats, percussion
    {"broken beat", 120, 20, 20, 2, {{P_KICK, "X..x.....x...x..X.....x..x..x..."},
                                     {P_SNARE, "....X....g....X.....X..g...X...."},
                                     {P_CHAT9, "xgxXxgxgxgxXxgxgxgxXxgxgxgxXxgxg"},
                                     {P_CONGA, "......x.......*...*.......x....."}}},
    {"broken clap", 124, 30, 10, 2, {{P_KICK, "X.....x..x......X..x......x..x.."},
                                     {P_CLAP, "....X.......X.......X......X...."},
                                     {P_RIM, "..x....x..x.......x....x.....x.."},
                                     {P_TOM, "..............*............*..x."},
                                     {P_CHAT, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x."}}},
    {"broken 3-3-2", 118, 41, 30, 1, {{P_KICK, "X..x..x...x....."}, {P_SNARE, "....X......X...+"},
                                      {P_CHAT9, "XggXggXggXggXgXg"}, {P_CONGA, ".....*........x."}}},
    {"broken swing", 112, 51, 30, 2, {{P_KICK2, "X.....x...x..x..X..x....x....x.."},
                                      {P_CLAP, "....X.......X.......X........X.."},
                                      {P_SNARE, "......g..g.............g..g...g."},
                                      {P_SHAKE, "xgxgxgxgxgxgxgxgxgxgxgxgxgxgxgxg"},
                                      {P_CONGA, ".x....x..x...x.....x......x..*.."}}},
};
const int kStyleBrokenBeat = 12;

const uint8_t kVelAccent = 124, kVelHit = 100, kVelGhost = 45, kVelGhostPad = 90;

// apc-chord's step_of: the stroke a character gives (0 rest, 1 hit, 2
// accent, 3 ghost); r in 0..5 is the variation's draw.
inline uint8_t style_stroke(char c, bool vary, uint32_t r) {
    const bool keep = !vary || r < 3;  // p 1/2
    const bool add = vary && r < 2;    // p 1/3
    switch (c) {
    case 'x': return 1;
    case 'X': return 2;
    case 'g': return 3;
    case 'o': return keep ? 1 : 0;
    case 'O': return keep ? 2 : 0;
    case 'q': return keep ? 3 : 0;
    case '*': return add ? 1 : 0;
    case '+': return add ? 3 : 0;
    default: return 0;
    }
}
inline int stroke_strength(uint8_t v) { return v == 2 ? 3 : v == 1 ? 2 : v == 3 ? 1 : 0; }

// Bar b of style g's phrase (b taken modulo its length) into
// grid[pad][step] (strokes, 0 = rest). vary: a variation drawn from h; each
// (part, phrase bar, step) has its own draw. Two parts on one pad: the
// stronger stroke.
inline void style_bar(int g, int b, bool vary, uint32_t h, uint8_t grid[kNumPads][kStepsPerBar]) {
    memset(grid, 0, kNumPads * kStepsPerBar);
    const GrooveStyle &s = kStyles[g];
    const int pb = b % s.bars;
    for (int i = 0; i < kMaxParts; i++) {
        const StylePart &p = s.part[i];
        if (!p.steps) continue;
        for (int st = 0; st < kStepsPerBar; st++) {
            const uint32_t r = hash32(h, (uint32_t)((i * 2 + pb) * kStepsPerBar + st)) % 6u;
            const uint8_t v = style_stroke(p.steps[pb * kStepsPerBar + st], vary, r);
            if (!v) continue;
            const int pad = (p.pad == P_SNARE && v == 3) ? (int)P_GHOST : (int)p.pad;
            if (stroke_strength(v) > stroke_strength(grid[pad][st])) grid[pad][st] = v;
        }
    }
}

inline uint8_t stroke_vel(int pad, uint8_t v) {
    if (pad == P_GHOST) return kVelGhostPad;
    return v == 2 ? kVelAccent : v == 3 ? kVelGhost : kVelHit;
}

// ------------------------------------------------------------------ loop

struct Loop {
    Ev ev[kMaxEvents];
    int n;
    int bars;             // the patch uses kMaxBars always; the tests also use 1
    Ev bak[kMaxEvents];   // snapshot for Undo
    int nbak;
    bool has_bak;

    void init(int b) { n = 0; bars = b; nbak = 0; has_bak = false; }
    uint32_t len() const { return (uint32_t)bars * kBarTicks; }

    void snapshot() {
        memcpy(bak, ev, (size_t)n * sizeof(Ev));
        nbak = n;
        has_bak = true;
    }
    // Undo: back to the snapshot; the current state becomes the snapshot,
    // so a second call is a redo.
    bool undo() {
        if (!has_bak) return false;
        const int m = n > nbak ? n : nbak;
        for (int i = 0; i < m; i++) { const Ev t = ev[i]; ev[i] = bak[i]; bak[i] = t; }
        const int t = n; n = nbak; nbak = t;
        return true;
    }

    // First index with tick >= t.
    int lower(uint32_t t) const {
        int lo = 0, hi = n;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (ev[mid].tick < t) lo = mid + 1; else hi = mid;
        }
        return lo;
    }
    // Insert keeping the ticks sorted, after any event with the same tick.
    // Returns the index, or -1 if the loop is full.
    int insert(const Ev &e) {
        if (n >= kMaxEvents) return -1;
        int i = lower((uint32_t)e.tick + 1u);
        memmove(&ev[i + 1], &ev[i], (size_t)(n - i) * sizeof(Ev));
        ev[i] = e;
        n++;
        return i;
    }
    // Remove the events that match: type < 0 any type; a < 0 any pad/note.
    int erase(int type, int a) {
        int j = 0;
        for (int i = 0; i < n; i++) {
            const bool hit = (type < 0 || ev[i].type == type || (type == EV_ON && ev[i].type == EV_OFF)) &&
                             (a < 0 || ev[i].a == a);
            if (!hit) ev[j++] = ev[i];
        }
        const int removed = n - j;
        n = j;
        return removed;
    }
};

// ------------------------------------------------------------------ engine

struct Pending { uint32_t at; uint8_t type, a, vel; };
struct HistEv { uint32_t at; uint8_t type, a, vel; };

struct Groove {
    Clock clock;
    Loop loop;
    uint32_t lap_start;   // absolute tick where this lap began
    uint32_t done;        // ticks of this lap already played (cursor boundary)
    int cursor;           // first event not yet played in this lap
    Pending pend[kPending];
    int npend;
    HistEv hist[kHistory];
    int hist_head, hist_n;
    bool recording, quantize, click_on;
    float swing, drunk;   // 0..1
    uint32_t seed;
    uint32_t gen_count;   // styles drawn, to vary the variations
    int style;            // the style the drums were last written from, -1 none
    uint8_t sounding[128];// notes on in the synth because of the loop (count)

    void init() {
        clock.init();
        loop.init(kMaxBars);
        lap_start = 0;
        done = 0;
        cursor = 0;
        npend = 0;
        hist_head = hist_n = 0;
        recording = false;
        quantize = true;
        click_on = false;
        swing = 0.0f;
        drunk = 0.0f;
        seed = 0x6E1D2B3Au;
        gen_count = 0;
        style = -1;
        for (int i = 0; i < 128; i++) sounding[i] = 0;
    }

    uint32_t now() const { return clock.now(); }
    uint32_t lap_pos() const { return clock.now() - lap_start; }  // < loop.len() between blocks

    // After the loop changed under the cursor: play on from the same place.
    void resync() {
        if (done > loop.len()) done = loop.len();
        cursor = loop.lower(done);
    }

    void release_loop_notes(BlockOut &out) {
        for (int k = 0; k < 128; k++)
            for (; sounding[k] > 0; sounding[k]--) out.note(EV_OFF, k, 0);
        npend = 0;
    }

    // ---- transport
    void play() {
        if (clock.running) return;
        clock.pos = 0;
        lap_start = 0;
        done = 0;
        cursor = 0;
        for (int i = 0; i < loop.n; i++) loop.ev[i].flags &= (uint8_t)~EVF_SKIP;
        clock.running = true;
    }
    void stop(BlockOut &out) {
        clock.running = false;
        recording = false;
        release_loop_notes(out);
    }
    // Record: from stopped, starts the loop and records from its start; while
    // running, punches in at once. Pressed again: back to playing.
    void record_toggle() {
        if (recording) { recording = false; return; }
        loop.snapshot();
        if (!clock.running) play();
        recording = true;
    }

    // ---- input: what the player plays now (also sounds it, through out)
    void push_hist(uint8_t type, uint8_t a, uint8_t vel) {
        HistEv &h = hist[(hist_head + hist_n) % kHistory];
        h.at = now(); h.type = type; h.a = a; h.vel = vel;
        if (hist_n < kHistory) hist_n++;
        else hist_head = (hist_head + 1) % kHistory;
    }
    // The 16th nearest to lap position p, taking swing into account: the
    // step k whose played time k*240 + swing_delay(k) is closest. Steps k-1,
    // k, k+1 with k = p / 240 are the only candidates (swing delays a step by
    // at most half a step). Not reduced modulo the loop: the step after the
    // last one means "the start of the next lap".
    uint32_t nearest_step(uint32_t p) const {
        const uint32_t k = p / kStepTicks;
        const uint32_t t0 = k * kStepTicks + swing_delay(k, swing);
        const uint32_t t1 = (k + 1) * kStepTicks + swing_delay(k + 1, swing);  // > p
        uint32_t best = k, d = p > t0 ? p - t0 : t0 - p;
        if (t1 - p < d) { best = k + 1; d = t1 - p; }
        if (k > 0) {
            const uint32_t tm = (k - 1) * kStepTicks + swing_delay(k - 1, swing);  // <= p
            if (p - tm < d) best = k - 1;
        }
        return best;
    }
    // Store an event played live at lap position p: on the grid (quantized
    // drums) or as played. It already sounded live, so if its stored place
    // is still ahead of the playhead in this lap (at or after done, or past
    // the loop end into the next lap's start) it is skipped once.
    void record_event(uint8_t type, uint8_t a, uint8_t vel, uint32_t p) {
        Ev e = {0, type, a, vel, 0};
        bool ahead;
        if (type == EV_DRUM && quantize) {
            const uint32_t steps = (uint32_t)loop.bars * kStepsPerBar, k = nearest_step(p);
            e.tick = (uint16_t)((k % steps) * kStepTicks);
            e.flags = EVF_GRID;
            ahead = k >= steps || e.tick >= done;
        } else {
            e.tick = (uint16_t)p;
            ahead = e.tick >= done;
        }
        if (ahead) e.flags |= EVF_SKIP;
        // The cursor is the first event at or after done. An event behind
        // done lands at or before the cursor's index (sorted insert), so
        // the cursor moves past it; one at or after done lands at or after
        // the cursor and will be met (and skipped once).
        const int i = loop.insert(e);
        if (i >= 0 && e.tick < done) cursor++;
    }
    void live(uint8_t type, uint8_t a, uint8_t vel, BlockOut &out) {
        if (type == EV_DRUM) out.drum(a, vel);
        else out.note(type, a, vel);
        if (recording && clock.running) record_event(type, a, vel, lap_pos());
        else push_hist(type, a, vel);
    }

    // Capture: the last loop-length of playing (heard over the running loop,
    // not recorded) goes into the loop as a pass, each event at its place in
    // the lap. Notes still held get their release at the capture.
    bool capture(const bool held[128]) {
        if (!clock.running || hist_n == 0) return false;
        loop.snapshot();
        const uint32_t len = loop.len(), t = now();
        for (int i = 0; i < hist_n; i++) {
            const HistEv &h = hist[(hist_head + i) % kHistory];
            if (t - h.at > len) continue;
            const uint32_t p = (h.at - lap_start + len * 64u) % len;  // + 64 laps: h.at may precede lap_start
            Ev e = {(uint16_t)p, h.type, h.a, h.vel, 0};
            bool wrapped = false;
            if (h.type == EV_DRUM && quantize) {
                const uint32_t steps = (uint32_t)loop.bars * kStepsPerBar, k = nearest_step(p);
                wrapped = k >= steps;
                e.tick = (uint16_t)((k % steps) * kStepTicks);
                e.flags = EVF_GRID;
            }
            // Heard in this lap already, and its place still to come: skip once.
            const bool this_lap = h.at - lap_start <= now() - lap_start;
            if (this_lap && (e.tick >= done || wrapped)) e.flags |= EVF_SKIP;
            loop.insert(e);
        }
        for (int k = 0; k < 128; k++)
            if (held[k]) {
                Ev e = {(uint16_t)(lap_pos() % len), EV_OFF, (uint8_t)k, 0, 0};
                loop.insert(e);
            }
        hist_n = 0;
        resync();
        return true;
    }

    // Drums from style g over the whole loop: the style's phrase repeated,
    // the last bar a variation (a turnaround). vary (the style's pad pressed
    // again): the repeated phrase is a variation as well, drawn anew and the
    // same in every repeat, so the loop still repeats. The drum part of the
    // loop is replaced (Undo brings it back). A new style (vary false) also
    // sets the tempo, swing and drunk; a variation keeps them as they are.
    void generate(int g, bool vary) {
        if (g < 0 || g >= kNumStyles) return;
        loop.snapshot();
        loop.erase(EV_DRUM, -1);
        const uint32_t h_main = hash32(seed ^ 0x67726F76u, ++gen_count);
        const uint32_t h_last = hash32(seed ^ 0x67726F76u, ++gen_count);
        uint8_t grid[kNumPads][kStepsPerBar];
        for (int b = 0; b < loop.bars; b++) {
            const bool last = b == loop.bars - 1;
            style_bar(g, b, vary || last, last ? h_last : h_main, grid);
            for (int pad = 0; pad < kNumPads; pad++)
                for (int st = 0; st < kStepsPerBar; st++)
                    if (grid[pad][st]) {
                        const Ev e = {(uint16_t)((b * kStepsPerBar + st) * kStepTicks), EV_DRUM, (uint8_t)pad,
                                      stroke_vel(pad, grid[pad][st]), EVF_GRID};
                        loop.insert(e);
                    }
        }
        if (!vary) {
            clock.set_bpm((float)kStyles[g].bpm);
            swing = kStyles[g].swing / 127.0f;
            drunk = kStyles[g].drunk / 127.0f;
        }
        style = g;
        resync();
    }

    // ---- one block
    void emit(uint8_t type, uint8_t a, uint8_t vel, BlockOut &out) {
        if (type == EV_DRUM) { out.drum(a, vel); return; }
        if (type == EV_ON) { out.note(EV_ON, a, vel); if (sounding[a] < 255) sounding[a]++; return; }
        if (sounding[a] > 0) { sounding[a]--; out.note(EV_OFF, a, 0); }
    }
    void queue(uint32_t at, const Ev &e, BlockOut &out) {
        if (npend < kPending) {
            Pending &q = pend[npend++];
            q.at = at; q.type = e.type; q.a = e.a; q.vel = e.vel;
        } else {
            emit(e.type, e.a, e.vel, out);  // queue full: play it now rather than lose it
        }
    }
    // Play the lap's events with ticks in [done, upto).
    void play_until(uint32_t upto, BlockOut &out) {
        while (cursor < loop.n && loop.ev[cursor].tick < upto) {
            Ev &e = loop.ev[cursor++];
            if (e.flags & EVF_SKIP) { e.flags &= (uint8_t)~EVF_SKIP; continue; }
            uint32_t delay = 0;
            if (e.type == EV_DRUM && (e.flags & EVF_GRID)) {
                const uint32_t k = e.tick / kStepTicks;
                const uint32_t g = (lap_start + e.tick) / kStepTicks;  // absolute step: drunk changes every lap
                delay = swing_delay(k, swing) + drunk_delay(seed, e.a, g, drunk);
            }
            if (delay == 0) emit(e.type, e.a, e.vel, out);
            else queue(lap_start + e.tick + delay, e, out);
        }
        done = upto;
    }
    void tick(BlockOut &out) {
        out.clear();
        if (!clock.running) return;
        const uint32_t t0 = clock.now();
        clock.pos += clock.inc;
        const uint32_t t1 = clock.now();
        if (click_on && t1 / kPPQ != t0 / kPPQ)  // a beat begins in this block
            out.click = ((t1 / kPPQ) * kPPQ - lap_start) % kBarTicks == 0 ? 2 : 1;
        const uint32_t len = loop.len();
        while (t1 - lap_start >= len) {  // the lap ends in this block
            play_until(len, out);
            lap_start += len;
            done = 0;
            cursor = 0;
        }
        play_until(t1 - lap_start, out);
        for (int i = 0; i < npend;) {
            if ((int32_t)(t1 - pend[i].at) >= 0) {
                emit(pend[i].type, pend[i].a, pend[i].vel, out);
                pend[i] = pend[--npend];
            } else {
                i++;
            }
        }
    }
};

#pragma GCC pop_options

}  // namespace freak

#endif
