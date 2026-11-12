// Host render of the groove patch: the same controller, looper, drums, synth
// and mixer code as the board (lib/groove_*.h, with the Elements reverb from
// the firmware tree), driven by MIDI as the Launchkey would send it:
//   1. upper round button + pad 13: broken-beat drums for the 4-bar loop
//      (120 BPM, the style's tempo)
//   2. Record (starts the loop), play a pushed ii-V-iii-vi in E flat
//      (Fm9, Bb13, Gm9, Cm9, 3 + 5 beats each pair, as apc-chord's broken
//      beat progression), Record off
//   3. the loop plays on: drums and the recorded chords
//   make render_groove && ./tests/render_groove out.wav [kit dir] [preset] [drums|chords|tour]
// "tour": instead, each of the 16 styles for one lap, drums only, with its levels.
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../lib/grains_bank.h"
#include "../lib/groove_mix.h"

using namespace freak;

struct FileReader {
    FILE *f;
    uint32_t read(uint32_t off, uint8_t *buf, uint32_t n) {
        if (fseek(f, off, SEEK_SET) != 0) return 0;
        return (uint32_t)fread(buf, 1, n, f);
    }
};
struct HeapAlloc { void *operator()(uint32_t n) { return malloc(n); } };

static void write_wav2(const char *path, const std::vector<float> &l, const std::vector<float> &r) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t n = (uint32_t)l.size(), bytes = n * 4;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(48000); u32(48000 * 4); u16(4); u16(16);
    fwrite("data", 1, 4, f); u32(bytes);
    for (uint32_t i = 0; i < n; i++) {
        const int16_t s[2] = {(int16_t)lrintf(l[i] * 32767), (int16_t)lrintf(r[i] * 32767)};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "groove.wav";
    const char *dir = argc > 2 ? argv[2] : "objects/kit_bb";
    const int preset = argc > 3 ? atoi(argv[3]) : 0;
    const char *solo = argc > 4 ? argv[4] : "";  // "drums" or "chords": mute the other

    static std::vector<int16_t> builtin(builtin_total());
    Kit &k = kit();
    kit_init(k, builtin.data());
    static const char *names[kNumPads] = {"01_kick", "02_snare", "03_ghost", "04_clap", "05_chat", "06_ohat", "07_rim", "08_shake",
                                         "09_kick2", "10_snr2", "11_snap", "12_rim8", "13_chat9", "14_conga", "15_tom", "16_ride"};
    std::vector<uint32_t> stage((kMaxBlockAlign + kStage) / 4);
    int loaded = 0;
    for (int p = 0; p < kNumPads; p++) {
        char fn[512];
        snprintf(fn, sizeof fn, "%s/%s.wav", dir, names[p]);
        FILE *f = fopen(fn, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        const uint32_t size = (uint32_t)ftell(f);
        FileReader rd = {f};
        HeapAlloc alloc;
        const GrainSource s = load_wav(rd, size, (uint8_t *)stage.data(), alloc);
        fclose(f);
        if (s.len > 0) { kit_publish(k, p, s); loaded++; }
    }
    printf("kit: %d of 16 pads from %s\n", loaded, dir);

    static GrooveCtl c;
    c.init();
    c.apply_preset(preset);
    // GROOVE page knobs (as if turned): delay 3/16 (dotted 1/8), some reverb.
    c.knob[GV_GROOVE][GK_DTIME] = 50;  // zone 2: dotted 1/8
    c.knob[GV_GROOVE][GK_DFB] = 45;
    c.knob[GV_GROOVE][GK_DLEVEL] = 55;
    c.knob[GV_GROOVE][GK_RLEVEL] = 60;
    c.knob[GV_GROOVE][GK_RTIME] = 70;
    if (!strcmp(solo, "drums")) c.mute[GB_MCHORDS] = true;
    if (!strcmp(solo, "chords")) for (int i = 0; i < NUM_DRUM_GROUPS; i++) c.mute[i] = true;

    static Drums drums;
    drums.init();
    static Synth synth;
    synth.init();
    static Chorus chorus;
    chorus.init();
    static std::vector<float> dmem_l(kGrooveDelayLen), dmem_r(kGrooveDelayLen);
    static std::vector<uint16_t> rmem(kReverbBytes / 2);
    static elements::Reverb reverb;
    reverb.Init(rmem.data());
    static GrooveMix mix;
    mix.init(dmem_l.data(), dmem_r.data(), &reverb);

    std::vector<float> L, R;
    double sum = 0;
    float peak = 0;
    int nan = 0, drum_hits = 0, note_ons = 0;
    // One block of the whole patch, appended to L, R.
    auto block = [&]() {
        c.tick();
        const GrooveBus &b = gbus_groove();
        drum_hits += b.out.nd;
        for (int i = 0; i < b.out.nn; i++) note_ons += b.out.ntype[i] == EV_ON;
        float dl[kBlock], dr[kBlock], dsl[kBlock], dsr[kBlock], mono[kBlock], sl[kBlock], sr[kBlock], ssl[kBlock], ssr[kBlock], l[kBlock], r[kBlock];
        groove_drums_block(drums, b, dl, dr, dsl, dsr);
        groove_synth_block(synth, chorus, b, mono, sl, sr, ssl, ssr);
        mix.process(b, dl, dr, dsl, dsr, sl, sr, ssl, ssr, l, r);
        for (int i = 0; i < kBlock; i++) {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { nan++; l[i] = r[i] = 0; }
            L.push_back(l[i]);
            R.push_back(r[i]);
            sum += l[i] * l[i] + r[i] * r[i];
            peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
        }
    };
    // The pad note of kit pad k (inverse of GrooveCtl::drum_pad).
    auto pad_note = [](int k) {
        for (int n = 36; n <= 51; n++)
            if (GrooveCtl::drum_pad((uint8_t)n) == k) return n;
        return 36;
    };
    auto style = [&](int s) {
        c.midi(0xBF, 104, 127); c.midi(0x99, (uint8_t)pad_note(s), 100); c.midi(0x99, (uint8_t)pad_note(s), 0);
        c.midi(0xBF, 104, 0);
    };

    if (!strcmp(solo, "tour")) {
        // Each style as the patch writes it: one lap (4 bars) at its own
        // tempo, swing and drunk, then 0.5 s of tail; levels per style.
        for (int s = 0; s < kNumStyles; s++) {
            style(s);
            block();  // the style press is handled in this block
            c.midi(0xBF, 115, 127);
            const size_t from = L.size();
            const double tpb = (double)c.g.clock.inc / 4294967296.0;
            const long laps = (long)std::ceil(c.g.loop.len() / tpb);
            const int hits0 = drum_hits;
            float pk = 0;
            for (long i = 0; i < laps; i++) block();
            c.midi(0xBF, 116, 127);
            for (long i = 0; i < (long)(0.5f * kControlRate); i++) block();
            double s2 = 0;
            for (size_t i = from; i < L.size(); i++) {
                s2 += L[i] * L[i] + R[i] * R[i];
                pk = std::fmax(pk, std::fmax(std::fabs(L[i]), std::fabs(R[i])));
            }
            printf("%2d %-17s %3.0f BPM swing %3d drunk %3d: %3d hits, rms %.3f, peak %.3f\n", s + 1, kStyles[s].name,
                   c.g.clock.bpm, kStyles[s].swing, kStyles[s].drunk, drum_hits - hits0,
                   std::sqrt(s2 / (2.0 * (L.size() - from))), pk);
        }
        write_wav2(path, L, R);
        printf("%s: %.1f s, NaN %d, peak %.3f\n", path, L.size() / kSampleRate, nan, peak);
        return 0;
    }

    style(kStyleBrokenBeat);
    c.midi(0xBF, 117, 127);  // Record: starts the loop

    // MIDI as played: the chords in lap 1 (beats from the loop start), Record
    // off at the end of lap 1, Stop after lap 3.
    struct Midi { uint32_t at; uint8_t s, d1, d2; };
    struct Chord { float at, len; uint8_t n[4]; };
    const Chord prog[4] = {{0.0f, 2.75f, {56, 60, 63, 67}},    // Fm9  (Ab C Eb G)
                           {3.0f, 4.75f, {56, 62, 67, 72}},    // Bb13 (Ab D G C), pushed: 3 + 5 beats
                           {8.0f, 2.75f, {58, 62, 65, 69}},    // Gm9  (Bb D F A)
                           {11.0f, 4.75f, {51, 55, 58, 62}}};  // Cm9  (Eb G Bb D)
    std::vector<Midi> midi;
    for (const Chord &ch : prog)
        for (int j = 0; j < 4; j++) {
            midi.push_back({(uint32_t)(ch.at * kPPQ), 0x90, ch.n[j], (uint8_t)(92 - 6 * j)});
            midi.push_back({(uint32_t)((ch.at + ch.len) * kPPQ), 0x80, ch.n[j], 0});
        }
    const uint32_t len = 4 * kBarTicks;
    midi.push_back({len, 0xBF, 117, 127});      // Record off
    midi.push_back({3 * len, 0xBF, 116, 127});  // Stop
    std::sort(midi.begin(), midi.end(), [](const Midi &a, const Midi &b) { return a.at < b.at; });

    size_t next = 0;
    const long tail = (long)(1.5f * kControlRate);
    long after_stop = -1;
    for (long blk = 0; after_stop < tail; blk++) {
        while (next < midi.size() && c.g.now() >= midi[next].at && (c.g.clock.running || midi[next].at == 0)) {
            c.midi(midi[next].s, midi[next].d1, midi[next].d2);
            next++;
        }
        block();
        if (!c.g.clock.running && next >= midi.size()) after_stop++;
        if (blk > 20 * 60 * (long)kControlRate) break;  // safety
    }
    write_wav2(path, L, R);
    printf("%s: %.1f s at %.1f BPM, synth preset %s, loop %d bars, %d events; %d drum hits, %d note-ons;"
           " rms %.3f, peak %.3f, NaN %d\n",
           path, L.size() / kSampleRate, c.g.clock.bpm, kSynthPresets[c.preset].name, c.g.loop.bars, c.g.loop.n, drum_hits,
           note_ons, std::sqrt(sum / (2.0 * L.size())), peak, nan);
    return 0;
}
