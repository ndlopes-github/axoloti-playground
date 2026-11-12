// Host tests for the groove patch: clock, swing and drunk timing, the loop
// (record, quantise, skip-once, undo/redo, capture, length), the drum
// generator (apc-chord's rules), the synth and the drum voices, and the
// controller's MIDI paths. make test
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../lib/groove_ctl.h"

using namespace freak;

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            failures++;                                     \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                       \
            std::printf("\n");                              \
        }                                                   \
    } while (0)

// Run the groove block by block, collecting what it plays with the block's
// end tick (lap-relative and absolute).
struct Hit { uint32_t at; int type, a, vel; };
static std::vector<Hit> run(Groove &g, int blocks) {
    std::vector<Hit> hits;
    BlockOut out;
    for (int b = 0; b < blocks; b++) {
        g.tick(out);
        for (int i = 0; i < out.nd; i++) hits.push_back({g.now(), EV_DRUM, out.dpad[i], out.dvel[i]});
        for (int i = 0; i < out.nn; i++) hits.push_back({g.now(), out.ntype[i], out.nnote[i], out.nvel[i]});
    }
    return hits;
}
static int blocks_for_ticks(const Groove &g, double ticks) {
    return (int)std::ceil(ticks / ((double)g.clock.inc / 4294967296.0));
}

static void test_clock() {
    Clock c;
    c.init();
    const float bpms[3] = {40.0f, 120.0f, 200.0f};
    for (float bpm : bpms) {
        c.set_bpm(bpm);
        c.pos = 0;
        const int blocks = (int)(60.0f * kControlRate);  // one minute
        for (int i = 0; i < blocks; i++) c.pos += c.inc;
        const double want = bpm * kPPQ;  // ticks in a minute
        CHECK(std::fabs((double)c.now() - want) <= 1.0, "%g BPM: %u ticks in a minute, want %.0f", bpm, c.now(), want);
    }
}

static void test_swing_drunk() {
    CHECK(swing_delay(0, 1.0f) == 0 && swing_delay(2, 1.0f) == 0, "even steps never swing");
    CHECK(swing_delay(1, 0.0f) == 0, "no swing at 0");
    CHECK(swing_delay(1, 1.0f) == 120 && swing_delay(3, 0.5f) == 60, "swing 1: half a step (75 %% MPC)");
    uint32_t most = 0;
    bool kick_late = false;
    for (uint32_t g = 0; g < 2000; g++)
        for (int pad = 0; pad < kNumPads; pad++) {
            const uint32_t d = drunk_delay(123, pad, g, 1.0f);
            if (is_kick(pad) && d) kick_late = true;
            if (d > most) most = d;
            CHECK(drunk_delay(123, pad, g, 1.0f) == d, "drunk not a pure function");
        }
    CHECK(!kick_late, "a kick landed late");
    CHECK(most <= 80 && most >= 70, "drunk at most a third of a step: %u", most);
    CHECK(drunk_delay(123, P_SNARE, 5, 0.0f) == 0, "no drunk at 0");
}

static void test_generator() {
    // Every style: 16 characters per bar of its phrase (1 or 2 bars), from
    // the notation's alphabet, on a pad of the kit; at least a kick and a
    // backbeat part; tempo inside the tempo knob's range.
    bool ok = true;
    for (int s = 0; s < kNumStyles; s++) {
        const GrooveStyle &st = kStyles[s];
        ok = ok && (st.bars == 1 || st.bars == 2) && st.bpm >= 70 && st.bpm <= 140 && st.swing <= 127 && st.drunk <= 127;
        int parts = 0;
        for (int i = 0; i < kMaxParts; i++) {
            const char *p = st.part[i].steps;
            if (!p) continue;
            parts++;
            int n = 0;
            for (; p[n]; n++) ok = ok && std::strchr("XxgOoq*+.", p[n]) != nullptr;
            ok = ok && n == kStepsPerBar * st.bars && st.part[i].pad >= 0 && st.part[i].pad < kNumPads;
        }
        ok = ok && parts >= 3 && is_kick(st.part[0].pad);
        if (!ok) { std::printf("FAIL: style %d (%s) malformed\n", s, st.name); failures++; ok = true; }
    }
    // Written broken beat, both bars of its phrase: the fixed strokes on
    // their pads; a ghost on the snare goes to the ghost pad; '*' absent.
    uint8_t grid[kNumPads][kStepsPerBar];
    style_bar(kStyleBrokenBeat, 0, false, 1, grid);
    CHECK(grid[P_KICK][0] == 2 && grid[P_KICK][3] == 1 && grid[P_KICK][9] == 1 && grid[P_KICK][13] == 1,
          "bar 1 kick X..x.....x...x..");
    CHECK(grid[P_SNARE][4] == 2 && grid[P_SNARE][14] == 2 && grid[P_GHOST][9] == 3 && grid[P_SNARE][9] == 0,
          "bar 1 snare on 5 and 15, ghost on the ghost pad");
    CHECK(grid[P_CHAT9][0] == 1 && grid[P_CHAT9][1] == 3 && grid[P_CHAT9][3] == 2, "909 hats xgxX");
    CHECK(grid[P_CONGA][6] == 1 && grid[P_CONGA][14] == 0, "conga x, and '*' absent when written");
    style_bar(kStyleBrokenBeat, 1, false, 1, grid);
    CHECK(grid[P_KICK][6] == 1 && grid[P_KICK][3] == 0 && grid[P_SNARE][11] == 2, "bar 2 is the phrase's second bar");
    style_bar(kStyleBrokenBeat, 2, false, 1, grid);
    CHECK(grid[P_KICK][3] == 1 && grid[P_SNARE][14] == 2, "bar 3 wraps to the phrase's first bar");
    // Variations of funk (1 bar): fixed strokes always; 'o' kept with p 1/2;
    // '*' and '+' added with p 1/3 each (the '+' as a ghost-pad stroke).
    const int kFunk = 0;
    int opt = 0, add = 0, ghost_add = 0;
    const int N = 6000;
    bool fixed_ok = true;
    for (int i = 0; i < N; i++) {
        style_bar(kFunk, 0, true, hash32(99, (uint32_t)i), grid);
        fixed_ok = fixed_ok && grid[P_KICK][0] == 2 && grid[P_SNARE][4] == 2 && grid[P_GHOST][7] == 3 &&
                   grid[P_CHAT9][1] == 3 && grid[P_OHAT][14] == 1;
        opt += grid[P_KICK][13] != 0;       // 'o'
        add += grid[P_KICK][7] != 0;        // '*'
        ghost_add += grid[P_GHOST][6] != 0; // '+' on the snare
    }
    CHECK(fixed_ok, "a variation dropped a fixed stroke");
    const double po = (double)opt / N, pa = (double)add / N, pg = (double)ghost_add / N;
    CHECK(std::fabs(po - 0.5) < 0.03, "optional kept with p %.3f, want 1/2", po);
    CHECK(std::fabs(pa - 1.0 / 3) < 0.03, "hit added with p %.3f, want 1/3", pa);
    CHECK(std::fabs(pg - 1.0 / 3) < 0.03, "ghost added with p %.3f, want 1/3", pg);
    std::printf("info: funk variations: optional kept %.3f, hit added %.3f, ghost added %.3f\n", po, pa, pg);

    // The 4-bar loop: the phrase repeats, the last bar is a variation; a
    // variation keeps the repeats identical; a new style sets the tempo,
    // swing and drunk, a variation keeps them.
    static Groove g;
    g.init();
    CHECK(g.loop.bars == 4, "the loop is 4 bars");
    auto bar_of = [&](int b, uint8_t out[kNumPads][kStepsPerBar]) {
        std::memset(out, 0, kNumPads * kStepsPerBar);
        for (int i = 0; i < g.loop.n; i++) {
            const Ev &e = g.loop.ev[i];
            if (e.type == EV_DRUM && e.tick / kBarTicks == (uint32_t)b) out[e.a][(e.tick % kBarTicks) / kStepTicks] = e.vel;
        }
    };
    uint8_t b0[kNumPads][kStepsPerBar], b1[kNumPads][kStepsPerBar], b2[kNumPads][kStepsPerBar],
        b3[kNumPads][kStepsPerBar];
    g.generate(kFunk, false);
    bar_of(0, b0); bar_of(1, b1); bar_of(2, b2); bar_of(3, b3);
    CHECK(!std::memcmp(b0, b1, sizeof b0) && !std::memcmp(b0, b2, sizeof b0), "funk: bars 1-3 as written");
    CHECK(b3[P_KICK][0] == kVelAccent && b3[P_SNARE][4] == kVelAccent, "bar 4 keeps the fixed strokes");
    CHECK(std::fabs(g.clock.bpm - 100.0f) < 1e-3f && std::fabs(g.swing - 10 / 127.0f) < 1e-6f && g.drunk == 0.0f,
          "funk sets 100 BPM, swing 10, drunk 0 (%g %g %g)", g.clock.bpm, g.swing, g.drunk);
    g.clock.set_bpm(97.0f);
    g.swing = 0.3f;
    int differs = 0;
    for (int k = 0; k < 20; k++) {
        g.generate(kFunk, true);
        bar_of(0, b0); bar_of(1, b1); bar_of(2, b2);
        CHECK(!std::memcmp(b0, b1, sizeof b0) && !std::memcmp(b0, b2, sizeof b0), "variation: bars 1-3 the same");
        uint8_t w[kNumPads][kStepsPerBar];
        style_bar(kFunk, 0, false, 0, w);
        bool same = true;
        for (int p = 0; p < kNumPads; p++)
            for (int s = 0; s < kStepsPerBar; s++) same = same && ((b0[p][s] != 0) == (w[p][s] != 0));
        differs += !same;
    }
    CHECK(differs >= 15, "variations differ from the written bar (%d of 20)", differs);
    CHECK(std::fabs(g.clock.bpm - 97.0f) < 1e-3f && std::fabs(g.swing - 0.3f) < 1e-6f, "a variation keeps tempo and swing");
    g.generate(kStyleBrokenBeat, false);
    bar_of(0, b0); bar_of(2, b2); bar_of(1, b1);
    CHECK(!std::memcmp(b0, b2, sizeof b0) && std::memcmp(b0, b1, sizeof b0), "2-bar phrase: bar 3 = bar 1 != bar 2");
    CHECK(std::fabs(g.clock.bpm - 120.0f) < 1e-3f, "broken beat at 120 BPM");
}

// One lap of a generated loop: every event exactly once, at its tick plus
// swing plus drunk (within one block), never early.
static void test_playback() {
    Groove g;
    g.init();
    g.loop.init(2);
    g.generate(kStyleBrokenBeat, false);
    g.swing = 0.5f;
    g.drunk = 0.6f;
    const int nev = g.loop.n;
    g.play();
    const double tick_per_block = (double)g.clock.inc / 4294967296.0;
    std::vector<Hit> hits = run(g, blocks_for_ticks(g, g.loop.len() + 200));
    int in_lap = 0;
    bool timing_ok = true;
    for (const Hit &h : hits) {
        if (h.at > g.loop.len()) {  // fired in the next lap (late strokes of the last steps, or lap 2)
            continue;
        }
        in_lap++;
    }
    // Match each loop event to a hit at its due time.
    std::vector<bool> used(hits.size(), false);
    int matched = 0;
    for (int i = 0; i < nev; i++) {
        const Ev &e = g.loop.ev[i];
        const uint32_t k = e.tick / kStepTicks;
        const uint32_t due = e.tick + swing_delay(k, g.swing) + drunk_delay(g.seed, e.a, e.tick / kStepTicks, g.drunk);
        for (size_t j = 0; j < hits.size(); j++)
            if (!used[j] && hits[j].a == e.a && hits[j].at >= due && hits[j].at < due + tick_per_block + 1) {
                used[j] = true;
                matched++;
                break;
            }
    }
    CHECK(matched == nev, "%d of %d events played on time", matched, nev);
    CHECK(timing_ok && in_lap >= nev - 4, "lap 1 played %d hits for %d events", in_lap, nev);
}

// Recording: hits played live sound once in their lap, then play from the
// loop on the (swung) grid. A hit just before the loop end quantises to the
// next lap's start and is not doubled there.
static void test_record() {
    Groove g;
    g.init();
    g.loop.init(1);
    g.swing = 0.5f;
    g.record_toggle();  // starts the loop and records
    CHECK(g.clock.running && g.recording, "record from stopped starts the loop");
    BlockOut out;
    const double tpb = (double)g.clock.inc / 4294967296.0;
    // Live hits at lap positions (ticks): near step 2, near odd step 5 (swung
    // to 1200 + 60), and 30 ticks before the loop end (forward to step 0).
    const uint32_t when[3] = {470, 1255, 3810};
    const int pads[3] = {P_KICK, P_SNARE, P_CHAT};
    int live = 0, w = 0;
    std::vector<Hit> lap1;
    while (g.now() < g.loop.len()) {
        g.tick(out);
        for (int i = 0; i < out.nd; i++) lap1.push_back({g.now(), EV_DRUM, out.dpad[i], 0});
        while (w < 3 && g.now() >= when[w]) {
            g.live(EV_DRUM, (uint8_t)pads[w], 100, out);
            live++;
            w++;
        }
    }
    CHECK(lap1.empty(), "a recorded hit played again in its own lap (%zu)", lap1.size());
    CHECK(g.loop.n == 3, "%d events recorded", g.loop.n);
    CHECK(g.loop.ev[0].tick == 0 && g.loop.ev[0].a == P_CHAT, "the late hit is stored at step 0");
    CHECK(g.loop.ev[1].tick == 480 && g.loop.ev[2].tick == 1200, "quantised to steps 2 and 5");
    // Lap 2: the hit at step 0 was heard 30 ticks before: skipped once. Lap 3: all three.
    std::vector<Hit> lap2 = run(g, blocks_for_ticks(g, g.loop.len()));
    int n2 = 0;
    for (const Hit &h : lap2) if (h.at <= 2 * g.loop.len()) n2++;
    CHECK(n2 == 2, "lap 2 played %d (want the two not heard just before)", n2);
    std::vector<Hit> lap3 = run(g, blocks_for_ticks(g, g.loop.len()));
    int n3 = 0, on_time = 0;
    for (const Hit &h : lap3) {
        n3++;
        const uint32_t p = (h.at - 2 * g.loop.len()) % g.loop.len();  // the last block may reach the next lap
        const uint32_t due = h.a == P_CHAT ? 0 : h.a == P_KICK ? 480 : 1200 + 60;
        if (p >= due && p < due + tpb + 1) on_time++;
    }
    CHECK(n3 == 3 && on_time == 3, "lap 3: %d hits, %d on the swung grid", n3, on_time);
    // Undo removes the pass; redo brings it back.
    g.record_toggle();
    CHECK(g.loop.undo() && g.loop.n == 0, "undo of the first pass empties the loop");
    CHECK(g.loop.undo() && g.loop.n == 3, "redo brings the three hits back");
}

// Notes are kept as played (no grid) and released.
static void test_notes_and_capture() {
    Groove g;
    g.init();
    g.loop.init(1);
    g.play();
    BlockOut out;
    bool held[128] = {false};
    // Not recording: play a chord over the running loop, then Capture.
    while (g.now() < 1000) g.tick(out);
    const uint8_t chord[4] = {60, 63, 67, 70};
    for (uint8_t n : chord) { g.live(EV_ON, n, 90, out); held[n] = true; }
    while (g.now() < 2500) g.tick(out);
    for (uint8_t n : chord) { g.live(EV_OFF, n, 0, out); held[n] = false; }
    g.live(EV_DRUM, P_KICK, 100, out);  // at 2500: quantised to step 10 (2400)
    while (g.now() < 3000) g.tick(out);
    CHECK(g.capture(held), "capture while running");
    int ons = 0, offs = 0, drums = 0;
    for (int i = 0; i < g.loop.n; i++) {
        const Ev &e = g.loop.ev[i];
        if (e.type == EV_ON) { ons++; CHECK(std::abs((int)e.tick - 1000) <= 2, "note on at %u", e.tick); }
        if (e.type == EV_OFF) { offs++; CHECK(std::abs((int)e.tick - 2500) <= 2, "note off at %u", e.tick); }
        if (e.type == EV_DRUM) { drums++; CHECK(e.tick == 2400, "captured kick at %u", e.tick); }
    }
    CHECK(ons == 4 && offs == 4 && drums == 1, "captured %d on, %d off, %d drums", ons, offs, drums);
    // Next lap: four ons, four offs, one kick; nothing hangs after Stop.
    std::vector<Hit> lap = run(g, blocks_for_ticks(g, g.loop.len()));
    int on2 = 0, off2 = 0, k2 = 0;
    for (const Hit &h : lap) { on2 += h.type == EV_ON; off2 += h.type == EV_OFF; k2 += h.type == EV_DRUM; }
    CHECK(on2 == 4 && off2 == 4 && k2 == 1, "lap after capture: %d on %d off %d kick", on2, off2, k2);
    // Stopping in the middle of the chord releases it: run into the next
    // lap, past the chord's start.
    const uint32_t lap0 = g.lap_start;
    while (g.lap_start == lap0 || g.now() - g.lap_start < 1500) g.tick(out);
    g.stop(out);
    int released = 0;
    for (int i = 0; i < out.nn; i++) released += out.ntype[i] == EV_OFF;
    CHECK(released == 4, "stop released %d loop notes", released);
}

static void test_loop_insert() {
    Loop l;
    l.init(1);
    const Ev a = {100, EV_DRUM, 0, 100, EVF_GRID}, on = {3000, EV_ON, 60, 90, 0}, off = {3500, EV_OFF, 60, 0, 0};
    l.insert(off); l.insert(a); l.insert(on);
    CHECK(l.ev[0].tick == 100 && l.ev[1].tick == 3000 && l.ev[2].tick == 3500, "insert keeps order");
    const Ev b = {3000, EV_DRUM, 1, 100, EVF_GRID};
    CHECK(l.insert(b) == 2 && l.ev[1].type == EV_ON, "same tick: inserted after the earlier event");
}

static void test_synth() {
    SynthParams sp = {};
    sp.wave = 0.0f; sp.detune = 0.0f; sp.cutoff_oct = 10.0f; sp.reso = 0.0f; sp.env_amt = 0.0f;
    sp.a_a = sp.f_a = 0.001f; sp.a_d = sp.f_d = 0.1f; sp.a_s = sp.f_s = 1.0f; sp.a_r = sp.f_r = 0.05f;
    static Synth s;
    s.init();
    s.note_on(69, 100);
    std::vector<float> y;
    for (int b = 0; b < 3000; b++) {
        float out[kBlock];
        s.render(sp, out);
        for (float v : out) y.push_back(v);
    }
    // A saw at 440 Hz: rising zero crossings in the last half second.
    int zc = 0;
    bool finite = true;
    double peak = 0;
    for (size_t i = 24000; i + 1 < y.size(); i++) {
        if (y[i] <= 0 && y[i + 1] > 0) zc++;
        finite = finite && std::isfinite(y[i]);
        peak = std::fmax(peak, std::fabs(y[i]));
    }
    CHECK(finite, "synth output not finite");
    CHECK(std::abs(zc - 220) <= 2, "%d rising zero crossings in 0.5 s, want 220 (440 Hz)", zc);
    CHECK(peak > 0.05 && peak < 0.3, "one voice peak %g", peak);
    // Five notes on four voices: the oldest goes.
    s.init();
    for (int n = 0; n < 5; n++) s.note_on(60 + n, 100);
    bool has60 = false, has64 = false;
    for (int i = 0; i < kSynthVoices; i++) { has60 = has60 || s.v[i].note == 60; has64 = has64 || s.v[i].note == 64; }
    CHECK(!has60 && has64, "voice stealing: the oldest note goes");
    // Release: silent after the release time; the resonant filter stays bounded.
    sp.reso = 1.0f; sp.cutoff_oct = 6.0f; sp.env_amt = 6.0f;
    s.init();
    for (int n = 0; n < 4; n++) s.note_on(48 + 7 * n, 127);
    double rpeak = 0;
    for (int b = 0; b < 6000; b++) {
        if (b == 3000) s.all_off();
        float out[kBlock];
        s.render(sp, out);
        for (float v : out) { finite = finite && std::isfinite(v); if (b < 3000) rpeak = std::fmax(rpeak, std::fabs(v)); else if (b > 4000) CHECK(std::fabs(v) < 1e-4f, "not silent after release"); }
    }
    CHECK(finite && rpeak < 2.0, "resonant chord peak %g", rpeak);
}

static void test_drums() {
    static int16_t mem[200000];
    Kit &k = kit();
    CHECK(builtin_total() <= 200000, "built-in kit size %d", builtin_total());
    kit_init(k, mem);
    Drums d;
    d.init();
    float gg[NUM_DRUM_GROUPS] = {1, 1, 1, 1};
    d.trigger(k, P_OHAT, 127);
    float dl[kBlock], dr[kBlock], sl[kBlock], sr[kBlock];
    d.render(gg, 0.5f, dl, dr, sl, sr);
    CHECK(d.v[P_OHAT].on, "open hat sounding");
    d.trigger(k, P_CHAT, 100);
    CHECK(!d.v[P_OHAT].on, "closed hat chokes the open hat");
    // Every pad plays its built-in sound and stops by itself.
    for (int p = 0; p < kNumPads; p++) d.trigger(k, p, 100);
    double peak = 0;
    bool finite = true;
    for (int b = 0; b < 3000; b++) {
        d.render(gg, 0.5f, dl, dr, sl, sr);
        for (int i = 0; i < kBlock; i++) {
            finite = finite && std::isfinite(dl[i]) && std::isfinite(dr[i]);
            peak = std::fmax(peak, std::fabs(dl[i]));
            CHECK(std::fabs(sl[i] - 0.5f * dl[i]) < 1e-6f, "send is the send amount of the dry");
        }
    }
    int on = 0;
    for (int p = 0; p < kNumPads; p++) on += d.v[p].on;
    CHECK(finite && on == 0 && peak > 0.3, "drums: %d still on, peak %g", on, peak);
    // Publication flips the slot; a playing voice keeps its copy.
    static int16_t other[100];
    for (int i = 0; i < 100; i++) other[i] = 1000;
    d.trigger(k, P_KICK, 100);
    const int16_t *before = d.v[P_KICK].data;
    const GrainSource s = {other, 100, 1.0f};
    kit_publish(k, P_KICK, s);
    CHECK(d.v[P_KICK].data == before && kit_source(k, P_KICK).data == other, "publish under a playing voice");
}

static void test_ctl() {
    static GrooveCtl c;
    c.init();
    GrooveBus &b = gbus_groove();
    c.midi(0x99, 36, 100);  // bottom-left pad: kit pad 1 (kick)
    c.midi(0x99, 40, 90);   // top-left pad: kit pad 9
    c.tick();
    CHECK(b.out.nd == 2 && b.out.dpad[0] == P_KICK && b.out.dpad[1] == P_KICK2, "pads -> drums: %d", b.out.nd);
    // Upper round button held + pad 13 (broken beat): drums written; tempo,
    // swing and drunk and their knobs follow.
    c.midi(0xBF, 104, 127);
    c.midi(0x99, 48, 100);  // top row, 5th pad (note 48)
    c.midi(0xBF, 104, 0);
    c.tick();
    CHECK(c.g.style == kStyleBrokenBeat && c.g.loop.n > 80, "style 13 written (%d events)", c.g.loop.n);
    CHECK(c.knob[GV_GROOVE][GK_TEMPO] == 91 && c.knob[GV_GROOVE][GK_SWING] == 20 && c.knob[GV_GROOVE][GK_DRUNK] == 20,
          "tempo/swing/drunk knobs %d %d %d", c.knob[GV_GROOVE][GK_TEMPO], c.knob[GV_GROOVE][GK_SWING],
          c.knob[GV_GROOVE][GK_DRUNK]);
    CHECK(std::fabs(c.g.clock.bpm - 120.0f) < 1e-3f, "broken beat at %g BPM", c.g.clock.bpm);
    CHECK(b.out.nd == 0, "a style press does not sound the pad");
    c.midi(0xBF, 104, 127); c.midi(0x99, 48, 100); c.midi(0xBF, 104, 0);
    c.tick();
    CHECK(c.g.style == kStyleBrokenBeat, "variation");
    // Pad 1 (funk): 100 BPM.
    c.midi(0xBF, 104, 127); c.midi(0x99, 36, 100); c.midi(0xBF, 104, 0);
    c.tick();
    CHECK(c.g.style == 0 && std::fabs(c.g.clock.bpm - 100.0f) < 1e-3f, "pad 1: funk at %g BPM", c.g.clock.bpm);
    // Lower round button + kick pad: the kicks erased.
    c.midi(0xBF, 105, 127); c.midi(0x99, 36, 100); c.midi(0xBF, 105, 0);
    c.tick();
    bool kick = false;
    for (int i = 0; i < c.g.loop.n; i++) kick = kick || (c.g.loop.ev[i].type == EV_DRUM && c.g.loop.ev[i].a == P_KICK);
    CHECK(!kick, "erase kick");
    c.midi(0xBF, 77, 127);  // Undo: kicks back
    c.tick();
    kick = false;
    for (int i = 0; i < c.g.loop.n; i++) kick = kick || (c.g.loop.ev[i].type == EV_DRUM && c.g.loop.ev[i].a == P_KICK);
    CHECK(kick, "undo the erase");
    // Transport and loop length.
    c.midi(0xBF, 115, 127); c.tick();
    CHECK(c.g.clock.running, "Play");
    const int n_before = c.g.loop.n;
    c.midi(0xBF, 118, 127); c.tick();
    CHECK(c.g.loop.bars == 4 && c.g.loop.n == n_before, "Loop does nothing: %d bars", c.g.loop.bars);
    c.midi(0xBF, 116, 127); c.tick();
    CHECK(!c.g.clock.running, "Stop");
    // Sustain pedal: a release under the pedal waits for the pedal.
    c.midi(0xB0, 64, 127);
    c.midi(0x90, 60, 100);
    c.midi(0x80, 60, 0);
    c.tick();
    int offs = 0;
    for (int i = 0; i < b.out.nn; i++) offs += b.out.ntype[i] == EV_OFF;
    CHECK(offs == 0, "release held by the pedal");
    c.midi(0xB0, 64, 0);
    c.tick();
    offs = 0;
    for (int i = 0; i < b.out.nn; i++) offs += b.out.ntype[i] == EV_OFF;
    CHECK(offs == 1, "pedal up releases (%d)", offs);
    // Tempo knob: 70 .. 140 BPM.
    CHECK(std::fabs(groove_tempo(0) - 70.0f) < 1e-4f && std::fabs(groove_tempo(127) - 140.0f) < 1e-3f, "tempo range");
    CHECK(std::fabs(groove_tempo(91) - 120.2f) < 0.1f, "init tempo %g", groove_tempo(91));
    CHECK(delay_beats(0) == 0.25f && delay_beats(127) == 2.0f && delay_beats(64) == 1.0f, "delay divisions");
}

int main() {
    test_clock();
    test_swing_drunk();
    test_generator();
    test_playback();
    test_record();
    test_notes_and_capture();
    test_loop_insert();
    test_synth();
    test_drums();
    test_ctl();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("groove: all tests passed\n");
    return 0;
}
