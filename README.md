# axoloti-playground

A standalone Freak-style synth on an original **Axoloti Core**, played from a
**Novation Launchkey 49 MK3** plugged into the board's USB host port. It has a
multi-engine digital oscillator (Braids, 34 shapes), a filter, envelopes, an
LFO and effects, with every control on the Launchkey.

Status: **`patches/freak/freak_v1.axp`** is the current synth. It has 3
voices, each a Braids oscillator (34 shapes) into a state-variable filter
with an amp envelope, a mod envelope and glide. One LFO is shared by the
voices, followed by a delay and the Elements reverb. Every control of the
Launchkey is mapped: two knob pages, faders, fader buttons, pads as shape
banks, and transport buttons. See the cheat sheet,
[`docs/cheatsheet.pdf`](docs/cheatsheet.pdf). `freak_v0.axp` is the first,
simpler version.

**`patches/grains/grains_v1.axp`** is the second patch, meant for the SD card:
the board starts it when a card is in, and freak (in flash) when none is. It
is a mono granular synth after the MicroFreak's grain engines. Up to 20
grains (SCAN, CLOUD or HIT) play from a source in SDRAM, into a stereo filter
and amp with the same envelopes, LFO and effects as freak. It keeps freak's
control layout and adds a GRAIN knob page. Sources are a built-in one plus
the WAV files in `/grains` on the card. A starter bank of 8, rendered with
freak's own oscillators, is in `objects/grains_wav/` and is copied to the card
with the patch. See [`docs/grains_cheatsheet.pdf`](docs/grains_cheatsheet.pdf).

**`patches/groove/groove_v1.axp`** is the third patch, on a card of its own:
a real-time looper for drums and chords. The pads play a sampled kit of 16
dry drum-machine sounds (TR-808, 909, 707, 727; CC0, in `objects/kit_bb/`).
The keys play a 4-voice synth after the Juno and the Prophet, recorded as
played. The transport buttons run the looper: record, overdub, capture,
undo, quantise and click, over a loop of 4 bars. Drums can also be written
in 16 styles with variations (four each of funk, neo-soul, boom-bap and
broken beat, in apc-chord's notation); a style sets the tempo, swing and
drunk too. See [`docs/groove_cheatsheet.pdf`](docs/groove_cheatsheet.pdf).

**`patches/soliton/soliton_v1.axp`** is the fourth patch: 3 voices whose
oscillator is a wave field on a ring that evolves under a nonlinear PDE.
One period of the field is one cycle of the waveform, played at the note's
pitch, and the field's time moves the timbre. There are four engines:
Burgers (SHOCK: a sine steepens into a saw and decays), KdV (SOLITON: a
wave breaks into solitons), Kuramoto–Sivashinsky (CHAOS: drifting cells,
from order to chaos), and focusing NLS (ROGUE: a wave group whose sidebands
grow, focus and recur). Velocity sets the field's size, so it changes the
evolution. Pads freeze, rewind (KdV and NLS only), speed up or kick the
field. Filter, envelopes, LFO and effects are as in freak, with freak's
layout plus a FIELD knob page. See
[`docs/soliton_cheatsheet.pdf`](docs/soliton_cheatsheet.pdf) and the
numerics in `lib/soliton_field.h`. On the SD card with the others.

**`patches/aeolian/aeolian_v1.axp`** is the fifth patch: a wind harp. Eight
waveguide strings are never played directly; a wind blows across them. Each
string sheds vortices at the Strouhal frequency 0.2 U/d, modelled as a van der
Pol wake oscillator coupled to the string (vortex-induced vibration); near one
of the string's harmonics the wake locks in and that harmonic sings. The wind
is steady, gusty (Ornstein–Uhlenbeck) or chaotic (Lorenz-63). The keys tune
the harp (unison, harmonic series, fifths, chord), the faders set the eight
string diameters, the pads are gusts, calm, storm, pluck and mute. See
[`docs/aeolian_cheatsheet.pdf`](docs/aeolian_cheatsheet.pdf) and the model in
`lib/aeolian_harp.h`. On the SD card with the others.

**`patches/raw/raw_v1.axp`** is the sixth patch: a monosynth after the Korg
MS-20. VCO1 (triangle, saw, pulse with width, noise) and VCO2 (saw, square,
narrow pulse, ring) go through a drive stage into a resonant high-pass and a
resonant low-pass, then the VCA. Both filters are 2-pole Sallen-Key filters
after the Korg-35, with clipping in the feedback loop, solved without a unit
delay; they self-oscillate from 91 % of PEAK. The voice runs at 96 kHz and
is decimated to 48 kHz. An MG (ramp, triangle or sample and hold), EG1
(delay, attack, release) and EG2 (hold, ADSR) modulate pitch and both cutoffs.
Mono keys with last-note priority, legato and hold. Three knob pages (SOUND,
MOD, FX), the faders are the two envelopes, the pads are presets and
gestures (scream, open, dive, S&H). See
[`docs/raw_cheatsheet.pdf`](docs/raw_cheatsheet.pdf) and the filter
derivation in `lib/raw_voice.h`. On the SD card with the others.

**`patches/quartet/quartet_v1.axp`** is the seventh patch, and the startup
patch on the card: four voices after the Roland Jupiter-4. Each voice is a VCO
(saw, square, pulse in four widths, PWM) with a sub-oscillator (four levels)
and noise, a 1-pole high-pass and a 4-pole low-pass whose resonance is limited
as the Jupiter-4's diodes do: it self-oscillates at the cutoff, in tune with
the keys. One ADSR per voice drives the cutoff (either polarity) and the VCA,
or the VCA follows the gate. One LFO (sine, square, saw, inverted saw, with a
delay), a sample-and-hold of noise on the cutoff, and the Ensemble (a stereo
chorus). POLY or UNISON (four voices on the lowest key, detuned), hold,
portamento, eight presets on the top pads. No arpeggiator (the Launchkey has
one) and no delay or reverb (an external pedal). See
[`docs/quartet_cheatsheet.pdf`](docs/quartet_cheatsheet.pdf) and the filter
analysis in `lib/quartet_voice.h`.

## Switching patches (one SD card)

Every patch carries `objects/patch_switch.axo`. To change patch, **hold both
buttons left of the pads and tap a top pad**: pad 1 freak, 2 grains,
3 groove, 4 soliton, 5 aeolian, 6 raw, 7 quartet. On their own those buttons only switch
the knob page, and the switch is armed only while they are held, so a page
change followed by a preset never loads a patch. (Shift + a key was tried
first: in standalone mode Shift sends no MIDI, and while it is held the keys
send nothing.) The switcher loads
`/<name>_v1/patch.bin` from the card with the firmware's `LoadPatch` (no
firmware change). A patch that is not on the card is logged and ignored.

To install: open each of the seven patches in the patcher and use *Upload to
SD Card*. That writes `/<name>_v1/patch.bin` and the samples the patch needs
(`/grains`, `/kit`) and touches nothing else. Then use *Upload to SD Card as
Startup* on the one that should start when the card is in (`/start.bin`).
Without a card, the board starts freak from flash, as before.

## Hardware and software

- Axoloti Core (original board), running **Ksoloti Patcher 1.1.0** in Firmware
  Mode *Axoloti Core*. Version 1.1.0 is needed because it adds the 64 KB SRAM3
  patch region; 1.0.12 gives patches 44 + 8 KB only.
- Launchkey 49 MK3 on the Axoloti's USB host port; the board powers it. The
  MIDI map, recorded from this unit, is in
  [`docs/launchkey49mk3_map.md`](docs/launchkey49mk3_map.md).

### Setup on a new machine

1. Install the Ksoloti Patcher and run `platform_linux_x64/add_udev_rules.sh`.
   Set *File → Preferences → Firmware Mode* to **Axoloti Core**, then restart.
2. Register this repository as a library. Either use *Preferences → Libraries →
   Add*, type local, or add this to `ksoloti.prefs` while the patcher is closed:
   ```xml
   <filelib>
     <Id>axoloti-playground</Id>
     <Type>local</Type>
     <LocalLocation>/path/to/axoloti-playground</LocalLocation>
     <Enabled>true</Enabled>
   </filelib>
   ```
   Patches refer to `braids_osc` by name. Relative object paths do not resolve
   inside embedded subpatches.
3. Flashing the 1.1.0 firmware over 1.0.12 needs Rescue Mode (DFU). If S1 is
   hard to reach, see `tools/axo_dfu` below.

## Contents

| Path | What |
|---|---|
| `lib/launchkey_ctl.h` | The whole Launchkey mapping: pages, soft takeover, toggles, pads, tap tempo, LFO. It publishes everything on a control bus. Single source of truth for the cheat sheet. |
| `lib/freak_dsp.h` | Envelopes, TPT state-variable filter, LFO, glide, delay, and fast exp2/sin/tan (no libm). Error bounds are in the comments and checked by the tests. |
| `lib/grains.h` | The granular engine: a fixed grain pool, 32.32 fixed-point positions, the window, and the fast and careful read loops. Correctness arguments are in the comments. |
| `lib/grains_ctl.h`, `lib/grains_voice.h` | The grains mapping (mono keys, three knob pages, pads, modes, presets) and its voice. The mapping is the single source of truth for the grains cheat sheet. |
| `lib/wav.h`, `lib/grains_bank.h`, `lib/grains_loader.h` | WAV parsing and conversion, the source bank with its built-in source, and the board-side loader thread (FatFs, sector-aligned reads). |
| `objects/grains_*.axo`, `fx_delay_st.axo` | The grains objects (control hub, bank, voice) and a stereo delay. |
| `tests/test_grains*.cpp`, `tests/render_grains.cpp`, `tests/render_bank.cpp` | Host tests for the engine, loader, controller and voice; a preset renderer; the starter-bank renderer (`make bank`). |
| `lib/groove_seq.h` | The groove looper: 960-PPQ clock, the 4-bar loop (record, skip-once, undo/redo, capture), swing and drunk, and the 16 drum styles (funk, neo-soul, boom-bap, broken beat) with apc-chord's variation rules. |
| `lib/groove_synth.h`, `lib/groove_drums.h`, `lib/groove_mix.h`, `lib/groove_ctl.h` | The 4-voice synth (PolyBLEP oscillators, zero-delay-feedback ladder, Juno chorus), the drum sampler and its kit, the mixer (delay, Elements reverb, click), and the Launchkey mapping. |
| `lib/sd_files.h`, `lib/groove_kit_loader.h` | SD-card file helpers shared by the grains and groove loaders, and the kit loader (`/kit/NN_name.wav`). |
| `objects/groove_*.axo`, `objects/kit_bb/` | The groove objects (hub, kit, drums, synth, mix) and the drum kit with its credits. |
| `tools/make_kit.sh`, `tests/test_groove.cpp`, `tests/render_groove.cpp` | The kit builder (`make kit`, from pinned CC0 sources), the groove tests, and a host render of a recorded broken-beat loop (`make render_groove`). |
| `lib/soliton_field.h` | The soliton oscillator: the four PDEs, Fourier pseudo-spectral (128 points, 42 modes), Strang splitting with RK4 (Cayley rotation for NLS), the step limits, a step cut into 12 work items, the band-limited playback table. Numerics and their justification are in the comments. |
| `lib/pde_fft.h`, `third_party/cmsis_fft/` | Real and complex FFTs for the solver: CMSIS-DSP 1.6's radix-8 FFT (Apache-2.0, vendored, split so one FFT spreads over ticks), the real-transform splits, accurate twiddles and `sincos_unit`. |
| `lib/soliton_ctl.h`, `lib/soliton_voice.h`, `objects/soliton_*.axo` | The soliton mapping (three knob pages, engines, time pads, presets), its voice (field, SVF, envelopes) and objects. The mapping is the single source of truth for the soliton cheat sheet. |
| `tests/test_soliton.cpp`, `tests/render_soliton.cpp` | Tests against exact results (FFT vs DFT, KdV energy, Burgers slope vs characteristics, KS and NLS growth rates vs linear theory, resolution over the knob range, rewind, mapping, voice bounds) and a preset renderer (`make render_soliton`), whose `probe` mode prints what `objects/dev/probe.axo` logs on the board. |
| `objects/dev/soliton_cycles*.axo`, `patches/bench_soliton/` | Cycle counts of the solver's pieces on the board (DWT counter) and the DSP-load benchmarks. |
| `lib/aeolian_harp.h`, `lib/aeolian_ctl.h`, `objects/aeolian_*.axo` | The wind harp: waveguide strings tuned exactly (the loss filter's phase delay compensated), van der Pol wakes at the Strouhal frequency with lock-in, OU and Lorenz wind, sound box and level control; its Launchkey mapping (the single source of truth for the aeolian cheat sheet) and objects. |
| `tests/test_aeolian.cpp`, `tests/render_aeolian.cpp`, `objects/dev/aeolian_cycles.axo` | Tests against exact results (tuning, ring time, the van der Pol limit cycle at the Strouhal frequency, lock-in and its hysteresis, the gusts' statistics, bounds, mapping), a preset renderer (`make render_aeolian`, with a `probe` mode), and cycle counts on the board. |
| `lib/raw_voice.h`, `lib/raw_ctl.h`, `objects/raw_*.axo` | The raw monosynth: band-limited oscillators (PolyBLEP, PolyBLAMP), the Korg-35-style filters (zero-delay feedback, a scalar Newton solve per sample, its error bound and stability argument in the comments), the half-band decimator, the MS-20 envelopes; its Launchkey mapping (the single source of truth for the raw cheat sheet) and objects. |
| `tests/test_raw.cpp`, `tests/render_raw.cpp`, `objects/dev/raw_cycles.axo`, `patches/bench_raw/` | Tests against exact results (the filters against the bilinear transform, the self-oscillation threshold and amplitude, the solve, the decimator's bands, aliasing, envelope timing, the mono key logic, the mapping), a preset renderer (`make render_raw`, with a `probe` mode), cycle counts on the board and the DSP-load benchmarks. |
| `lib/quartet_voice.h`, `lib/quartet_ctl.h`, `objects/quartet_*.axo` | The quartet voices: 4-point BLEP oscillators and the flip-flop sub, the 4-pole low-pass (linear zero-delay stages, the diode-like limiter solved per sample with exactly one root, its stability argument; and the full OTA model with saturating stages kept as a host reference), ADSR and gate, LFO, S&H, the Ensemble; its Launchkey mapping (the single source of truth for the quartet cheat sheet), voice assignment, presets and objects. |
| `tests/test_quartet.cpp`, `tests/render_quartet.cpp`, `objects/dev/quartet_cycles.axo`, `patches/bench_quartet/` | Tests against exact results (the low-pass against the bilinear ladder, the threshold k = 4, self-oscillation at the cutoff, the solve, the full model against a double-precision implicit solve, aliasing, envelope timing, the Ensemble, key assignment, presets, pickup), a scene renderer (`make render_quartet`), cycle counts on the board and the DSP-load benchmarks. |
| `lib/patch_switch.h`, `objects/patch_switch.axo`, `tests/test_switch.cpp` | The patch switcher (in every patch): hold both buttons left of the pads + a top pad, tested on the host; the object checks the card and calls the firmware's `LoadPatch` from k-rate code. |
| `objects/dev/sd_list.axo`, `load_test.axo`, `patches/dev/switch_test.axp` | Read-only card listing, a load-by-file-name test, and the switcher alone (send it the gesture over USB MIDI with `amidi` during `-runTest`). |
| `tests/test_freak.cpp` | Host unit tests (`make test`, with ASan/UBSan). |
| `objects/launchkey_ctl.axo`, `freak_voice.axo`, `fx_delay.axo` | The synth's objects: control hub, one voice, delay. They talk through the control bus, not through wires. |
| `objects/braids_osc.axo` | Stand-alone Braids digital macro-oscillator, with inlets: pitch, trig (strike), timbre, colour, shape and `shapecv`. |
| `third_party/braids/` | Upstream Braids `digital_oscillator.cc` (MIT), adapted to Ksoloti's firmware headers. The changes are listed in its README. |
| `patches/freak/freak_v1.axp` | The synth. `freak_v0.axp` is the first version (knob/fader map in its notes). |
| `docs/cheatsheet.tex`, `.pdf` | One-page A4 card for playing freak v1 (`make cheatsheet`). |
| `patches/bench_*` | Silent DSP-load benchmarks. |
| `patches/dev/midi_monitor.axp`, `objects/dev/` | Forwards the USB-host controller's MIDI to the laptop, for mapping controllers. |
| `tools/axo_dfu.c` | Puts the board into Rescue Mode (DFU) over USB, without pressing S1. |
| `docs/` | Controller map. |

## Measurements (on the board, Ksoloti 1.1.0)

Measured with `Ksoloti -runTest <patch.axp | dir>`, which compiles, uploads,
runs for 1 s and logs the DSP load. The GUI patcher must be closed.

| Patch | DSP load | Patch RAM (SRAM1) |
|---|---|---|
| empty | 1 % | 0.6 KB |
| 1 Braids voice | 2.5–8.5 %, depending on shape (worst: harmonics) | 25 KB |
| 3 Braids voices, worst shape | 23 % | 25.8 KB, code shared |
| `freak_v0` (3 voices + Elements reverb) | 33 % | 31 KB |
| `freak_v1` (3 voices with filter and envelopes + delay + reverb) | 58.5 % | 42.9 KB (95 %, Braids at -Os) |
| `grains_v1`, idle / all 20 grains (stereo delay + reverb) | 41 % / 75.5 % | 28.2 KB (controller at -Os) |
| grain engine, per live grain | 1.76 % | |
| `groove_v1`, idle / beat + 4-voice chord (delay, reverb, chorus) | 46.5 % / 80.5 % | 32.1 KB (controller and sequencer at -Os) |
| groove kit load from SD (16 files, 946 KB, background thread) | 0.45 s (2.1 MB/s) | |
| `soliton_v1`, idle (delay + reverb) | 37.5–38 % | 36.4 KB |
| 3 soliton voices + delay + reverb, all sounding (`bench_soliton`) | 61.5–68.5 % (89–90.5 % before the voices took turns) | 35.9 KB |
| soliton field, one work item (cycles, worst of ~1300, interrupts included) | 4,600–12,900 cycles | |
| 128-point real FFT / complex FFT (CMSIS radix 8, firmware bit reversal) | 5,700 / 9,300 cycles (radix 2 before: 14,000 / 22,000) | |
| `aeolian_v1` (8 strings, stereo delay, reverb), constant | 75.5 % (104.5 % before the string loop was rewritten) | 20.8 KB |
| `raw_v1` (mono voice at 96 kHz, mono delay, reverb), constant | 70.5–79.5 % (by oscillator wave; most with VCO2 on ring) | 22 KB |
| `quartet_v1`, idle / four voices / every source, full resonance, Ensemble | 8.5 % / 64.5 % / 72 % | 18.7 KB |
| quartet low-pass, per sample (board) | 178 cycles (the full OTA model with saturating stages: 641; a voice 449 cycles) | |
| aeolian string, per sample (board) | ~150 cycles (265 at first: a division, int/float conversions and member reloads per sample) | |
| grains bank load from SD (8 files, 3000 KB, background thread) | 1.06 s (2.8 MB/s) | |
| Elements reverb / diffuser / string / tube | 19.5 / 7.5 / 5 / 4.5 % | ~0 (code in firmware) |
| Warps | 46 % | ~0 |
| Clouds | 79–103 % | ~0, too heavy next to voices |

## Notes and pitfalls

- **No SD card access, and no logging, in a MIDI handler.** The MIDI handlers
  run in firmware threads with 256-byte stacks (USB device MIDI, serial
  MIDI), and FatFs puts a 512-byte long-file-name buffer on the caller's
  stack (`_USE_LFN 2`). An `f_stat` in `patch_switch`'s first MIDI handler
  hung the board. The handler now only records the request; the k-rate code
  (DSP thread, 7.3 KB of stack) does the card access and `LoadPatch`.
- **Objects used in polyphonic subpatches must be trivially destructible.** A
  member with a user-provided destructor made the generated voice array
  register its destructor with newlib's `atexit()`, and that patch hung the
  board at start. `braids_osc` therefore builds its oscillator with placement
  new into raw storage.
- Braids' tables assume 96 kHz. The object transposes up an octave, so pitch is
  right at 48 kHz. Time constants that do not depend on pitch (envelopes, grain
  rates) run half as fast.
- The full Elements object (`fx/lmnts`) needs sample data on the SD card. Run
  without it, it hangs the board, and a USB replug recovers.
- **Two buffer overruns in Ksoloti 1.1.0's Mutable Instruments code** (found
  here, not yet reported upstream):
  - The factory `fx/lmnts/reverb` allocates 32768 *bytes*, but its engine
    `FxEngine<32768, FORMAT_16_BIT>` uses 32768 *16-bit words* and zero-fills
    them. So it writes 32 KB into whatever was allocated next. freak v1's
    delay line sat there, and that turned the output into noise.
    `objects/fx_reverb.axo` allocates the full 64 KB.
  - `braids/digital_oscillator.h`'s `Init()` allocates 8200 bytes for delay
    lines that need 16384, because of the comb shape. AddressSanitizer
    reports it on the host. `lib/braids_init.h` reserves the rest
    contiguously; the SDRAM allocator is a bump allocator.
  The fx/lmnts object family has the same 32 KB/64 KB pattern
  (`ELEMENTS_SZ = 32768` for the full Elements part).
- **Checking a patch on the board without listening:** `objects/dev/probe.axo`
  logs RMS, peak, zero-crossing rate and saturated-sample counts over 0.9 s
  to the patcher log. `patches/dev/chain/` probes every stage of the chain;
  run it with `-runTest`.
- **Reading files from the SD card in a patch** (grains_loader.h):
  - The firmware builds the SD driver without unaligned-buffer support. Every
    read of a sector or more must start at a 512-byte file offset and land in
    a 4-byte-aligned buffer.
  - `sdram_malloc` returns the SDRAM *base* when it runs out, not null, so check
    `sdram_get_free()` first. It is not thread-safe either: a loader thread
    must wait until every object's init has run.
  - `-runTest` does not copy an object's `file-depends` to the card. Going Live
    in the GUI or *Upload to SD Card* does.
- **Checking the voice on the host:** `make render`, then
  `./tests/render_freak out.wav <shape> <note>` renders the same voice code
  with the real Braids oscillator, under AddressSanitizer, and prints level
  and fundamental.

## `tools/axo_dfu`

```bash
cc -O2 -Wall -o tools/axo_dfu tools/axo_dfu.c -l:libusb-1.0.so.0
tools/axo_dfu      # 1.0.12-era firmware ("AxoD")
tools/axo_dfu -n   # 1.1.0 firmware ("Axou")
```

Close the patcher first. Then replug the board and run the tool within a few
seconds, because the firmware's command parser can be left mid-command by a
previous host. The board then shows up as `0483:df11`; flash it with *Board →
Firmware → Flash (Rescue)*.

## Licence

The code in this repository is under the MIT licence (`LICENSE`).
`third_party/braids/` is © Emilie Gillet, MIT, with its notices kept. It
depends on the Ksoloti firmware and objects (GPL), which are not included here.
