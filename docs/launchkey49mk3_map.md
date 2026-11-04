# Launchkey 49 MK3 → Axoloti USB host: MIDI map

Recorded 2026-11-04. The Launchkey was plugged into the Axoloti's USB host
port, with `patches/dev/midi_monitor.axp` live forwarding to the laptop, and
logged there with `aseqdump -p "Axoloti Core"`. The Launchkey was in its
default standalone mode. All messages came from host port 1, the MIDI port.
The DAW port (port 2), used to light the pads, sent nothing in standalone mode,
and whether it is reachable through the Axoloti host is still unknown.

Channels are 1-based.

| Control | Message | Notes |
|---|---|---|
| Keys (49) | ch 1 note on/off 36–84, velocity | at the default octave |
| Knobs 1–8 | ch 1 CC 21–28 | observed in order |
| Faders 1–9 | ch 1 CC 71–79 | nine consecutive CCs; fader *i* = CC 70+*i* and master = CC 79 is inferred, not checked one by one |
| Fader buttons 1–9 | ch 1 CC 11–19, 127 = press, 0 = release | observed in order |
| Pads, top row L→R | ch 10 notes 40 41 42 43 48 49 50 51 | velocity + polyphonic aftertouch |
| Pads, bottom row L→R | ch 10 notes 36 37 38 39 44 45 46 47 | velocity + polyphonic aftertouch |
| Pitch wheel | ch 1 pitch bend −8192..8191 | |
| Mod wheel | ch 1 CC 1 | |
| Transport, top row | ch 16 CC 74 75 76 77 (127/0) | names (Capture MIDI, Quantise, Click, Undo) are from memory of the panel |
| Transport, bottom row | ch 16 CC 115 116 117 118 (127/0) | likely Play, Stop, Record, Loop (from memory) |
| Track ◄ ► | ch 16 CC 102 / 103 | |
| Buttons left of pads (upper, lower) | ch 16 CC 106 / 107 | 107 observed |
| Round buttons right of pads (upper, lower) | ch 16 CC 104 / 105 | not observed |
| Arrows right of knobs (up, down) | ch 16 CC 51 / 52 | not observed |
| ? | ch 1 program change, incrementing 1 → 66 | some button, not yet identified |

Button numbers come from Novation's *Launchkey [MK3] Programmer's Reference Guide* v1,
p. 5 (standalone mode: every button sends a CC on ch 16 on the MIDI port), and
they agree with every button observed. The pad LEDs can only be set in **DAW
mode**:
- Send `9F 0C 7F` to the **DAW port** (USB interface 2) to enter it, `9F 0C 00`
  to leave.
- `BF 03 01` selects the Drum pad layout. Drum pads then stay on the MIDI port.
- Pad colour: note 36–51 on ch 10 (stationary), ch 11 (flash) or ch 12 (pulse),
  sent to the DAW port; the velocity selects the palette entry.
- In DAW mode the pots and faders switch to DAW functions on the DAW port.
  `BF 09 00` / `BF 0A 00` put them back in Custom Mode 0.
- Not yet tested through the Axoloti host.
