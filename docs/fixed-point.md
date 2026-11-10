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
| Oscillator phase | `uint32_t` | unsigned Q0.32 | one full cycle = 2^32 | 0 to just under 1 cycle | Wraps for free on overflow; 32 bits give a frequency step of rate / 2^32 (about 11 uHz at 48 kHz). |
| Phase increment | `uint32_t` | unsigned Q0.32 per sample | | capped at `0x7FFF0000`, just under Nyquist | Same scale as the phase, so one add per sample. |
| Note frequency | `uint32_t` | unsigned Q16.16 Hz | | up to 15.8 kHz (note 131) | A 12-entry table for notes 120 to 131 (`tools/gen_pitch_table.py`); lower octaves are exact right shifts, so no `pow()`. The increment is `(hz_q16 << 16) / rate`, computed once per note on. |
| Audio sample (inside the engine) | `int32_t` | Q8.24 | 2^24 | about -128 to +128 | 24 fraction bits keep far more resolution than the 16-bit output, and 7 bits of headroom above 1.0 leave room to sum voices, filter resonance and chorus in later slices before the final clamp. |
| Envelope level | `int32_t` | Q1.30 | 2^30 (`FM_ENV_FULL`) | 0 to 1.0 | Fine enough for a smooth 10 s segment at 192 kHz (the smallest step is about 560 LSB). |
| Gain | `int32_t` | Q1.30 | 2^30 | 0 to 1.0 | Same as the envelope, so `sample * gain >> 30` keeps Q8.24. |
| polyBLEP correction | `int64_t` | Q16 | 2^16 | -1.0 to +1.0 | Only computed on the two samples around each saw wrap. |
| Output | `int16_t` | Q1.15 | 32768 | -32768 to 32767 | What a codec or a WAV file takes. Q8.24 maps to it with `>> 9` and saturation. |

Multiplies of two 32-bit values go through a 64-bit intermediate
(`(int64_t)a * b >> n`). Divisions happen only on events (note on, a parameter
change, an envelope stage change), except the polyBLEP correction, which does
one 64-by-32 division on each of the two samples around every saw wrap.

### Gain staging in this slice

The one voice is the saw (peak 1.0 in Q8.24) times the amp envelope (Q1.30)
times a fixed voice gain of 0.5, so the output peaks at about half of full
scale (-6 dBFS). Gain staging for several voices and the chorus comes with the
polyphony and chorus slices.

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

- The sample rate is an argument to `fm_engine_init` (1 Hz to 192 kHz) and is
  never hardcoded, because the FM-1's real rate still has to be confirmed.
  Tests run at 44.1 kHz and 48 kHz.
- `fm_engine_render` takes any number of frames, so the caller (the desktop
  host or the device's audio callback) picks the block size. The engine works
  sample by sample and keeps no block buffers. The host renders between MIDI
  events, so its blocks vary in size.
- Output is interleaved stereo `int16_t` (L, R, L, R ...).
- All state lives in the caller-owned `fm_engine` struct; the engine has no
  globals other than `const` tables, and allocates nothing.

## Aliasing threshold

The saw is a naive ramp with a 2-point polyBLEP correction. The test
`high_saw_note_has_low_aliasing` renders C7 (MIDI 96, 2093 Hz) at 48 kHz, takes
a 32768-point Blackman-Harris windowed FFT, and counts as aliasing all energy
more than 6 bins away from a harmonic of the note. The limit is **-25 dB**
relative to the total energy.

| Saw | Aliasing on this test |
| --- | --- |
| Naive ramp | -12.9 dB |
| polyBLEP, this engine | -28.5 dB |
| polyBLEP, double-precision model | -28.5 dB |

The fixed-point saw matches the floating-point model, so the remaining
aliasing is a property of 2-point polyBLEP, not of the number format. A
stronger method (a wider BLEP, or oversampling) can lower the threshold later
if listening tests call for it.

## Float-free check

`make check-float-free` runs `tools/check-engine.sh` on every `engine/*.c`.
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

The target then compiles the canaries in `tests/canary/` (floating-point
arithmetic, a `float` variable, a `malloc` call) along with the engine and
fails unless the check rejects each one. The check works on x86-64 and arm64
with GCC or Clang, so it runs on Linux, macOS and in CI.
