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
| Pitch (note plus bend plus tuning) | `int32_t` | Q16.16 semitones | one semitone = 2^16 | any; the table covers notes 0 to 131 | Outside the table the pitch folds back by whole octaves and the increment shifts by the same power of two, so intervals stay exact up to the Nyquist cap. Pitch bend is exact in this format (see [voices.md](voices.md#pitch-bend)). Between semitones the frequency is the note below times 2^(f/12), by the same cubic series as the tuning (under 0.001 cent). |
| Note frequency | `uint32_t` | unsigned Q16.16 Hz | | up to 15.8 kHz (note 131) | A 12-entry table for notes 120 to 131 (`tools/gen_pitch_table.py`); lower octaves are exact right shifts, so no `pow()`. The increment is `(hz_q16 << 16) / rate`, computed once per note on. |
| Audio sample (inside the engine) | `int32_t` | Q8.24 | 2^24 | about -128 to +128 | 24 fraction bits keep far more resolution than the 16-bit output, and 7 bits of headroom above 1.0 leave room to sum voices, filter resonance and chorus in later slices before the final clamp. |
| Envelope level | `int32_t` | Q1.30 | 2^30 (`FM_ENV_FULL`) | 0 to 1.0 | Fine enough for a smooth 10 s segment at 192 kHz (the smallest step is about 560 LSB). |
| Gain | `int32_t` | Q1.30 | 2^30 | 0 to 1.0 | Same as the envelope, so `sample * gain >> 30` keeps Q8.24. |
| polyBLEP correction | `int64_t` | Q16 | 2^16 | -1.0 to +1.0 | Only computed on the two samples around each saw wrap. |
| Pulse width | `uint32_t` | unsigned Q0.32 | one full cycle = 2^32 | 2 increments to 1 cycle minus 2 increments | Same scale as the phase, so the second saw of a pulse is just `phase + width`. Set from `oscN_pulse_width_pct` and limited by the pitch on events (see [Aliasing threshold](#aliasing-threshold)). |
| Tuning | `int32_t` | Q16.16 semitones | one semitone = 2^16 | -25 to +25 semitones (2 octaves and 100 cents) | Set in cents (`octave * 1200 + fine`) and added to the voice's pitch on events, so tuning and bend combine by one add. The fraction of a semitone becomes a ratio 2^(f/12) by a cubic series in Q2.30 (error under 0.001 cent), so no extra table and no `pow()`. |
| Oscillator mix | `int32_t` | Q1.30 | 2^30 (`FM_OSC_MIX_FULL`) | 0 to 1.0 | `a + (b - a) * mix >> 30`: exactly one oscillator at either end. |
| Sync ratio | `uint32_t` | Q16.16 | 2^16 | osc2's increment over osc1's | Worked out on events; at each osc1 wrap osc2 restarts at `t * ratio >> 16`, where `t` is how far osc1 went past the wrap. |
| Cross-mod depth | `int32_t` | Q1.30 | 2^30 | 0 to 1.0 | Times osc2's increment on events (`xmod_inc`), so each sample's bend is one multiply: `xmod_inc * osc1 >> 24`. |
| Noise state | `uint32_t` | xorshift32 | | never 0 | Three shifts and three XORs a sample; the top bits, `>> 7`, are the Q8.24 sample. |
| Filter pitch (cutoff, key tracking, envelope amount) | `int32_t` | cents above MIDI note 0 (8.18 Hz) | | cutoff 0 to 14400 | Cutoff, key tracking and the filter envelope add in this log domain, so one octave is always 1200 whatever the frequency. |
| Filter stage coefficient `g` | `int32_t` | Q1.30 | 2^30 (fully open) | 0 to 1.0 | Same scale as the envelope; a stage is `y += (x - y) * g >> 30`. |
| Resonance feedback `k` | `int32_t` | Q4.28 | 2^28 | 0 to 4.2 | 4.2 at resonance 1000. In theory the loop self-oscillates at about 4.0 across most of the range, dropping to 3.4 at the top; at the lowest cutoffs 4.2 is not enough (see Filter). |
| Output | `int16_t` | Q1.15 | 32768 | -32768 to 32767 | What a codec or a WAV file takes. Q8.24 maps to it with `>> 9` and saturation. |

Multiplies of two 32-bit values go through a 64-bit intermediate
(`(int64_t)a * b >> n`). Divisions happen only on events (note on, pitch
bend, a parameter change, an envelope stage change), except the polyBLEP correction, which does
one 64-by-32 division on each of the two samples around every saw wrap. That
is 2 divisions a cycle for a saw and 4 for a pulse or square (two saws, one
per edge), so up to 8 a cycle for a voice with two pulse oscillators. Per
sample that is 2 x increment for a saw and 4 x increment for a pulse, where
the increment is in cycles per sample. High notes add up: the highest pitch
is note 127 (12.5 kHz, an increment of 0.284 at 44.1 kHz), which is above a
quarter of the sample rate, so the pulse is forced to a square and divides on
more than every other sample. The worst case is about 2.28 divisions per
sample per voice (two pulse or square oscillators at note 127). On the JieLi
clang toolchain these compile to the native divide, and the branch links with
no `__*di3` or float helpers (checked by the #12 firmware work). If the CPU
measurement (#13) shows the divides cost too much, the planned fix is a
reciprocal of each increment computed on note and retune events, which turns
every polyBLEP division into a multiply.

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

Each voice is its oscillator mix (every waveform peaks at 1.0 in Q8.24),
through the filter, times its amp envelope (Q1.30). The voices are summed in
Q8.24 and the sum is scaled by `0.5 / voice_count` (Q1.30), so even every
voice at full level with lined-up peaks stays at or below half of full scale
(-6 dBFS) without filter resonance. High resonance uses that 6 dB and can
reach full scale, where the int16 conversion saturates (follow-up: #24). The
table and the reasoning are in [voices.md](voices.md#gain-staging).
The chorus (below) never raises the peak above its input's.

The pulse is the difference of two saws, `saw(t) - saw(t + width)`, so it has
no DC (like an AC-coupled analog pulse): it sits at +2(1 - w) for the last
fraction w of the cycle and at -2w for the rest, so a narrow pulse would peak
near +2.0 (a wide one near -2.0), twice the saw. A gain of 1 / (2 max(w,
1 - w)), worked out with the width limit on events, brings every width to a
peak of 1.0, so switching between saw, square and pulse never jumps in level.
At 50 % the gain is 1 (the square is +-1.0). The mix is a crossfade, so two
oscillators together peak no higher than the louder one.

### Filter

`engine/filter.c` is a per-voice filter: a one-pole highpass, then a
resonant 4-pole (24 dB/octave) lowpass.

- **Lowpass.** Four one-pole stages in series with the last stage fed back
  to the input times `-k`. Each stage is `m = x - (x - x1) / 4` (`x1`: its
  input one sample ago), then `y += g * (m - y)`. The `x1` term puts a zero at
  z = -1/3 in each stage. Without it the stages lag too little in phase at
  high cutoffs, so the one-sample delay in the feedback made the loop buzz at
  Nyquist there, and stay below self-oscillation at the bottom. With it,
  resonance 1000 self-oscillates close to the cutoff and never at Nyquist.
  Measured at 44.1 kHz (an impulse, then zero crossings 6 to 8 s later), the
  ringing is within 2 cents of the cutoff from 16 Hz to 440 Hz (+1.7 cents at
  16 Hz, -0.6 at 440 Hz), then increasingly flat: 4 cents at 880 Hz, 12 at
  1.5 kHz, 23 at 2.1 kHz, 63 at 3.5 kHz and 286 at 8.4 kHz (it rings at
  7.1 kHz). The test holds it within a semitone below 4 kHz. Cutoffs
  above Nyquist ring at about fs/4 (11 kHz at 44.1 kHz) until the top-of-range
  fade silences the feedback. At the bottom, the self-oscillation sustains
  from about 15 Hz at 44.1 or 48 kHz and from about 33 Hz at 192 kHz; below
  that, resonance 1000 rings down instead. The feedback sum goes through a soft clip: a wire up to
  +-2.0 (a full-scale saw passes untouched), then a quadratic knee that
  flattens at +-4.0. `m` is a weighted average of `x` and `x1`, and `y` of `m`
  and the old `y`, so no state can pass the clip's limit. The filter cannot
  overflow or run away at any resonance, for any Q8.24 input (the whole
  +-128.0), and self-oscillation settles at a fixed level (about 0.83 of
  full scale at mid cutoffs).
- **32-bit sums (#34).** The sums run in 32 bits; only the products need 64
  (one 32x32->64 multiply each). The highpass state stays within the input's
  range, so while the input and that state are within +-32.0 (an oscillator
  stays near +-1.0) the highpass output is within +-64.0 and, less the
  feedback (within +-4.0 * 4.2), the lowpass input still fits 32 bits.
  Louder, the highpass runs in 64 bits and its output is clamped to +-32.0
  (with the highpass off, the input is), which changes nothing: the clamped
  value less the largest feedback still drives the soft clip past its
  saturation (6.0) with the same sign. The output is bit for bit that
  of the same filter in 64 bits over the whole +-128.0
  (`tests/test_filter_oracle.c` holds it to a 64-bit reference).
- **Cutoff.** The voice's cutoff is `cutoff + key_track * (note - 60) / 10 +
  env_amount * env_level`, all in cents, computed every 16 samples (a
  2.8 kHz control rate at 44.1 kHz), not per sample. Key tracking follows the
  MIDI note number only: pitch bend, each oscillator's octave and fine
  tune, and osc2's coarse tune move the oscillators but not the cutoff. The coefficient is the
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
  resonate. Turning it on clears that lowpass, so the output starts equal to
  the input instead of jumping by whatever it held the last time it was on. Position 0 is off (a wire), 1 to 3 have corners at about 100, 240
  and 560 Hz (6 dB/octave).
- **Filter envelope.** A second `fm_env` ADSR, advanced every sample like the
  amp envelope; its amount is -9600 to +9600 cents (8 octaves either way).
  It starts where the amp envelope does: from 0 on an idle voice or after a
  steal fade (whatever level the last note left it at), from its level on a
  retrigger. During a steal fade the old note keeps its own key-tracked
  cutoff; the new note's applies when it starts, as its pitch does.

Measured response (`tests/test_filter.c`, sines at 44.1 kHz, cutoff 440 Hz).
The acceptance tolerance for the 24 dB/octave slope is: at resonance 0, each
octave's drop within +-2 dB of the ideal 4-pole; at resonance 600, the octave
above the cutoff 24 +-3 dB below the cutoff point.

| Measurement | Measured | Ideal 4-pole | Test tolerance |
| --- | --- | --- | --- |
| 3 octaves below cutoff | -0.27 dB | -0.27 dB | above -0.5 dB |
| At cutoff, no resonance | -12.0 dB | -12.0 dB | +-2 dB |
| Drop over the 1st octave above cutoff (880 Hz: -28.0 dB) | 15.9 dB | 15.9 dB | +-2 dB |
| Drop over the 2nd octave (1760 Hz: -49.3 dB) | 21.3 dB | 21.2 dB | +-2 dB |
| Drop over the 3rd octave (3520 Hz: -73.0 dB) | 23.6 dB | 23.3 dB | +-2 dB |
| Drop over the 1st octave, resonance 600 | 24.4 dB | 24.4 dB | 24 +-3 dB |
| At cutoff, resonance 500 / 900 | -5.6 / +9.6 dB | -5.6 / +13.2 dB | each at least 5 / 12 dB above the previous |

With no resonance, four equal poles meet at the cutoff, so the cutoff point
itself is -12 dB and the slope approaches 24 dB/octave further up (23.6 dB
over the third octave).
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
which compile to multiplies), under one multiply per sample on average; it
is skipped when nothing feeding it changed, and the coefficients are not
recomputed when the cutoff comes out the same. There are no per-sample
divisions and no 64-bit divisions in the render path. The four stages form a
serial chain of dependent multiplies, so on the device this is latency more
than throughput. The device numbers come with the CPU spike.

Two cases cost almost nothing, so the default patch costs what it did
before the filter:

- **Wide open** (cutoff at the top, highpass off: the default). Every stage
  passes its input through exactly, so the render skips the filter
  (`fm_filter_run`, inline: four compares, for the fade, the highpass and
  the input's range) and keeps only the last sample.
  When a setting ends that, the stages are filled from it, exactly the state
  running the filter would have left, so the change is bit for bit the same
  as if the filter had run all along.
- **No envelope amount** (the default). The filter envelope cannot change
  the sound, so it is not stepped every sample: it counts the samples it is
  behind and is caught up by `fm_env_advance` (exactly that many steps, with
  one 32-bit division per stage it crosses) before every gate and every
  change to its settings or the amount, and at least every 2^24 samples. So
  turning the amount up mid-note finds it exactly where it would have been.

When both hold for a whole block, and no note waits for a steal fade and no
release is queued, nothing about the filter can change in the block, so the
voice renders it with a loop of just the amp envelope and the oscillators
and does the filter's bookkeeping (its last sample, the envelope's lag, the
control phase) once at the end. That loop calls nothing but those two: any
other call in it costs a voice about 15 %, because the compiler must then
reload the voice's fields every sample. It relies on the oscillator pair
peaking within the soft clip's untouched +-2.0 (it peaks at 1.0, with sync
and cross-mod too; `extreme_osc_settings_stay_bounded_and_release` and
`max_xmod_and_sync_stay_in_range` hold it there).
`open_filter_without_envelope_matches_the_full_render` (and
`open_filter_with_sync_and_xmod_matches_the_full_render`, with osc2 synced
and fully cross-modulated) checks it against the full loop bit for bit,
through setting changes on a control tick.

`make bench` on a desktop i5-8350U (default patch, ns per voice and sample,
pinned to one core, the best of seven interleaved runs): before the filter
6.8 at 8 voices, 6.9 at 6 and 8.6 at 1; with the filter 6.7, 6.7 and 8.3.
Without the block loop it was 9.6, 9.8 and 11.3.

### Parameter values

Parameters are plain integers in the unit their name gives, for example
`amp_attack_ms` 0 to 10000 and `amp_sustain_permille` 0 to 1000. The engine
converts them to samples or Q1.30 when they are set, not per sample. The table
is in `engine/params.c`.

Each voice has two oscillators. `osc1_wave` and `osc2_wave` pick 0 saw,
1 pulse, 2 square or 3 noise; `oscN_octave` (-2 to +2) and `oscN_fine_cents`
(-100 to +100) tune each from the played note; `oscN_pulse_width_pct` (1 to
99) sets the pulse duty; and `osc_mix_permille` crossfades from osc1 alone (0,
the default) to osc2 alone (1000). osc2 also has `osc2_coarse_semis` (-12 to
+12 semitones, on top of its octave and fine tune) for intervals such as a
fifth (+7) or a fourth (+5). For example:

```sh
./build/fm1-8p-render -s osc1_wave=1 -s osc1_pulse_width_pct=25 \
    -s osc2_octave=1 -s osc2_fine_cents=7 -s osc_mix_permille=500 song.mid song.wav
```

### Mix envelope

`osc_mix_env_permille` (-1000 to +1000, default 0) lets an envelope move the
mix: each voice's mix is `osc_mix_permille` plus that share of the envelope's
level, clamped to 0..1000. Positive depth fades osc2 in as the envelope rises
(a swell where the tone thickens), negative fades it out.
`osc_mix_env_source` picks the envelope: 0 the filter envelope (the default),
1 the amp envelope. The amp envelope is read against the note's own peak, so
a soft note gets the same depth as a hard one (velocity changes the
loudness, not the mix). For example, a pad whose second oscillator blooms over
two seconds:

```sh
./build/fm1-8p-render -s osc2_fine_cents=-12 -s osc_mix_env_permille=800 \
    -s filter_env_attack_ms=2000 -s filter_env_sustain_permille=1000 song.mid song.wav
```

The mix is worked out at the filter's control rate (every 16 samples) and
glides there a sample at a time, so a change of depth or of `osc_mix_permille`
doesn't click and the envelope doesn't step. No divide runs per sample: the
glide's step is a division by the constant 16, once a tick, and the amp
source's reading against the peak is one divide a tick. At depth 0 a voice
renders exactly as with the fixed mix, on the same path; after depth goes
back to 0 it glides to `osc_mix_permille` and returns to that path.

Above 0 the voice leaves the plain path for the full one, even in a flat
sustain (left to the speed pass, #34). With the amp source and no filter
envelope amount the filter envelope stays unstepped. In fm1-emu's strict mode
the CPU test's step F (full patch with the filter, 6 voices) measures 77.2 %
on main before this, 79.0 % at depth 0 and 82.5 % with
`osc_mix_env_permille=500`, the same on two code layouts. Of the 1.8 points
at depth 0, about 1.2 is the per-sample glide check and 0.8 the control
tick's (#34).

### Sync and cross-mod

`osc_sync` (0 or 1) restarts osc2 every time osc1 starts a cycle, so the pair
sounds at osc1's pitch and osc2's tuning shapes the tone: sweep `osc2_octave`
and `osc2_fine_cents` for the classic sync sound. The restart falls between
samples, at the point where osc1 wrapped, and osc2's jump there gets a
polyBLEP on the samples either side of it, like a saw's wrap (see
[Aliasing threshold](#aliasing-threshold)).

`osc_xmod_permille` (0 to 1000) is cross-modulation: osc1's output moves
osc2's frequency every sample, by up to +-100 % of it at 1000. That is
frequency modulation, so it adds sidebands at osc2's frequency plus and minus
multiples of osc1's, from a little grit to a clangorous, bell-like tone.
osc2's polyBLEPs follow the bent pitch. Both act on osc2, so they are heard
only when `osc_mix_permille` is above 0, and they can be used together:

```sh
./build/fm1-8p-render -s osc_sync=1 -s osc2_octave=1 -s osc2_fine_cents=-30 \
    -s osc_xmod_permille=200 -s osc_mix_permille=1000 song.mid song.wav
```

Cost, in fm1-emu's strict mode (the FM-1 CPU test's chord at 8 voices, worst
block of 256 frames; the device reads about 10-15 % higher):

| Setting | before sync and cross-mod | with them |
| --- | --- | --- |
| Default patch (osc1 alone), step 8 | 28.5 % | 27.7 % |
| Full patch (both oscillators, LFO, chorus), step F | 60.5 % | 62.9 % |
| Full patch with sync on and cross-mod 500 | | 77.0 % |

With both off the oscillators take their old path, behind one byte test;
the full patch's difference moves by a point or so with code placement (the
FM-1 runs from a 32 KB flash cache). Sync and cross-mod cost on top of
playing both oscillators, which most patches that use them do anyway.

A restart's position inside the sample (osc1's phase over its increment)
comes from a reciprocal of the increment, kept by Newton steps when the
increments change: no divide on events or at the LFO's control rate, and at
most 1/65536 of a sample from the exact quotient (checked over 10 million
random increments and phases).

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
  2 dB (slow), 3 dB (deep) and 5 dB (fast) compared with chorus off; make-up gain
  is left to the gain staging work in #24, if listening calls for it.
- Cost, per sample with the chorus on: one 64-bit multiply (the sweep), two
  32-bit multiplies (the interpolations), a few shifts, adds and masks, and
  four reads and one write of the delay line. The engine runs it over each
  64-frame mix block (`fm_chorus_process_block`, writing the right side to
  `fm_engine.mix_right`); with the chorus off it skips the call entirely and
  the block costs one check. Divisions run only when the mode is set.

Memory: the delay line is 2048 bytes and the whole `fm_chorus` struct about
2.1 KiB (2072 bytes on x86-64), inside `fm_engine`, plus the 256-byte
`mix_right` buffer. `mix_right` doubles as the voices' scratch: before the
chorus writes it, each voice renders its oscillators' chunk there
(`fm_voice_render_add` takes it as `osc`, `fm_osc_pair_render` fills it),
which is safe because the chorus only writes it
(`chorus_block_writes_right_without_reading_it`). With the voices' modulation sources and sums, their sync and
cross-mod state, their filters (124 bytes each) and their mix envelope
settings, that makes `fm_engine` about 5.2 KiB (5312 bytes on the FM-1,
5480 on x86-64), so
static storage is recommended over a task stack. Nothing is allocated and
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

Synced osc2 is held to the same **-25 dB** limit
(`synced_osc2_has_low_aliasing`): osc1 at C6 (1047 Hz), osc2 alone and 11
semitones up, so each restart catches it most of the way through its cycle, a
near full-size jump. Aliasing counts against osc1's harmonics, the synced
sound's fundamental.

| Synced osc2 at C6, +11 semitones | No polyBLEP on the restart | Only the sample after it | This engine (both sides) |
| --- | --- | --- | --- |
| Saw | -20.9 dB | -20.3 dB | -29.1 dB |
| Square (50 %) | | | -32.0 dB |
| Pulse 25 % | | | -29.8 dB |

After a restart osc2's phase is under one increment, so its own polyBLEP
already treats the restart as a full wrap (a step of -2.0 for a saw); the
restart's correction replaces that with the real jump.

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
