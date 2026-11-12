# axoloti-playground: context for Claude

Standalone synth on an **original Axoloti Core** (not a Ksoloti board), played
from a **Launchkey 49 MK3** (not an MK2) on the board's USB host port. Nuno's
direction: a **Freak-style synth** (multi-engine digital oscillator, filter,
modulation, effects, "strange" sounds), at most 3 voices. Do not frame it
with musical styles or genres in docs or the cheat sheet. No Rhodes and no
drums, since other gear covers them. The README has the setup and
measurements.

## Toolchain (identical layout on both machines)

- Ksoloti Patcher 1.1.0 at `~/.local/opt/ksoloti-1.1.0/ksoloti-1.1.0`, set to
  Firmware Mode "Axoloti Core". It bundles a JRE, arm-none-eabi-gcc 9.3.1 and
  dfu-util in `platform_linux_x64/bin`.
- Object libraries live in `~/ksoloti/1.1.0/`. This repo is registered as a
  local library, `<filelib>` "axoloti-playground", in `ksoloti.prefs`.
- Patch compile flags and memory map: `firmware/Makefile.patch.mk` and
  `firmware/ramlink_axoloti.ld`. 44 KB SRAM1 holds code, rodata and data;
  64 KB SRAM3 is loadable; 50 KB CCM holds bss; 8 MB SDRAM is filled at
  runtime only.

## Working method

- **Measure on the board headless:** `./Ksoloti -runTest <file.axp|dir>`
  compiles, uploads, runs for 1 s and logs "DSP load". Close the GUI patcher
  first, since it holds the USB connection.
  - Test through `.axp` files, not bare `.axo`: a bare object becomes embedded
    and loses its relative includes.
  - If the board hangs, `-runTest` still compiles and keeps the build files
    under `~/ksoloti/1.1.0/build/<abs path>/`. Inspect `.cpp`, `.lst` and
    `.map` there without touching the board.
- **Recovering a hung board** ("Core not responding") takes a USB replug, which
  only Nuno can do. Ask for it once and arm a watcher that runs the next test
  on replug.
- **GUI:** launch detached with the log at `~/.cache/ksoloti/patcher.log`.
  Passing a file to a second `./Ksoloti` invocation opens it in the running
  instance.
- **Controller MIDI:** with `patches/dev/midi_monitor.axp` live, log the
  laptop side with `aseqdump -p "Axoloti Core"`.

## Rules learned the hard way

- Objects used in **polyphonic subpatches** must be trivially destructible.
  Otherwise the generated `static voice v[N]` calls newlib `atexit()`, which
  hung the board. Before uploading, `grep -c 'bl.*<atexit>' <build>.lst` must
  be 0. Use placement new into raw storage, as `objects/braids_osc.axo` does.
- Ksoloti stripped the Mutable Instruments oscillator code from the firmware
  tree but kept the headers and tables. Vendor upstream sources from commit
  `6165129…` so they match. Ksoloti's Braids header moved the delay lines
  into SDRAM, and its `Init()` allocates them; see `third_party/braids/README.md`.
- The firmware contains Clouds, Warps and Elements code (free in patch memory)
  and Braids' tables. Clouds costs 79–103 % DSP, so it is unusable alongside
  voices.
- Elements (`fx/lmnts`) needs `/shared/elements/*.raw` on the SD card, or it
  hangs the board.
- The Launchkey MK3 lights its pads only in DAW mode, through its second USB
  port. That port is not yet tested through the Axoloti host.

- Inlet and outlet names: the patcher doubles underscores in the generated
  identifiers (`rev_amount` → `outlet_rev__amount`), so use names without `_`.
- freak v1 architecture: `launchkey_ctl` (one per patch) writes
  `freak::bus()`, a namespace-scope POD in `lib/launchkey_ctl.h`. The patch is
  one translation unit, so there is one instance. `freak_voice` and
  `fx_delay` read it, with no wires. Change the mapping in the header, keep
  `tests/` and `docs/cheatsheet.tex` in step, and run `make test cheatsheet`.
- **Do not trust Ksoloti's Mutable Instruments wrappers to size buffers
  correctly.** The factory fx/lmnts/reverb (32 KB allocated, 64 KB used) and
  the Braids delay lines (8200 bytes allocated, 16384 used) both overran.
  sdram_malloc is a bump allocator, so an overrun silently corrupts the next
  object's memory, and the result depends on init order. Check each engine's
  real buffer size (`sizeof`, FxEngine template size) and run `make render`
  under ASan.
- If the board "sounds wrong", probe before guessing: put `dev/probe` (id
  attribute) at each stage and run `-runTest`; the probe logs are staggered,
  because the log keeps only one message per burst. Compare with the host
  render (`make render`).
- Inside a polyphonic subpatch the generated code has a class named `voice`,
  so never name an object member `voice`.
- SRAM1 is at 95 % (42.9 of 44 KB) with freak v1 (after SPICE and the performance pads); the next addition needs code moved to SRAM3. Braids is compiled at -Os by a pragma in third_party/braids/digital_oscillator.cc (17 KB instead of 24 KB; about +1 % DSP per voice). The next code growth has to
  go to SRAM3 (`__attribute__((section(".sram3")))`), or Braids has to shrink.

- grains (SD-card patch): the engine costs **1.76 % DSP per live grain** on the
  board. The cost is mostly compute, not SDRAM: a CCM-resident source saved only
  0.2 % per grain. The pool is 20 grains (idle 41 %, full 75.5 %).
  `GrainsCtl` is compiled at -Os by a pragma in lib/grains_ctl.h; at -O3 it was
  17 KB of SRAM1.
- SD file reads from a patch: sector-aligned file offsets and 4-byte-aligned
  buffers only (`STM32_SDC_SDIO_UNALIGNED_SUPPORT FALSE`). `sdram_malloc`
  returns the SDRAM base when it is full (check `sdram_get_free()`) and is not
  thread-safe (the loader waits for the first DSP tick). `-runTest` does not
  copy `file-depends`; Live in the GUI or Upload to SD Card does. ChibiOS is
  3.2.1: use `THD_WORKING_AREA`, `chThdShouldTerminateX`, `ST2MS`.
- Two patches: grains on the SD card (`/start.bin`, plus `/grains/*.wav`),
  freak in flash. The board boots the SD one when a card is in, so pulling the
  card always recovers freak.

- groove (third patch, its own SD card): Nuno asked for drums and a broken-beat
  groove for this patch (2026-11-11), so the "no drums / no genre framing"
  rules above apply to freak and grains, not to groove. Kit: CC0 only (the repo
  is public); `tools/make_kit.sh` pins the source commits. Nuno found the
  acoustic kit too reverby: the kit is drum machines only (dry), and the 16
  styles are funk, neo-soul, boom-bap and broken beat, 4 each, each with its
  own pads, tempo, swing and drunk. The loop is always 4 bars: Nuno asked
  (a length button with no display is guesswork). The hub drains
  MIDI from a ring in `tick()`, so the looper state has one writer. The
  sequencer and controller are at -Os (SRAM1 32 KB); per-sample code stays
  at -O3. DSP on the board: 46.5 % idle, 80.5 % with beat + 4-voice chord.
  `make render_groove` with mode `tour` renders every style for one lap.

- soliton (fourth patch, 2026-11-12; not yet installed on flash or a card):
  each voice's oscillator is a periodic field evolving under Burgers, KdV,
  Kuramoto–Sivashinsky or focusing NLS (`lib/soliton_field.h`; numerics and
  their reasons are in its header). Lessons:
  - **FFT cost on the board** (DWT cycle counter, `objects/dev/soliton_cycles*`):
    radix-2 was 22,000 cycles for 128 complex points. CMSIS-DSP 1.6's radix 8
    (vendored in `third_party/cmsis_fft`, namespaced, because the firmware's
    `arm_*` symbols would clash) plus the firmware's assembly
    `arm_bitreversal_32` (declared `extern "C"`) take 9,300; a 128-point real
    FFT takes 5,700.
  - **DSP load is a 2 ms moving average**, and above the limit the firmware
    mutes the output. So per-tick work must be bounded: a field step is 12
    work items of at most ~8,000 cycles, and the voices take turns by the
    parity of the controller's tick (`SolBus::tick`, global, so idle voices
    cannot fall into step). One item per tick for all 3 voices measured
    89–90.5 %; taking turns, 61.5–68.5 %.
  - Integrating-factor RK4 blew up on KdV at large dispersion; Strang
    splitting (exact linear flow, RK4 on the nonlinear term) fixed it. Split-step
    NLS resonates when the top mode's linear phase per substep nears pi
    (Weideman–Herbst): the step bounds it at 2.5 rad.
  - `./tests/render_soliton <dir> probe` and `patches/bench_soliton/probe_c3.axp`
    printed identical RMS, peak and zero-crossing numbers: host and board agree.

- aeolian (fifth patch, 2026-11-12; not installed): a wind harp. Nuno asked
  for "something wild, droning or ambient, out of the box"; I proposed and
  built it. Eight waveguide strings, each driven by a van der Pol wake at the
  Strouhal frequency (Facchinetti–de Langre wake-oscillator model, velocity
  coupling normalised by the lift so the loop gain does not depend on U or d).
  Lessons:
  - A per-sample division, int/float conversions and member reloads made a
    string cost 265 cycles per sample (104.5 % DSP). Locals, integer
    indices with power-of-two buffers, a division-free damping polynomial and
    round-robin parameter updates: ~150 cycles, 75.5 %.
  - The loss filter must be pitch-relative (kappa = T60 at 4 kHz / T60 of
    the fundamental, b solved per string): a fixed b killed dark high strings.
  - Gaussian draws for the wind interleaved with other draws from one
    xorshift stream biased the gusts by 4 standard errors: separate
    splitmix32 stream.
  - The controller publishes every other tick; init() publishes too, and the
    renderer mirrors the object's init + one tick, or the (chaotic) host and
    board runs diverge. They then match to the digit.

- patch switcher (2026-11-12, Nuno's request): `objects/patch_switch.axo` in
  all five patches; Device Select + Device Lock together, then top pad 1-5
  (freak, grains, groove, soliton, aeolian) loads `/<name>_v1/patch.bin`
  with the firmware's `LoadPatch` ("Upload to SD Card" writes exactly
  that path, per the patcher's source in Ksoloti.jar `main/java`).
  - **Never touch the SD card (or LogTextMessage) in a MIDI handler**: those
    threads have 256-byte stacks, FatFs LFN needs 512 bytes of stack. It
    hung the board; the k-rate code does it now.
  - Load by file name (marked "TODO: must test!!" in the firmware) works:
    `/start.bin` loaded from k-rate code, grains came up with its bank.
  - Laptop MIDI reaches a running patch: `amidi -p hw:1,0,0 -S "BF 33 7F ..."`
    during `-runTest` (port "Axoloti Core Axoloti MIDI").
  - The card in the board also holds Nuno's other Axoloti material; only
    add files, never rename or delete without asking.

## Open items

- SD card: holds grains (`/start.bin`, `/grains/01..08.wav`). Standalone boot
  from the card was confirmed by Nuno on 2026-11-05. Reach the card via *Board →
  Enter Card Reader Mode*, since the laptop has no SD reader.
- grains presets are set from the parameter meanings and not yet tuned by ear.
- soliton: presets are set from the parameter meanings, not tuned by ear, and
  the patch is not installed (flash or a card: Nuno to choose; the patch
  switcher would put it on one card with grains and groove).
- soliton: Nuno (2026-11-12): "the 4 engines sound similar but ok".
- aeolian: presets not tuned by ear; not installed.
- Patch switcher: works on the Launchkey (Nuno, 2026-11-12). The five patches
  are on the card (/<name>_v1/patch.bin); /start.bin must be a patch that
  contains the switcher (Upload to SD Card as Startup), or nothing switches.
- Next sound work: multimode filter, cycling envelope, LFO → timbre on the mod
  wheel, pitch bend, pad LEDs through the DAW port.
- Plaits port (speech, modal and chord engines): ≈36 KB code + 12 KB tables
  for ARM. Needs its own namespace-renamed stmlib.
