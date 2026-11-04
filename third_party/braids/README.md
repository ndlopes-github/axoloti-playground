# Braids digital oscillator (Mutable Instruments, MIT)

Fetched from <https://github.com/pichenettes/eurorack>, at commit
`6165129cd99416c762da3087594d9148e4c29222`. Ksoloti's firmware copy of
Mutable's code is based on this commit (`firmware/mutable_instruments/README.md`).
Building at that commit means these sources match the Braids headers, lookup
tables and stmlib that are already in the firmware.

Only `digital_oscillator.cc` is compiled (by `objects/braids_osc.axo`).
- Its `#include "braids/..."` and `"stmlib/..."` lines resolve to the
  firmware's copies (`firmware/mutable_instruments/`).
- The lookup tables are linked from the firmware.
- The other files here are unmodified upstream copies, kept for reference.

Changes to `digital_oscillator.cc`:
1. `delay_lines_.x` became `delay_lines_->x` (12 places). Ksoloti's header
   moved the 8 KB of delay lines into SDRAM, behind a pointer.
2. When the shape changes, `Render()` resets the state inline instead of
   calling `Init()`. Ksoloti's `Init()` allocates the delay lines, so calling
   it there leaked 8 KB of SDRAM per shape change.

Notes:
- The `DigitalOscillatorShape` names for indices 15–23 do not match the render
  table. The table follows the macro-oscillator order (digital index =
  macro shape − 11): plucked, bowed, blown, fluted, bell, drum, kick, cymbal,
  snare. Shapes are indexed by table position.
- The lookup tables assume 96 kHz. At the Axoloti's 48 kHz the object
  transposes up an octave, so pitch is right. Time constants that do not
  depend on pitch (envelopes, grain rates, filter cutoffs) run 2× slower.
