# LFO and velocity

How the engine (`engine/lfo.c`, `engine/voice.c`, `engine/osc.c`) moves a
sound over time and responds to how hard a key is played. Number formats are
in [fixed-point.md](fixed-point.md); voices, stealing and unison in
[voices.md](voices.md).

| Parameter | Range | Default | What it does |
| --- | --- | --- | --- |
| `lfo_wave` | 0..3 | 0 | 0 triangle, 1 saw (rising), 2 square, 3 random (sample and hold) |
| `lfo_rate_centihz` | 1..3000 | 500 | rate in hundredths of a hertz: 0.01 Hz to 30 Hz (5 Hz) |
| `lfo_delay_ms` | 0..5000 | 0 | after a note starts, the LFO is held at zero this long, then fades in over as long again |
| `lfo_pitch_cents` | 0..1200 | 0 | pitch depth (vibrato) at full LFO, each way |
| `lfo_pw_pct` | 0..49 | 0 | pulse width depth at full LFO, in percent of a cycle, each way |
| `amp_velocity_pct` | 0..100 | 0 | how much velocity sets the level: 0 ignores it, 100 follows the full curve |

Every depth defaults to 0 and so does `amp_velocity_pct`, so a patch that
does not set them renders byte-identical to an engine without this module
(`zero_pitch_depth_leaves_the_output_untouched`,
`zero_pulse_width_depth_leaves_the_output_untouched`, and the host's demo
render). A cutoff route (`lfo_cutoff`) and filter velocity land with the
filter (#7).

## One LFO, a delay per voice

There is **one LFO for the whole engine**, free-running from
`fm_engine_init` (no key trigger), and **each voice has its own delay and
fade-in**. That is how the classic two-oscillator polys of the mid-80s
behave: every voice wobbles in step, so a chord's vibrato or PWM moves as one
instead of beating, and a note played later still gets its own slow onset.
With key trigger off, separate per-voice LFOs would all run in step anyway, so
one LFO gives the same sound for one LFO's cost; only the delay and fade
(a counter and a level per voice) need to be per voice. The random wave is
shared too: every voice takes the same new level each cycle.

A per-voice, key-triggered LFO (each note starting its own phase) would be
an extra parameter later; its cost would be one LFO step per voice per tick
instead of one per tick.

## Control rate

The LFO and everything it modulates update once per **control tick of 32
samples** (`FM_LFO_TICK_FRAMES`, 0.73 ms or 1378 Hz at 44.1 kHz), not per
sample. Ticks are counted from `fm_engine_init`, and `fm_engine_render`
splits its work at tick boundaries, so the caller's block size never moves
them (`block_size_does_not_change_lfo_output` renders blocks of 1, 31, 255,
256 and 4096 frames byte for byte the same). At 30 Hz, the fastest rate, a
cycle still has 46 ticks; the steps of a vibrato are far below what the ear
hears as zipper noise, and the oscillators keep their phase across each step.

With every depth at 0 the ticks change no sample, so the engine does not
split its 64-frame mix chunks at them: the ticks a chunk passes run after it
and only advance the LFO and the tick count, without visiting the voices.
That keeps the default patch's cost where it was before the LFO existed.
Once a depth is set (and for one tick after it goes back to 0, to clear the
offsets) the chunks stop at every tick again.
`block_size_does_not_change_output_as_depths_come_and_go` turns the depths
on and off mid-note and checks blocks of 1, 31, 100 and 4096 frames still
render byte for byte the same.

## Waveforms

The LFO value is Q1.30, -1.0..+1.0. Its phase is unsigned Q0.32 of a cycle,
advanced each tick by an increment computed once per rate change:

    inc = rate_centihz * 2^(32 + 5) / (sample_rate * 100)

(5 is log2 of the tick length; one 64-by-32 divide, on a parameter change
only, and never by zero: `fm_engine_init` rejects a zero rate and the LFO
code returns a stopped LFO for one.) The phase resolution makes the rate
exact to far better than 0.01 %.

| Wave | Starts at | Shape |
| --- | --- | --- |
| triangle | 0, rising | 0 -> +1 -> -1 -> 0 |
| saw | 0, rising | 0 -> +1, jumps to -1, -> 0 |
| square | +1 | +1 for the first half cycle, -1 for the second |
| random | 0 | a new random level (xorshift32) at the start of each cycle, held until the next |

`lfo_rate_matches_the_setting_for_each_waveform` counts cycles of each
waveform at 0.05, 0.5, 5, 10 and 30 Hz over 20 or 200 seconds and checks the
count within one cycle.

## Delay and fade-in

When a voice starts a note from silence (from idle, or after a steal fade),
its LFO level drops to zero and is held there for `lfo_delay_ms`, then rises
in a straight line to full over another `lfo_delay_ms`. With a delay of 0 the
LFO is at full depth at once. The delay is counted in ticks (rounded down),
the fade step is `2^30 / delay_ticks`, both computed when the parameter is
set, so the tick has no divide. A voice keeps the tick its delay started at,
not a running counter, so ticks without modulation have nothing to count.
The start is the first tick after the note starts, even while every depth
is 0: a depth set later finds the delay already running, or over
(`lfo_delay_counts_from_the_note_even_with_no_depth_set`).

A note restruck while its voice still sounds (the same note again in poly,
a note taken back during a steal fade, a unison retrigger) and a unison
legato return do not restart the delay: the vibrato and pulse width carry on
from where they are, so the pitch never jumps by up to the full depth in one
sample. `restriking_a_sounding_note_keeps_its_vibrato` restrikes a note at
the top of a one-octave vibrato and checks the pitch stays there.

`lfo_delay_holds_modulation_at_zero_then_fades_it_in` plays a note with a
5 Hz square vibrato, a 100-cent depth and a 400 ms delay, and checks the
pitch is unmodulated (under 0.5 cent) for 400 ms, then grows through about
12, 37, 62 and 87 cents in the next four half-cycles, then sits at 100.

## Routing

At each tick each sounding voice computes `lfo * fade` (Q1.30) and scales
it by each depth. A target whose depth is 0 gets an offset of exactly 0, and
the oscillators are only touched when an offset changes, so zero depth costs
no work and changes no sample.

### Pitch

`lfo_pitch_cents` is kept as Q16.16 octaves (`cents * 65536 / 1200`, exact
for multiples of 75 cents and otherwise off by under 1/65536 of an octave).
The offset `x` multiplies both oscillators' phase increments by `2^x`,
on top of the note, bend, unison detune and each oscillator's tuning:

- whole octaves are shifts, and the fraction `2^f` on [0, 1) is a quartic
  with Q30 coefficients fitted for relative error, under 0.011 cent;
- no divide: the increment for the unmodulated pitch is computed once per note
  or bend as before (a 64-by-32 divide per oscillator), and the tick only
  multiplies it, so vibrato adds no per-sample or per-tick divide;
- a bend, retune or tuning change while the LFO runs recomputes the base
  increment and keeps the current offset.

The trade-off: the pulse width limits (at least two samples from each edge)
and the 50 % square above a quarter of the sample rate are worked out from
the unmodulated pitch. A deep vibrato at very high notes can push a narrow
pulse a little past those limits for part of the cycle; the alternative was a
divide per oscillator per tick.

`lfo_to_pitch_moves_the_pitch_by_the_depth` checks a 1 Hz square LFO at
100 cents moves A4 to +100 and -100 cents within 1 cent.

### Pulse width

`lfo_pw_pct` (percent of a cycle, kept as Q0.32) is added to both
oscillators' set pulse widths, then clamped to 1..99 % and to the pitch
limits above. It changes only oscillators playing the pulse wave (the square
stays 50 %). A pulse is scaled to peak 1.0 at any width by a gain of
`1 / (2 max(w, 1 - w))`; set statically that is an exact divide, but under
modulation it is a reciprocal by Newton's method (the seed
`48/17 - 32/17 m` and two steps, good to about 1e-5 or 0.0001 dB), so the tick
has no divide. With a depth of 0 the exact gain is kept, byte for byte.

`lfo_to_pulse_width_moves_the_duty_by_the_depth` checks that a 1 Hz square
LFO at 30 % moves a 50 % pulse to 80 % and 20 % duty within 1 %, at the same
peak level.

## Velocity

Velocity (MIDI 1..127; 0 is a note off) sets the **amp envelope's peak**:

    curve = (velocity / 127)^2
    peak  = 1 - amp_velocity_pct / 100 * (1 - curve)

so velocity 127 is always full level, and at 100 % velocity 64 is about
-12 dB and velocity 32 about -24 dB, which feels even across the keyboard's
range (a squared curve, as on most velocity-sensitive synths). At 50 % the
softest notes are about -6 dB. The sustain level is scaled by the peak too
(a 70 % sustain stays 70 % of the note's own peak), and the attack still takes
its set time to reach the peak. Both are computed per note on (two constant
divides); the sample loop is unchanged, since only the envelope's targets
move.

Because velocity moves the envelope's targets rather than a gain after it,
the level never jumps:

- a **retrigger** of a sounding note (same note in poly, or a new unison
  note) at a softer velocity, while the level is above the new peak, goes
  straight to the decay and glides down from where it is over the decay time;
  a harder one rises from where it is (`softer_retrigger_glides_down_without_a_click`);
- a **stolen** voice starts the new note from silence at the new note's
  velocity, as before;
- a **unison legato return** (releasing the newest key while an older one is
  held) does not retrigger, so it **keeps the velocity of the last strike**:
  the pitch changes and the level carries on. Going back to the older key's
  own velocity would mean a level jump or a new envelope stage on a note
  nobody struck (`unison_legato_return_keeps_the_last_strike_velocity`).

`higher_velocity_gives_louder_output` checks that every step up in velocity
is louder, that the level follows the curve within 1 %, that 50 %
sensitivity goes halfway, and that 0 % ignores velocity.

Filter velocity (velocity also opening the cutoff) lands with the filter
(#7).

## Cost

Per control tick (every 32 samples), for the whole engine: one LFO step.
Only while a depth is set, per sounding voice per tick: the delay and fade
from its start tick (one multiply during the fade), two 32-by-32 multiplies
into 64 bits (pitch and width depth), and only when an offset changes, the
2^x quartic (five multiplies) and one multiply per oscillator, and for a
pulse oscillator the clamp and the Newton reciprocal (five multiplies).
Velocity adds nothing per sample (one multiply per note on and per sustain
change, for the sustain target). No divides on the tick.

Measured with `make bench` (`host/bench.c`, the FM-1 CPU test's held chord
at 6 voices, 256-frame blocks) on an Intel i5-8350U desktop. The modulated
rows come from a copy of `firmware/cpu_test.c` that sets those parameters.
The machine was shared with other builds, so each figure is the best of
several runs, and runs vary by 10 % or more:

| Workload | ns / block, first run | ns / block, after review fixes (busier machine) |
| --- | ---: | ---: |
| main before #8, defaults (saw) | 10480 | 13996 |
| with #8, defaults (all depths 0) | 9890 | 13115 |
| with #8, osc 1 pulse, depths 0 | 13220 | 18430 |
| with #8, osc 1 pulse, 30-cent vibrato, 30 % PWM at 6 Hz, velocity 100 % | 14090 | 21079 |

With every depth at 0 there is no measurable cost: the default patch renders
as fast as main, within noise. With pitch and pulse width modulated, the LFO
adds about **150 to 450 ns per voice per 256-frame block** on this desktop,
about 7 to 14 % of a pulse voice. The FM-1 CPU test (`firmware/`) runs the
default patch, so its numbers do not change; timing the modulated case on
the device is for the CPU test's next round (#13).

Memory: each voice grows by 40 bytes (its LFO start tick, offsets and
velocity peak; the oscillator pair's unmodulated increments and offsets; the
envelope's peak and sustain target), and `fm_engine` holds the LFO (20
bytes), its routing (16) and two tick counters. `fm_engine` is 4056 bytes,
392 more than before. Nothing is allocated and the render path
adds no stack arrays.
