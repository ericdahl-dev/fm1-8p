# Fixed-point formats

The engine (`engine/`) uses only integer arithmetic, so it runs on the FM-1's
JieLi AC791N (pi32v2, no FPU usable for this) and renders bit for bit the same
there as on the desktop. `make check-float-free` enforces this (see
[Float-free check](#float-free-check)).

Notation: **Qm.n** is a signed (or, where noted, unsigned) value with `m`
integer bits, including the sign, and `n` fraction bits. Its value is the
integer divided by 2^n.

## Formats

| Quantity | Type | Format | 1.0 is | Range | Why |
| --- | --- | --- | --- | --- | --- |
| Oscillator phase | `uint32_t` | unsigned Q0.32 | one full cycle = 2^32 | 0 to just under 1 cycle | Wraps for free on overflow; 32 bits give a frequency step of rate / 2^32 (about 10 uHz at 44.1 kHz). |
| Phase increment | `uint32_t` | unsigned Q0.32 per sample | | capped at `0x7FFF0000`, just under Nyquist | Same scale as the phase, so one add per sample. |
| Pitch (note plus bend) | `int32_t` | Q16.16 semitones | one semitone = 2^16 | MIDI notes 0 to 131 | Pitch bend is exact in this format (see [voices.md](voices.md#pitch-bend)). Between semitones the frequency is interpolated on the note table. |
| Note frequency | `uint32_t` | unsigned Q16.16 Hz | | up to 15.8 kHz (note 131) | A 12-entry table for notes 120 to 131 (`tools/gen_pitch_table.py`); lower octaves are exact right shifts, so no `pow()`. The increment is `(hz_q16 << 16) / rate`, computed once per note on. |
| Audio sample (inside the engine) | `int32_t` | Q8.24 | 2^24 | about -128 to +128 | 24 fraction bits keep far more resolution than the 16-bit output, and 7 bits of headroom above 1.0 leave room to sum voices, filter resonance and chorus in later slices before the final clamp. |
| Envelope level | `int32_t` | Q1.30 | 2^30 (`FM_ENV_FULL`) | 0 to 1.0 | Fine enough for a smooth 10 s segment at 192 kHz (the smallest step is about 560 LSB). |
| Gain | `int32_t` | Q1.30 | 2^30 | 0 to 1.0 | Same as the envelope, so `sample * gain >> 30` keeps Q8.24. |
| polyBLEP correction | `int64_t` | Q16 | 2^16 | -1.0 to +1.0 | Only computed on the two samples around each saw wrap. |
| Filter pitch (cutoff, key tracking, envelope amount) | `int32_t` | cents above MIDI note 0 (8.18 Hz) | | cutoff 0 to 14400 | Cutoff, key tracking and the filter envelope add in this log domain, so one octave is always 1200 whatever the frequency. |
| Filter stage coefficient `g` | `int32_t` | Q1.30 | 2^30 (fully open) | 0 to 1.0 | Same scale as the envelope; a stage is `y += (x - y) * g >> 30`. |
| Resonance feedback `k` | `int32_t` | Q4.28 | 2^28 | 0 to 4.2 | The loop self-oscillates from about 3.4 (top of the range) to 4.2 (lowest cutoffs); 4.2 at resonance 1000. |
| Output | `int16_t` | Q1.15 | 32768 | -32768 to 32767 | What a codec or a WAV file takes. Q8.24 maps to it with `>> 9` and saturation. |

Multiplies of two 32-bit values go through a 64-bit intermediate
(`(int64_t)a * b >> n`). Divisions happen only on events (note on, pitch
bend, a parameter change, an envelope stage change), except the polyBLEP correction, which does
one 64-by-32 division on each of the two samples around every saw wrap. On
the JieLi clang toolchain these compile to the native divide, and the branch
links with no `__*di3` or float helpers (checked by the #12 firmware work).
If the CPU test shows the divides cost too much, a reciprocal of the
increment computed once per note on (or once per block) can replace them.

Every divisor is non-zero by construction, because division by zero does not
trap on pi32v2; it returns garbage:

- the sample rate: `fm_engine_init` rejects 0, and the pitch code returns 0
  for a zeroed (uninitialized) engine instead of dividing;
- the oscillator increment: the polyBLEP division runs only when it is
  non-zero;
- envelope times: a 0 ms segment (or the steal fade at a very low sample
  rate) jumps at once instead of dividing by 0 samples;
- the voice count: `fm_engine_init` rejects 0 before dividing by it for the
  mix gain;
- the remaining divisors are constants (1000).

`extreme_settings_stay_bounded_and_release` and
`uninitialized_engine_is_silent_and_does_not_divide_by_zero` cover these on
the desktop, where a division by zero does trap.

### Gain staging

Each voice is the saw (peak 1.0 in Q8.24) times its amp envelope (Q1.30).
The voices are summed in Q8.24 and the sum is scaled by
`0.5 / voice_count` (Q1.30), so even every voice at full level with lined-up
peaks stays at or below half of full scale (-6 dBFS), with 6 dB left for
resonance and the chorus. The table and the reasoning are in
[voices.md](voices.md#gain-staging).

### Filter

`engine/filter.c` is a per-voice filter: a one-pole highpass, then a
resonant 4-pole (24 dB/octave) lowpass.

- **Lowpass.** Four one-pole stages in series with the last stage fed back
  to the input times `-k`. Each stage is `m = x - (x - x1) / 4` (`x1`: its
  input one sample ago), then `y += g * (m - y)`. The `x1` term puts a zero at
  z = -1/3 in each stage. Without it the stages lag too little in phase at
  high cutoffs, so the one-sample delay in the feedback made the loop buzz at
  Nyquist there, and stay below self-oscillation at the bottom. With it,
  resonance 1000 self-oscillates within 30 cents of the cutoff up to
  3.5 kHz (sharp at higher cutoffs: 7.1 kHz for an 8.4 kHz cutoff), and
  never at Nyquist. The feedback sum goes through a soft clip: a wire up to
  +-2.0 (a full-scale saw passes untouched), then a quadratic knee that
  flattens at +-4.0. `m` is a weighted average of `x` and `x1`, and `y` of `m`
  and the old `y`, so no state can pass the clip's limit. The filter cannot
  overflow or run away at any resonance, and self-oscillation settles at a
  fixed level (about 0.83 of full scale at mid cutoffs).
- **Cutoff.** The voice's cutoff is `cutoff + key_track * (note - 60) / 10 +
  env_amount * env_level`, all in cents, computed every 16 samples (a
  2.8 kHz control rate at 44.1 kHz), not per sample. The coefficient is the
  matched-pole `g = 1 - e^(-2 pi fc / fs)`, worked out from two small
  tables (`tools/gen_filter_tables.py`): `2^(cents / 1200)` in 16-cent steps,
  and `(1 - e^-x) / x`, both linearly interpolated. The sample rate's own
  pitch in cents is found once at init (`fm_filter_rate_cents`, a binary
  search with no division), so the update needs only multiplies, shifts and
  divisions by the constant 1200. Corners at or above Nyquist use the Nyquist
  coefficient (about 0.957). Across the top 600 cents of the range (13800 to
  14400) the stage zero, the feedback and `1 - g` fade linearly to zero, so
  the maximum cutoff (14400, the default) is an exact wire and the default
  patch sounds exactly as it did before the filter.
- **Highpass.** `x - lowpass(x)` with a one-pole lowpass, so it cannot
  resonate. Position 0 is off (a wire), 1 to 3 have corners at about 100, 240
  and 560 Hz (6 dB/octave).
- **Filter envelope.** A second `fm_env` ADSR, advanced every sample like the
  amp envelope; its amount is -9600 to +9600 cents (8 octaves either way).

Measured response (`tests/test_filter.c`, sines at 44.1 kHz, cutoff 440 Hz):

| Measurement | Measured | Ideal 4-pole | Test tolerance |
| --- | --- | --- | --- |
| 3 octaves below cutoff | -0.27 dB | -0.27 dB | above -0.5 dB |
| At cutoff, no resonance | -12.0 dB | -12.0 dB | +-2 dB |
| Drop over the 1st octave above cutoff | 15.9 dB | 15.9 dB | +-2 dB |
| Drop over the 2nd octave | 21.2 dB | 21.2 dB | +-2 dB |
| Drop over the 3rd octave | 23.0 dB | 23.3 dB | +-2 dB |
| Drop over the 1st octave, resonance 600 | 24.4 dB | 24.4 dB | 24 +-3 dB |
| At cutoff, resonance 500 / 900 | -5.6 / +9.6 dB | -5.6 / +13.2 dB | each at least 5 / 12 dB above the previous |

With no resonance, four equal poles meet at the cutoff, so the cutoff point
itself is -12 dB and the slope only reaches 24 dB/octave about two octaves up.
A moderate resonance (around 600) lifts the cutoff point to about -3.4 dB,
and the octave above it is then about 24 dB lower. Near self-oscillation the
peak is lower than the ideal analog ladder's (+9.6 against +13.2 dB at 900):
close to threshold, the small differences between the digital loop and the
analog one matter most. Resonance also costs bass (the
passband is `1 / (1 + k)` of the input; -14 dB at resonance 1000), as in the
analog ladder it is modeled on; there is no gain compensation.

Highpass gains: at 55 Hz, 0 / -6.4 / -13.2 / -20.6 dB for positions 0 to 3;
at 110 Hz, 0 / -2.7 / -7.8 / -14.7 dB.

Cost per sample, per voice: five 32x32->64 multiplies (four stages and the
feedback) and about 40 adds, shifts and compares, plus one multiply for the
highpass when it is on, one in the clip only above +-2.0, and the filter
envelope step. Inside the top 600 cents of cutoff the stage zero takes a
multiply instead of a shift (four more). The cutoff update every 16 samples
adds about ten multiplies and three divisions by constants (1200 and 10,
which compile to multiplies), under one multiply per sample on average. There are
no per-sample divisions and no 64-bit divisions in the render path. On a
desktop i5-8350U (a noisy laptop measurement) one voice rose from about 5-7 to
19-23 ns per sample with every filter control in use; the four stages form a
serial chain of dependent multiplies, so on the device this is latency more
than throughput. The device numbers come with the CPU spike.

### Parameter values

Parameters are plain integers in the unit their name gives, for example
`amp_attack_ms` 0 to 10000 and `amp_sustain_permille` 0 to 1000. The engine
converts them to samples or Q1.30 when they are set, not per sample. The table
is in `engine/params.c`.

## Assumptions about the C implementation

- `>>` on a negative `int32_t` or `int64_t` is an arithmetic shift. C99 leaves
  this to the implementation; GCC, Clang and the JieLi toolchain all do it.
  `engine/engine.c` fails to compile if it is not true.
- Left shifts of negative values are undefined, so the engine multiplies
  instead (UBSan checks this in the tests).
- `int64_t` exists. On a 32-bit CPU, 64-bit multiplies and divides may become
  library calls such as `__divdi3`; the float-free check allows those.

## Sample rate and block size

- The FM-1 runs at 44.1 kHz with 256-frame blocks, so the host and the tests
  default to 44100 Hz. One block is 5.8 ms of audio, and all the voices plus
  the chorus have to render within that.
- The sample rate is an argument to `fm_engine_init` (1 Hz to 192 kHz) and is
  never hardcoded, because the FM-1's real rate still has to be confirmed.
  Most tests run at 44.1 kHz; the edge-case test also uses 1 Hz, 8 kHz and
  192 kHz.
- `fm_engine_render` takes any number of frames, so the caller (the desktop
  host or the device's audio callback) picks the block size. The engine works
  sample by sample and keeps no block buffers. The host renders between MIDI
  events, so its blocks vary in size. `block_size_does_not_change_output`
  checks that blocks of 1, 255, 256 and 4096 frames give byte-identical
  output.
- Output is interleaved stereo `int16_t` (L, R, L, R ...).
- All state lives in the caller-owned `fm_engine` struct; the engine has no
  globals other than `const` tables, and allocates nothing.
- On the device, `fm_engine_render` runs in the audio interrupt, which has
  about 7.75 KiB of stack. Render paths must keep no large locals (no
  per-block arrays on the stack); today they use a few dozen bytes. Scratch
  buffers go in `fm_engine`, like the 64-frame mix buffer.

## Constants the FM-1 build rejects

The fm1-dock image build rejects any 32-bit word in 0xFFC00000..0xFFCFFFFF
because it looks like a ROM address. Q8.24 -0.25 is exactly 0xFFC00000, and
`-32767 << 7` is 0xFFC00080, so such constants are easy to make by accident.
Clamp first and shift after, and don't write negative constants that land in
that range. Layer 4 of the engine check (below) scans for them.

## Aliasing threshold

The saw is a naive ramp with a 2-point polyBLEP correction. The test
`high_saw_note_has_low_aliasing` renders C7 (MIDI 96, 2093 Hz) at 44.1 kHz, takes
a 32768-point Blackman-Harris windowed FFT, and counts as aliasing all energy
more than 6 bins away from a harmonic of the note. The limit is **-25 dB**
relative to the total energy.

| Saw | Aliasing on this test |
| --- | --- |
| Naive ramp | -12.6 dB |
| polyBLEP, this engine | -27.9 dB |
| polyBLEP, double-precision model | -27.9 dB |

The fixed-point saw matches the floating-point model, so the remaining
aliasing is a property of 2-point polyBLEP, not of the number format. A
stronger method (a wider BLEP, or oversampling) can lower the threshold later
if listening tests call for it.

## Float-free check

`make check-float-free` runs `tools/check-engine.sh` on every `engine/*.c`.
Despite the name, it checks for constants the FM-1 build rejects as well as
for floating point and the heap.
Each layer can fail the check on its own:

1. The sources are compiled with the FPU turned off for code generation
   (`-mgeneral-regs-only`, plus `-mno-sse -mno-mmx -mno-80387` on x86), at
   `-O0` so nothing is folded away, and again at `-O2`. GCC then rejects any
   floating-point operation; Clang turns it into soft-float library calls,
   which layer 3 catches.
2. `float` and `double` are defined to undeclared names, so any
   floating-point type fails to compile, even an unused one. As a result the
   engine can include only headers that never mention them; today it includes
   only `<stdint.h>`.
3. The external symbols the objects reference, apart from those the engine
   defines itself, must be on a short allowlist (memory helpers, stack
   protection, 64-bit integer helpers). That rejects soft-float helpers such as
   `__muldf3`, libm, and the heap (`malloc`, `free` and so on).
4. No instruction immediate (as `objdump -d` prints it) or data word may fall
   in 0xFFC00000..0xFFCFFFFF. Desktop code generation differs from the
   device's, so this catches constants written in the source, not every word
   the device compiler might derive. fm1-dock's own check stays the final
   word.

The target then compiles the canaries in `tests/canary/` (floating-point
arithmetic, a `float` variable, a `malloc` call, and a 0xFFCxxxxx constant as
an immediate and in a table) along with the engine and
fails unless the check rejects each one. The check works on x86-64 and arm64
with GCC or Clang, so it runs on Linux, macOS and in CI.
