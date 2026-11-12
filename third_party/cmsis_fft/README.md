# cmsis_fft

The radix-8 complex FFT of **CMSIS-DSP V1.6.0** (Arm, Apache-2.0), cut down
to the two lengths the soliton patch uses (64 and 128 points).

- **Source:** the Ksoloti 1.1.0 tree, `CMSIS/DSP/Source/TransformFunctions/`
  (`arm_cfft_radix8_f32.c`, `arm_cfft_f32.c`, `arm_bitreversal2.c`) and
  `CommonTables/arm_common_tables.c`. The patch firmware ships no float FFT.
- **Changes:**
  - The function bodies are unchanged, apart from the split below.
  - They are `static`, in `namespace cmsis_fft`, so they cannot clash with the
    firmware's `arm_*` symbols, which a patch links against.
  - `arm_cfft_radix8by2_f32` is split in two: its radix-2 loop
    (`arm_cfft_radix8by2_pass_f32`) and its two radix-8 calls
    (`arm_cfft_radix8by2_half_f32`), so the solver can spread one 128-point
    FFT over several control ticks.
  - Only `arm_radix8_butterfly_f32`, that function,
    `arm_bitreversal_32` and the bit-reversal tables for 64 and 128 points are
    kept.
  - The twiddle tables are built at run time by `lib/pde_fft.h`, into bss, to
    save 1.5 KB of rodata in the patch's 44 KB of SRAM.
- **Why:** the plain radix-2 FFT took 22,000 cycles for 128 complex points on
  the board, and a 128-point real transform took 14,000. With this code (and
  the firmware's assembly bit reversal) they take 9,300 and 5,700.
- **License:** Apache License 2.0. Its header is kept in `cmsis_cfft.h`, and the
  full text is at <https://www.apache.org/licenses/LICENSE-2.0>.
