// Host renders of the quartet voice (lib/quartet_voice.h), dry (no Ensemble,
// delay or reverb yet), for comparing by ear with a Jupiter-4. Each scene
// drives up to four voices and the shared modulators directly (the
// controller and the voice assignment are step 3).
//   make render_quartet && ./tests/render_quartet <out_dir>
// One mono 16-bit WAV per scene, normalised to -1 dBFS; the level before
// normalising is printed (peak, rms), since resonance changes the level.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "../lib/quartet_voice.h"

using namespace freak;

static void write_wav(const std::string &path, const std::vector<float> &x) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); exit(1); }
    const uint32_t n = (uint32_t)x.size(), rate = 48000, bytes = n * 2;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(bytes);
    for (uint32_t i = 0; i < n; i++) {
        const float c = x[i] > 1 ? 1 : (x[i] < -1 ? -1 : x[i]);
        const int16_t s = (int16_t)lrintf(c * 32767);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

static QuartetParams base_params() {
    QuartetParams P = QuartetParams();
    P.wave = QW_SAW;
    P.pw = 0.5f;
    P.range = 0;
    P.hp_hz = 10;
    P.lp_oct = 8.6f;  // ~8 kHz at C4
    P.res = 0;
    P.kybd = 0.4f;
    P.a = 0.005f; P.d = 0.5f; P.s = 0.8f; P.r = 0.2f;
    P.lfo_hz = 5;
    P.lfo_wave = QL_SINE;
    P.trig_hz = 8;
    P.level = 1;
    return P;
}

// Four voices, the modulators, a clock in blocks.
struct Rig {
    QuartetMod m;
    QuartetVoice v[4];
    QuartetParams P;
    std::vector<float> out;
    double t = 0;  // seconds rendered

    Rig() {
        m.init();
        for (int i = 0; i < 4; i++) v[i].init((uint32_t)i + 1);
        P = base_params();
    }
    void on(int i, int note) {
        bool any = false;
        for (int j = 0; j < 4; j++) any = any || v[j].gate;
        if (!any) m.restart();  // a fresh key: the LFO delay starts over
        v[i].note = (uint8_t)note;
        v[i].gate = true;
        v[i].trig++;
    }
    void off(int i) { v[i].gate = false; }
    void all_off() { for (int i = 0; i < 4; i++) off(i); }
    // Render for s seconds; ctl(t) may change P before each block.
    void run(double s, const std::function<void(double)> &ctl = nullptr) {
        const int blocks = (int)(s * kSampleRate / kBlock + 0.5);
        float buf[kBlock];
        for (int b = 0; b < blocks; b++) {
            if (ctl) ctl(t);
            m.block(P);
            for (int i = 0; i < kBlock; i++) buf[i] = 0.0f;
            for (int i = 0; i < 4; i++) v[i].render(P, m, buf);
            for (int i = 0; i < kBlock; i++) out.push_back(buf[i]);
            t += (double)kBlock / kSampleRate;
        }
    }
    // play a note on voice i for dur seconds, then rest for gap
    void note(int i, int n, double dur, double gap, const std::function<void(double)> &ctl = nullptr) {
        on(i, n);
        run(dur, ctl);
        off(i);
        run(gap, ctl);
    }
};

static void save(const std::string &dir, const char *name, std::vector<float> x) {
    double pk = 0, ss = 0;
    for (float s : x) { pk = std::fmax(pk, std::fabs(s)); ss += (double)s * s; }
    const double rms = std::sqrt(ss / (double)x.size());
    const float g = pk > 0 ? (float)(0.891 / pk) : 1.0f;
    for (float &s : x) s *= g;
    write_wav(dir + "/" + name, x);
    std::printf("%-28s %5.1f s  peak %.3f  rms %.3f\n", name, x.size() / 48000.0, pk, rms);
}

// cutoff in Hz for lp_oct at note 60
static float oct_for(double hz) { return (float)std::log2(hz / 20.0); }

int main(int argc, char **argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";

    {   // the waveforms at C3: saw, square, pulse 40/20/10 %, PWM, saw + sub, sub alone, noise
        Rig r;
        struct W { int wave; float pw, pwm, sub; bool noise; } ws[] = {
            {QW_SAW, 0.5f, 0, 0, false}, {QW_SQUARE, 0.5f, 0, 0, false},
            {QW_PULSE, 0.4f, 0, 0, false}, {QW_PULSE, 0.2f, 0, 0, false},
            {QW_PULSE, 0.1f, 0, 0, false}, {QW_PWM, 0.5f, 0.8f, 0, false},
            {QW_SAW, 0.5f, 0, kQLvlSub, false}, {QW_OFF, 0.5f, 0, kQLvlSub, false}, {QW_OFF, 0.5f, 0, 0, true}};
        r.P.lfo_hz = 2;
        for (const W &w : ws) {
            r.P.wave = w.wave; r.P.pw = w.pw; r.P.pwm = w.pwm; r.P.sub = w.sub; r.P.noise = w.noise;
            r.note(0, 48, 1.6, 0.3);
        }
        save(dir, "01_waves.wav", r.out);
    }
    {   // a cutoff sweep, 30 Hz - 15 kHz and back over 8 s, saw + sub at C2, RESONANCE 0.5
        Rig r;
        r.P.sub = kQLvlSub; r.P.res = 0.5f; r.P.kybd = 0.1f; r.P.s = 1;
        r.on(0, 36);
        r.run(8.0, [&](double t) { const double u = t < 4 ? t / 4 : (8 - t) / 4; r.P.lp_oct = oct_for(30 * std::pow(500.0, u)); });
        r.off(0);
        r.run(0.5);
        save(dir, "02_sweep_res_half.wav", r.out);
    }
    {   // the same sweep at RESONANCE 1: self-oscillation over the saw
        Rig r;
        r.P.res = 1.0f; r.P.kybd = 0.1f; r.P.s = 1;
        r.on(0, 36);
        r.run(8.0, [&](double t) { const double u = t < 4 ? t / 4 : (8 - t) / 4; r.P.lp_oct = oct_for(30 * std::pow(500.0, u)); });
        r.off(0);
        r.run(0.5);
        save(dir, "03_sweep_selfosc.wav", r.out);
    }
    {   // RESONANCE in steps at 800 Hz: 0, .25, .5, .75, .87 (threshold), .95, 1 (bass thins as it rises)
        Rig r;
        r.P.sub = kQLvlSub; r.P.kybd = 0.1f; r.P.s = 1; r.P.lp_oct = oct_for(800);
        r.on(0, 36);
        for (float q : {0.0f, 0.25f, 0.5f, 0.75f, 0.87f, 0.95f, 1.0f}) r.run(1.2, [&](double) { r.P.res = q; });
        r.off(0);
        r.run(0.5);
        save(dir, "04_resonance_steps.wav", r.out);
    }
    {   // the S&H on the cutoff: 6 Hz, then 12 Hz; saw + sub at C2, RESONANCE 0.8
        Rig r;
        r.P.sub = kQLvlSub; r.P.res = 0.8f; r.P.kybd = 0.1f; r.P.s = 1;
        r.P.lp_oct = oct_for(600); r.P.sh_vcf = 2.5f; r.P.trig_hz = 6;
        r.on(0, 36);
        r.run(4.0);
        r.P.trig_hz = 12;
        r.run(4.0);
        r.off(0);
        r.run(0.5);
        save(dir, "05_sample_and_hold.wav", r.out);
    }
    {   // a plucked bass: 16', the envelope 5 octaves on the cutoff, RESONANCE 0.6
        Rig r;
        r.P.range = -1; r.P.sub = kQLvlSub; r.P.res = 0.6f; r.P.lp_oct = oct_for(150); r.P.kybd = 0.4f;
        r.P.env_vcf = 5; r.P.a = 0; r.P.d = 0.25f; r.P.s = 0; r.P.r = 0.2f;
        const int line[] = {36, 36, 48, 36, 39, 41, 43, 46, 36, 36, 48, 36, 34, 34, 46, 34};
        int i = 0;
        for (int n : line) r.note(i++ % 4, n, 0.16, 0.09);
        r.run(0.5);
        save(dir, "06_pluck_bass.wav", r.out);
    }
    {   // a chord with a filter swell: C3 E3 G3 B3, then F3 A3 C4 E4
        Rig r;
        r.P.res = 0.25f; r.P.lp_oct = oct_for(900); r.P.env_vcf = 3;
        r.P.a = 0.08f; r.P.d = 0.6f; r.P.s = 0.7f; r.P.r = 0.5f;
        const int c1[] = {48, 52, 55, 59}, c2[] = {53, 57, 60, 64};
        for (int i = 0; i < 4; i++) r.on(i, c1[i]);
        r.run(1.8);
        r.all_off();
        r.run(0.15);
        for (int i = 0; i < 4; i++) r.on(i, c2[i]);
        r.run(1.8);
        r.all_off();
        r.run(1.0);
        save(dir, "07_chord_swell.wav", r.out);
    }
    {   // PWM chord, slow attack and release (the Ensemble comes in step 4)
        Rig r;
        r.P.wave = QW_PWM; r.P.pwm = 0.7f; r.P.lfo_hz = 0.7f;
        r.P.res = 0.1f; r.P.lp_oct = oct_for(3500);
        r.P.a = 0.6f; r.P.d = 1.0f; r.P.s = 0.9f; r.P.r = 1.2f;
        const int c[] = {45, 52, 57, 60};
        for (int i = 0; i < 4; i++) r.on(i, c[i]);
        r.run(3.5);
        r.all_off();
        r.run(1.8);
        save(dir, "08_pwm_chord.wav", r.out);
    }
    {   // unison lead: four voices on one note, detuned -9, -3, +3, +9 cents,
        // glide, delayed vibrato
        Rig r;
        r.P.res = 0.45f; r.P.lp_oct = oct_for(700); r.P.env_vcf = 2.5f; r.P.kybd = 0.7f;
        r.P.a = 0.01f; r.P.d = 0.4f; r.P.s = 0.6f; r.P.r = 0.3f;
        r.P.glide = 0.12f; r.P.vco_lfo = 0.25f; r.P.lfo_hz = 5.5f; r.P.lfo_delay = 0.6f;
        const float det[] = {-0.09f, -0.03f, 0.03f, 0.09f};
        for (int i = 0; i < 4; i++) r.v[i].detune = det[i];
        const int mel[] = {60, 63, 67, 70, 72, 70, 67, 65};
        const double dur[] = {0.3, 0.3, 0.3, 0.3, 1.4, 0.3, 0.3, 1.6};
        for (int k = 0; k < 8; k++) {
            for (int i = 0; i < 4; i++) r.on(i, mel[k]);
            r.run(dur[k] - 0.02);
            if (k == 4) r.all_off();  // a breath after the long note; elsewhere the next key retriggers
            r.run(0.02);
        }
        r.all_off();
        r.run(0.8);
        save(dir, "09_unison_lead.wav", r.out);
    }
    {   // the self-oscillating filter played from the keys: sources off,
        // RESONANCE 1, keyboard follow 100 % (it oscillates at the cutoff)
        Rig r;
        r.P.wave = QW_OFF; r.P.res = 1.0f; r.P.kybd = 1.0f;
        r.P.lp_oct = oct_for(261.63);  // C4 sounds at C4
        r.P.a = 0.005f; r.P.d = 0.5f; r.P.s = 1; r.P.r = 0.4f;
        const int mel[] = {60, 64, 67, 72, 67, 64, 60, 55};
        int i = 0;
        for (int n : mel) r.note(i++ % 4, n, 0.45, 0.1);
        r.run(0.6);
        save(dir, "10_selfosc_keys.wav", r.out);
    }
    {   // VCA GATE with the envelope inverted on the cutoff: stabs that open as they end
        Rig r;
        r.P.vca_gate = true; r.P.env_inv = true; r.P.env_vcf = 3;
        r.P.res = 0.5f; r.P.lp_oct = oct_for(2500);
        r.P.a = 0.002f; r.P.d = 0.35f; r.P.s = 0; r.P.r = 0.1f;
        const int c[] = {48, 55, 58, 63};
        for (int k = 0; k < 6; k++) {
            for (int i = 0; i < 4; i++) r.on(i, c[i] + (k % 2) * 2);
            r.run(0.3);
            r.all_off();
            r.run(0.2);
        }
        r.run(0.5);
        save(dir, "11_gate_inverted_env.wav", r.out);
    }
    return 0;
}
