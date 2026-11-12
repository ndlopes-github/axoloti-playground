# kit_bb: drum kit of the groove patch

16 one-shots, 48 kHz mono 16-bit, built by `tools/make_kit.sh` (`make kit`)
from these sources at pinned commits. All are drum machines, so the kit is
dry (no room sound). All sources are **CC0 1.0** (public domain dedication):
no attribution is required, but here it is.

| Pad file | Sound | Source |
|---|---|---|
| 01_kick | 909 bass drum (Roland TR-8) | MckSamplePacks (MckAudio): `TR8/BD/006_909_Bass_Drum_3.wav` |
| 02_snare | 909 snare | MckSamplePacks: `TR8/SD/006_909_Snare_Drum_2.wav` |
| 03_ghost | 909 snare, a softer, more tonal setting (ghost notes) | MckSamplePacks: `TR8/SD/007_909_Snare_Drum_3.wav` |
| 04_clap | 808 hand clap (Roland TR-808, Michael Fischer's recordings) | sounds-tr808-fischer (TidalCycles): `cp8/CP.WAV` |
| 05_chat | 808 closed hi-hat | sounds-tr808-fischer: `ch8/CH.WAV` |
| 06_ohat | 808 open hi-hat, decay 5 | sounds-tr808-fischer: `oh8/OH50.WAV` |
| 07_rim | 909 rim shot | MckSamplePacks: `TR8/PERC/009_909_Rim_Shot.wav` |
| 08_shake | 727 cabasa | MckSamplePacks: `TR8/PERC/028_727_Cabasa.wav` |
| 09_kick2 | 808 bass drum, tone 2.5, decay 7.5 (long) | sounds-tr808-fischer: `bd8/BD2575.WAV` |
| 10_snr2 | 707 snare | MckSamplePacks: `TR8/SD/008_707_Snare_Dum_1.wav` |
| 11_snap | 808 finger snap (TR-8) | MckSamplePacks: `TR8/PERC/024_808_Finger_Snap.wav` |
| 12_rim8 | 808 rim shot | sounds-tr808-fischer: `rs8/RS.WAV` |
| 13_chat9 | 909 closed hi-hat, short | MckSamplePacks: `TR8/HATS/010_909_Closed_HiHat_Short.wav` |
| 14_conga | 808 mid conga, tuning 5 | sounds-tr808-fischer: `mc8/MC50.WAV` |
| 15_tom | 808 low tom, tuning 5 | sounds-tr808-fischer: `lt8/LT50.WAV` |
| 16_ride | 909 ride, short | MckSamplePacks: `TR8/HATS/027_909_Ride_Cymbal_Short.wav` |

Repositories (licence CC0-1.0 in each):
- https://github.com/MckAudio/MckSamplePacks (5db40e8)
- https://github.com/tidalcycles/sounds-tr808-fischer (85fbecf)

The sounds were chosen by measurement (decay time, brightness), not by
ear. The first kit (acoustic, from Virtuosity Drums) was replaced because
its room sound made the grooves wash out.

Left out on purpose: MckSamplePacks' DR5 and RX5 sets. They are recordings of
sample-ROM machines, where the CC0 covers the recordings but arguably not the
manufacturers' samples inside them. The TR-8 and TR-808 are synthesised.
