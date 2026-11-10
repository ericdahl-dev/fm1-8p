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
| Note frequency | `uint32_t` | unsigned Q16.16 Hz | | up to 15.8 kHz (note 131) | A 12-entry table for notes 120 to 131 (`tools/gen_pitch_table.py`); lower octaves are exact right shifts, so no `pow()`. The increment is `(hz_q16 << 16) / rate`, computed once per note on. |
| Audio sample (inside the engine) | `int32_t` | Q8.24 | 2^24 | about -128 to +128 | 24 fraction bits keep far more resolution than the 16-bit output, and 7 bits of headroom above 1.0 leave room to sum voices, filter resonance and chorus in later slices before the final clamp. |
| Envelope level | `int32_t` | Q1.30 | 2^30 (`FM_ENV_FULL`) | 0 to 1.0 | Fine enough for a smooth 10 s segment at 192 kHz (the smallest step is about 560 LSB). |
| Gain | `int32_t` | Q1.30 | 2^30 | 0 to 1.0 | Same as the envelope, so `sample * gain >> 30` keeps Q8.24. |
| polyBLEP correction | `int64_t` | Q16 | 2^16 | -1.0 to +1.0 | Only computed on the two samples around each saw wrap. |
| Pulse width | `uint32_t` | unsigned Q0.32 | one full cycle = 2^32 | 2 increments to 1 cycle minus 2 increments | Same scale as the phase, so the second saw of a pulse is just `phase + width`. Set from `oscN_pulse_width_pct` and limited by the pitch on events (see [Aliasing threshold](#aliasing-threshold)). |
| Tuning | `int32_t` | cents | | -2500 to +2500 (2 octaves and 100 cents) | `octave * 1200 + fine`. Turned into a ratio 2^(c/1200) on events by a cubic series in Q2.30 (error under 0.001 cent), so no table and no `pow()`. |
| Oscillator mix | `int32_t` | Q1.30 | 2^30 (`FM_OSC_MIX_FULL`) | 0 to 1.0 | `a + (b - a) * mix >> 30`: exactly one oscillator at either end. |
| Noise state | `uint32_t` | xorshift32 | | never 0 | Three shifts and three XORs a sample; the top bits, `>> 7`, are the Q8.24 sample. |
| Output | `int16_t` | Q1.15 | 32768 | -32768 to 32767 | What a codec or a WAV file takes. Q8.24 maps to it with `>> 9` and saturation. |

Multiplies of two 32-bit values go through a 64-bit intermediate
(`(int64_t)a * b >> n`). Divisions happen only on events (note on, a parameter
change, an envelope stage change), except the polyBLEP correction, which does
one 64-by-32 division on each of the two samples around every saw wrap. A
pulse or square is two saws, so it does that around both of its edges: four
divisions a cycle. On
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
- envelope times: a 0 ms segment jumps at once instead of dividing by 0
  samples;
- the remaining divisors are constants (1000).

`extreme_settings_stay_bounded_and_release` and
`uninitialized_engine_is_silent_and_does_not_divide_by_zero` cover these on
the desktop, where a division by zero does trap.

### Gain staging in this slice

The one voice is the oscillator mix (a saw, square or noise peaks at 1.0 in
Q8.24) times the amp envelope (Q1.30) times a fixed voice gain of 0.5, so the
output peaks at about half of full scale (-6 dBFS). Gain staging for several
voices and the chorus comes with the polyphony and chorus slices.

The pulse is the difference of two saws, `saw(t) - saw(t + width)`, so it has
no DC (like an AC-coupled analog pulse): it sits at +2(1 - w) for the last
fraction w of the cycle and at -2w for the rest. Peak to peak is always 2.0,
but a narrow pulse peaks near +2.0 (or a wide one near -2.0), so the voice can
reach almost full scale, against half for the saw and square. The mix is a
crossfade, so two oscillators together peak no higher than the louder one.

### Parameter values

Parameters are plain integers in the unit their name gives, for example
`amp_attack_ms` 0 to 10000 and `amp_sustain_permille` 0 to 1000. The engine
converts them to samples or Q1.30 when they are set, not per sample. The table
is in `engine/params.c`.

Each voice has two oscillators. `osc1_wave` and `osc2_wave` pick 0 saw,
1 pulse, 2 square or 3 noise; `oscN_octave` (-2 to +2) and `oscN_fine_cents`
(-100 to +100) tune each from the played note; `oscN_pulse_width_pct` (1 to 99)
sets the pulse duty; and `osc_mix_permille` crossfades from osc1 alone (0, the
default) to osc2 alone (1000). For example:

```sh
./build/fm1-8p-render -s osc1_wave=1 -s osc1_pulse_width_pct=25 \
    -s osc2_octave=1 -s osc2_fine_cents=7 -s osc_mix_permille=500 song.mid song.wav
```

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
  per-block arrays on the stack); today they use a few dozen bytes. Any scratch
  buffer a later slice needs goes in `fm_engine`.

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

The pulse and square are held to the same **-25 dB** limit with the same
measurement (`high_pulse_and_square_notes_have_low_aliasing` in
`tests/test_osc.c`). Each is two polyBLEP saws, so each edge gets the same
correction as the saw's wrap.

| Wave at C7 | Naive | polyBLEP, double model | This engine |
| --- | --- | --- | --- |
| Square (50 %) | -14.2 dB | -28.6 dB | -28.6 dB |
| Pulse 25 % | -13.3 dB | -29.4 dB | -29.4 dB |
| Pulse 10 % or 90 % | -9.8 dB | -26.4 dB | -26.4 dB |
| Pulse 1 % or 99 %, unlimited | -1.2 dB | -12.9 dB | -12.9 dB |
| Pulse 1 % or 99 %, limited | | | -28.0 dB |

A 1 % pulse at C7 is 0.2 samples wide, inside the two-sample polyBLEP span, so
it is mostly aliasing whatever the precision. The oscillator therefore plays a
width at least two phase increments (two samples) from either edge of the
cycle, worked out when the note or the width changes; above a quarter of the
sample rate only a square fits. At 44.1 kHz the limit reaches a 1 % setting
only above 220 Hz (A3), and a 10 % setting only above 2.2 kHz.

Noise is white and is not measured for aliasing.

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
