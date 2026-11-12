#!/usr/bin/env bash
# make_kit.sh: builds the groove patch's drum kit, objects/kit_bb/NN_name.wav,
# from CC0 sample libraries at pinned commits (credits: objects/kit_bb/CREDITS.md).
#   tools/make_kit.sh [out_dir]      (make kit)
# Needs curl, ffmpeg, ffprobe, od and awk.
#
# Each pad: decode to 48 kHz mono (stereo averaged), find the onset (the
# first sample at 5 % of the peak or more), cut from 1 ms before it for the
# pad's length, fade in over 0.5 ms (no click on the cut) and out over the
# last third, normalise the peak to -1 dBFS, then apply the pad's mix level
# (dB). The audio stays in float from the source file to the final 16-bit
# file; the 16-bit copy is only for finding the onset and the peak. Levels
# and lengths are chosen from each sound's role, not by ear.
set -euo pipefail
export LC_ALL=C  # decimal points in printf and awk

out=${1:-objects/kit_bb}
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Drum machines only (dry: no room sound): Roland TR-808 recorded from a
# real unit (Fischer), and the TR-8's models of the 808, 909, 707 and 727.
MK=https://raw.githubusercontent.com/MckAudio/MckSamplePacks/5db40e8fe26785c256845a5bb38921b2654a887f/TR8
F8=https://raw.githubusercontent.com/tidalcycles/sounds-tr808-fischer/85fbecf1bec32553395625ea659e2a56dfd7c0e1

# Whole-kit trim (dB), on top of each pad's mix level: drum machines are
# denser than the acoustic kit was, and -3 dB puts the drums back level with
# the chords (make render_groove: drums RMS 0.072 -> about 0.051, chords 0.048).
trim=-3

# name | source URL | length (s) | mix level (dB re -1 dBFS peak)
# Lengths: at least ~1.5x the sound's time to -30 dB (measured), so the
# fade over the last third only shortens the tail.
pads=(
  "01_kick|$MK/BD/006_909_Bass_Drum_3.wav|0.5|0"
  "02_snare|$MK/SD/006_909_Snare_Drum_2.wav|0.35|-1"
  "03_ghost|$MK/SD/007_909_Snare_Drum_3.wav|0.2|-9"
  "04_clap|$F8/cp8/CP.WAV|0.4|-3"
  "05_chat|$F8/ch8/CH.WAV|0.15|-8"
  "06_ohat|$F8/oh8/OH50.WAV|0.55|-9"
  "07_rim|$MK/PERC/009_909_Rim_Shot.wav|0.12|-5"
  "08_shake|$MK/PERC/028_727_Cabasa.wav|0.2|-11"
  "09_kick2|$F8/bd8/BD2575.WAV|1.45|-1"
  "10_snr2|$MK/SD/008_707_Snare_Dum_1.wav|0.3|-2"
  "11_snap|$MK/PERC/024_808_Finger_Snap.wav|0.2|-5"
  "12_rim8|$F8/rs8/RS.WAV|0.1|-6"
  "13_chat9|$MK/HATS/010_909_Closed_HiHat_Short.wav|0.12|-10"
  "14_conga|$F8/mc8/MC50.WAV|0.35|-6"
  "15_tom|$F8/lt8/LT50.WAV|0.6|-3"
  "16_ride|$MK/HATS/027_909_Ride_Cymbal_Short.wav|1.0|-11"
)

for p in "${pads[@]}"; do
  IFS='|' read -r name url len mix <<< "$p"
  src="$work/$name.src"
  curl -sSfL -o "$src" "$url"
  ch=$(ffprobe -v error -select_streams a:0 -show_entries stream=channels -of csv=p=0 "$src")
  mono="aresample=48000:resampler=soxr"
  if [ "$ch" -ge 2 ]; then mono="pan=mono|c0=0.5*c0+0.5*c1,$mono"; fi
  ffmpeg -nostdin -loglevel error -i "$src" -af "$mono" -f s16le -acodec pcm_s16le "$work/$name.raw"
  # onset: the first |x| >= 5 % of the peak; the cut starts 48 samples (1 ms) earlier
  read -r onset peak < <(od -An -v -t d2 -w2 "$work/$name.raw" | awk '
    { x = $1 < 0 ? -$1 : $1; v[NR] = x; if (x > m) m = x }
    END { for (i = 1; i <= NR; i++) if (v[i] >= 0.05 * m) { print i - 1, m; exit } }')
  start=$(( onset > 48 ? onset - 48 : 0 ))
  gain=$(awk -v m="$peak" -v mix="$mix" -v trim="$trim" 'BEGIN { printf "%.2f", 20 * log(0.891 * 32767 / m) / log(10) + mix + trim }')
  fade=$(awk -v l="$len" 'BEGIN { printf "%.3f", l / 3 }')
  fst=$(awk -v l="$len" -v f="$fade" 'BEGIN { printf "%.3f", l - f }')
  ffmpeg -nostdin -loglevel error -y -i "$src" \
    -af "$mono,atrim=start_sample=$start,asetpts=PTS-STARTPTS,atrim=end=$len,afade=t=in:d=0.0005,afade=t=out:st=$fst:d=$fade:curve=exp,volume=${gain}dB" \
    -acodec pcm_s16le -ar 48000 -ac 1 "$out/$name.wav"
  printf "%-9s onset %6d samples, peak %5d, gain %+6.2f dB, %s s\n" "$name" "$onset" "$peak" "$gain" "$len"
done
