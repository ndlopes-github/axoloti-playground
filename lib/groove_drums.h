// groove_drums.h: the groove patch's drum sampler: a kit of 16 one-shots
// (one per pad, files /kit/NN_name.wav on the SD card, NN = 01..16) and one
// playback voice per pad.
//
// Until a pad's file is loaded (or without a card) it plays a sound of the
// built-in kit, synthesised at init: kick, snare, hats, clap, rim, tom, in
// the roles of the real kit.
//
// Publication: the loader thread fills the free one of a pad's two slots and
// then flips the pad's index (one byte, a single store), with a compiler
// barrier between. A voice copies the source at its trigger and keeps the
// copy, so a swap never changes a sample under a playing voice, and a reader
// never sees half of a pointer/length pair.
//
// Header-only, C++11, no libm, every struct a POD.
#ifndef GROOVE_DRUMS_H
#define GROOVE_DRUMS_H

#include <stdint.h>

#include "freak_dsp.h"
#include "grains.h"
#include "groove_seq.h"

namespace freak {

enum { G_KICK, G_SNARE, G_HATS, G_PERC, NUM_DRUM_GROUPS };
// Mix group, pan (-1 left .. +1 right) and the built-in sound of each pad.
enum { B_KICK, B_SNARE, B_HAT, B_OHAT, B_CLAP, B_RIM, B_TOM, NUM_BUILTIN };
struct PadInfo { uint8_t group; float pan; uint8_t builtin; };
static const PadInfo kPads[kNumPads] = {
    {G_KICK, 0.0f, B_KICK},    {G_SNARE, 0.0f, B_SNARE},  {G_SNARE, 0.1f, B_SNARE},  {G_SNARE, -0.1f, B_CLAP},
    {G_HATS, 0.25f, B_HAT},    {G_HATS, 0.25f, B_OHAT},   {G_PERC, -0.2f, B_RIM},    {G_PERC, 0.35f, B_HAT},
    {G_KICK, 0.0f, B_KICK},    {G_SNARE, 0.05f, B_SNARE}, {G_PERC, -0.3f, B_CLAP},   {G_PERC, 0.2f, B_RIM},
    {G_HATS, 0.3f, B_HAT},     {G_PERC, -0.35f, B_TOM},   {G_PERC, -0.15f, B_TOM},   {G_HATS, 0.3f, B_OHAT},
};

struct Kit {
    GrainSource slot[kNumPads][2];
    volatile uint8_t cur[kNumPads];   // the slot in use
    volatile bool from_file[kNumPads];
    GrainSource builtin[NUM_BUILTIN];
    char name[kNumPads][13];
    volatile int32_t state;           // BANK_IDLE / LOADING / DONE (grains_bank.h's)
    volatile bool go;
    int32_t status, found, loaded, failed;
    uint32_t bytes, ms;
};
static Kit g_kit;
inline Kit &kit() { return g_kit; }

inline GrainSource kit_source(const Kit &k, int pad) { return k.slot[pad][k.cur[pad]]; }

inline void kit_publish(Kit &k, int pad, const GrainSource &s) {
    const uint8_t next = (uint8_t)(k.cur[pad] ^ 1u);
    k.slot[pad][next] = s;
    __asm__ volatile("" ::: "memory");  // the slot is written before the flip
    k.cur[pad] = next;
}

// Init-only code, compiled for size (see groove_seq.h).
#pragma GCC push_options
#pragma GCC optimize("Os")
// ---- built-in kit, synthesised: a few hundred ms each, 48 kHz.
struct Noise {  // xorshift32, uniform in [-1, 1)
    uint32_t r;
    float next() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (float)(r >> 8) * (2.0f / 16777216.0f) - 1.0f; }
};
inline int16_t to16(float x) { return (int16_t)(clampf(x, -1.0f, 1.0f) * 32000.0f); }
// Exponential decay e^(-t/tau) per sample, as a factor.
inline float decay_coef(float tau) { return 1.0f - one_pole_coef(tau, kSampleRate, 1.44269504f); }  // 2^-(1/(tau fs) log2 e)

inline int32_t builtin_len(int b) {
    static const int32_t ms[NUM_BUILTIN] = {450, 250, 60, 350, 300, 50, 400};
    return ms[b] * 48;
}
inline void make_builtin(int b, int16_t *d) {
    const int32_t n = builtin_len(b);
    Noise ns = {0x1234567u + (uint32_t)b};
    float env = 1.0f, ph = 0.0f, lp = 0.0f, hp = 0.0f;
    for (int32_t i = 0; i < n; i++) {
        const float t = (float)i / kSampleRate;
        float x = 0.0f;
        switch (b) {
        case B_KICK: {  // sine sweeping 150 -> 45 Hz, 0.12 s decay
            const float f = 45.0f + 105.0f * fast_exp2(-t * 40.0f);
            ph += f / kSampleRate;
            if (ph >= 1.0f) ph -= 1.0f;
            env *= decay_coef(0.12f);
            x = env * fast_sin(2.0f * kPi * ph);
            break;
        }
        case B_SNARE: {  // 190 Hz tone + noise, 60 ms
            ph += 190.0f / kSampleRate;
            if (ph >= 1.0f) ph -= 1.0f;
            env *= decay_coef(0.06f);
            x = env * (0.4f * fast_sin(2.0f * kPi * ph) + 0.6f * ns.next());
            break;
        }
        case B_HAT: case B_OHAT: {  // high-passed noise, 15 / 90 ms
            const float w = ns.next();
            hp = w - lp;
            lp += 0.3f * (w - lp);
            env *= decay_coef(b == B_HAT ? 0.015f : 0.09f);
            x = env * hp;
            break;
        }
        case B_CLAP: {  // three noise bursts 10 ms apart, then a 60 ms tail
            const float burst = t < 0.03f ? (t - 0.01f * (float)(int)(t * 100.0f) < 0.004f ? 1.0f : 0.25f) : 1.0f;
            if (t >= 0.03f) env *= decay_coef(0.06f);
            x = 0.8f * env * burst * ns.next();
            break;
        }
        case B_RIM: {  // 1.7 kHz click, 8 ms
            ph += 1700.0f / kSampleRate;
            if (ph >= 1.0f) ph -= 1.0f;
            env *= decay_coef(0.008f);
            x = env * fast_sin(2.0f * kPi * ph);
            break;
        }
        default: {  // tom: 110 -> 90 Hz, 0.15 s
            ph += (90.0f + 20.0f * fast_exp2(-t * 20.0f)) / kSampleRate;
            if (ph >= 1.0f) ph -= 1.0f;
            env *= decay_coef(0.15f);
            x = env * fast_sin(2.0f * kPi * ph);
        }
        }
        d[i] = to16(0.9f * x);
    }
}
inline int32_t builtin_total() {
    int32_t n = 0;
    for (int b = 0; b < NUM_BUILTIN; b++) n += (builtin_len(b) + 1) & ~1;  // keep each 4-byte aligned
    return n;
}
// Reset the kit to the built-in sounds, synthesised into mem
// (builtin_total() samples, 4-byte aligned); null mem: silent pads.
inline void kit_init(Kit &k, int16_t *mem) {
    for (int b = 0; b < NUM_BUILTIN; b++) {
        k.builtin[b].data = 0; k.builtin[b].len = 0; k.builtin[b].ratio = 1.0f;
        if (!mem) continue;
        make_builtin(b, mem);
        k.builtin[b].data = mem;
        k.builtin[b].len = builtin_len(b);
        mem += (builtin_len(b) + 1) & ~1;
    }
    for (int p = 0; p < kNumPads; p++) {
        k.slot[p][0] = k.builtin[kPads[p].builtin];
        k.slot[p][1] = k.slot[p][0];
        k.cur[p] = 0;
        k.from_file[p] = false;
        k.name[p][0] = 0;
    }
    k.state = 0;
    k.go = false;
    k.status = 0;
    k.found = k.loaded = k.failed = 0;
    k.bytes = k.ms = 0;
}

#pragma GCC pop_options

// ---- playback
struct DrumVoice {
    const int16_t *data;
    int32_t len, pos;
    float gl, gr;   // gains, pan included, with the int16 scale
    bool on;
};

// Velocity to gain: 0.1 + 0.9 (v/127)^2. A ghost stroke (45) is at 0.21
// (-13.5 dB), a hit (100) at 0.66, an accent (124) at 0.96.
inline float vel_gain(int v) { const float u = (float)v * (1.0f / 127.0f); return 0.1f + 0.9f * u * u; }

struct Drums {
    DrumVoice v[kNumPads];

    void init() { for (int i = 0; i < kNumPads; i++) { v[i].on = false; v[i].data = 0; v[i].len = v[i].pos = 0; } }

    // A hit restarts the pad's voice. The closed hats choke the open hat.
    void trigger(const Kit &k, int pad, int vel) {
        if (pad < 0 || pad >= kNumPads) return;
        if (pad == P_CHAT || pad == P_CHAT9) v[P_OHAT].on = false;
        const GrainSource s = kit_source(k, pad);
        DrumVoice &d = v[pad];
        if (!s.data || s.len <= 0) { d.on = false; return; }
        const float g = vel_gain(vel) * (1.0f / 32768.0f);
        const float ang = 0.25f * kPi * (kPads[pad].pan + 1.0f);  // equal power
        d.data = s.data;
        d.len = s.len;
        d.pos = 0;
        d.gl = g * fast_sin(0.5f * kPi - ang);
        d.gr = g * fast_sin(ang);
        d.on = true;
    }

    // One block: each pad's group gain (mutes as 0), and the send amount;
    // dry and send outputs are overwritten.
    void render(const float *group_gain, float send, float *dl, float *dr, float *sl, float *sr) {
        for (int i = 0; i < kBlock; i++) dl[i] = dr[i] = 0.0f;
        for (int p = 0; p < kNumPads; p++) {
            DrumVoice &d = v[p];
            if (!d.on) continue;
            const float gg = group_gain[kPads[p].group];
            const float gl = d.gl * gg, gr = d.gr * gg;
            int32_t n = d.len - d.pos;
            if (n > kBlock) n = kBlock;
            const int16_t *src = d.data + d.pos;
            for (int i = 0; i < n; i++) {
                const float x = (float)src[i];
                dl[i] += gl * x;
                dr[i] += gr * x;
            }
            d.pos += n;
            if (d.pos >= d.len) d.on = false;
        }
        for (int i = 0; i < kBlock; i++) { sl[i] = send * dl[i]; sr[i] = send * dr[i]; }
    }
};

}  // namespace freak

#endif
