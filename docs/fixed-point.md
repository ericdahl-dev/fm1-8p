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
The chorus (below) never raises the peak above its input's.

### Parameter values

Parameters are plain integers in the unit their name gives, for example
`amp_attack_ms` 0 to 10000 and `amp_sustain_permille` 0 to 1000. The engine
converts them to samples or Q1.30 when they are set, not per sample. The table
is in `engine/params.c`.

## Chorus

`engine/chorus.c` is the last stage of `fm_engine_render`: the mixed voices
(mono Q8.24, after the mix gain) go in a block at a time, left and right
Q8.24 come out, and each is saturated to int16 as before. The `chorus_mode` parameter selects:

| Mode | LFO rate | Center delay | Sweep | Character |
| --- | --- | --- | --- | --- |
| 0 off | | | | input passed to both sides unchanged (bit-exact with the engine before the chorus) |
| 1 slow | 0.5 Hz | 5.0 ms | +-1.5 ms | gentle, slow movement |
| 2 deep | 0.8 Hz | 5.0 ms | +-2.5 ms | wider and faster |
| 3 fast | 6.0 Hz | 3.0 ms | +-0.3 ms | quick, shallow shimmer |

How it works, per sample:

- The input is written to one delay line of 1024 `int16_t` samples, in units
  of Q8.24 >> 10 (1.0 = 16384), rounded, saturating at +-2.0. That keeps 14
  fraction bits, one fewer than the int16 output, on the wet signal only.
  The chorus takes the mix after the mix gain, which peaks at 0.5 (every
  voice at full level with lined-up peaks, times `0.5 / voice_count`; see
  [voices.md](voices.md#gain-staging)), so the line has 12 dB of headroom
  over the loudest mix for the filter's resonance and never clamps today.
- A triangle LFO (Q0.32 phase, one add per sample) sweeps two taps in opposite
  directions: left at center + sweep, right at center - sweep. Delays are
  Q16.16 samples; each tap reads two neighbors and interpolates linearly
  (Q0.15 fraction, 32-bit math).
- Each output is half the dry input plus half its tap. So an output never
  exceeds half the input peak plus 1.0, which is at most the input peak for
  inputs up to +-2.0, and stays far inside int32 for any input. Mixing
  equal parts dry and wet lowers the RMS level of a sustained saw (A3) by
  2 dB (slow), 3 dB (deep) and 5 dB (fast) compared with chorus off; the
  polyphony slice's gain staging can add make-up gain if listening calls for
  it.
- Cost, per sample with the chorus on: one 64-bit multiply (the sweep), two
  32-bit multiplies (the interpolations), a few shifts, adds and masks, and
  four reads and one write of the delay line. The engine runs it over each
  64-frame mix block (`fm_chorus_process_block`, writing the right side to
  `fm_engine.mix_right`); with the chorus off it skips the call entirely and
  the block costs one check. Divisions run only when the mode is set.

Memory: the delay line is 2048 bytes and the whole `fm_chorus` struct about
2.1 KiB (2072 bytes on x86-64), inside `fm_engine`. Nothing is allocated and
the render path adds no stack arrays. The line holds 23 ms at 44.1 kHz; the
deepest mode needs 7.5 ms, so above about 136 kHz a mode's center and sweep
are scaled down together to fit (rate and shape are kept). At very low rates
the center is raised so the nearest tap stays at least one sample back.
Turning the chorus on from off starts from a silent line, so no stale audio
plays. Switching between two "on" modes while sound is playing jumps the tap
delays at once (there is no crossfade), so it can click.

`tests/test_chorus.c` measures each mode's rate, center and sweep by passing a
slow ramp through the module. Linear interpolation of a ramp is exact, so each
tap reads back its own delay up to rounding (under 0.01 sample), and every
sample's delay must stay within 0.05 sample of the ideal triangle (the LFO
increment, rounded down, drifts by about 0.02 sample over the 8 s test).
Without interpolation it would be off by up to half a sample. The tests also
check boundedness with eight summed full-scale voices.

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
- The sample rate is an argument to `fm_engine_init` (8 kHz to 192 kHz; it
  returns `FM_ERR_ARG` outside that) and is never hardcoded, because the
  FM-1's real rate still has to be confirmed. Below 8 kHz the chorus LFO
  increment and the millisecond conversions lose too much precision to be
  worth supporting. Most tests run at 44.1 kHz; the edge-case tests also use
  8 kHz, 11.025 kHz and 192 kHz.
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
