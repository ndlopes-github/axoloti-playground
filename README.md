# axoloti-playground

A standalone synth on an original **Axoloti Core**, played from a **Novation
Launchkey 49 MK3** plugged into the board's USB host port. The aim is jazzy
chords, neo-soul, nu-jazz and broken beat, with "strange" Freak-style digital
oscillators and effects.

Status: **`patches/freak/freak_v1.axp`** is the current synth. It has 3
voices, each a Braids oscillator (34 shapes) into a state-variable filter
with an amp envelope, a mod envelope and glide. One LFO is shared by the
voices, followed by a delay and the Elements reverb. Every control of the
Launchkey is mapped: two knob pages, faders, fader buttons, pads as shape
banks, and transport buttons. See the cheat sheet,
[`docs/cheatsheet.pdf`](docs/cheatsheet.pdf). `freak_v0.axp` is the first,
simpler version.

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
| `freak_v1` (3 voices with filter and envelopes + delay + reverb) | 52 % | 41.2 KB (91 %) |
| Elements reverb / diffuser / string / tube | 19.5 / 7.5 / 5 / 4.5 % | ~0 (code in firmware) |
| Warps | 46 % | ~0 |
| Clouds | 79–103 % | ~0, too heavy next to voices |

## Notes and pitfalls

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
