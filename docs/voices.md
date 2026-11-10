# Voices, stealing, sustain, bend and unison

How the engine (`engine/engine.c`, `engine/voice.c`) turns notes into
voices. Everything up to [Unison](#unison) is poly mode, the default. The
number formats are in [fixed-point.md](fixed-point.md).

## Voice count

`fm_engine_init(e, sample_rate, voice_count)` sets the voice count, 1 to
`FM_MAX_VOICES` (8). The FM-1 target is 6, with 4 as the fallback if the CPU
test says 6 don't fit. The desktop render tool takes `-v VOICES` (default 6)
and plays one MIDI channel, `-c CHANNEL` (default: the channel of the first
note on); the engine itself has no notion of channels.
All voice state is in the caller's `fm_engine`; nothing is allocated.

## Which voice a note gets

On a note on, in this order:

1. **Same note.** If a voice is already sounding that note (held, held by
   the pedal, or releasing), it plays it again: its attack restarts from the
   current level, so there is no click and no second voice. The same goes for
   a voice that a steal is still fading that note out on (see below). So at
   most one voice ever has a given note.
2. **Idle voice.** Otherwise an idle voice takes it: the one whose last note
   on or off was longest ago (least recently used).
3. **Steal.** Otherwise a sounding voice is stolen. The engine takes the
   first non-empty group, and within it the voice whose last note on or off
   was longest ago:
   1. released voices (key up, in their release),
   2. voices held only by the sustain pedal,
   3. held voices (key down).

   So the quietest, least-needed note goes first: an old release before a
   recent one, any release before a pedal-held note, and a held note only when
   every voice is held, the oldest first.

A stolen voice is not cut. Its old note fades from its current level to
silence over **3 ms** (`STEAL_FADE_MS`, 132 samples at 44.1 kHz). Then the new
note starts from silence with its own attack, so it sounds the same as a note
on an idle voice, just 3 ms late. The new note always sounds: its key is
remembered during the fade. If its key comes up before the fade ends, the
note still starts after the fade, holds for 3 ms (as long as it waited), then
releases; if the pedal is down, it is held.

If the stolen note is played again during the fade, the steal is called off:
the voice takes that note back and restarts its attack from the current
(partly faded) level, so it never sounds on two voices. The note that was
waiting for the voice then gets one by the rules above, which may steal
another voice. On a 1-voice engine there is none, so the newest note wins and
the waiting note is dropped; it is also dropped if its key already came up
(and the pedal is up).

If yet another note arrives while every voice is busy and the chosen voice is
already fading, that voice just switches to the newer note; the note it was
waiting for never starts. That takes more notes at once than there are
voices plus one, and the newest notes win.

Ages are kept as a counter of note ons and offs (`stamp`), compared as a
wrapping 32-bit difference, so they never overflow in practice (2^31 events).

## Release and idle

A note off starts the release (unless the pedal holds the note). When the
release reaches zero the voice goes idle: it costs nothing to render and is
the first choice for the next note. `fm_engine_active_voices()` counts
voices that are sounding or fading, so a caller (or the CPU test) can see
voices free up.

## Sustain pedal (CC 64)

- Value 64 or more is down, below 64 is up.
- While the pedal is down, a key that comes up leaves its note sounding at
  its sustain level, whether the note started before or after the pedal went
  down.
- Playing a pedal-held note again restarts it on the same voice and it is
  held by its key again.
- When the pedal comes up, every note it was holding releases. Notes whose
  keys are still down keep sounding.
- CC 123 (all notes off) releases every note, including pedal-held ones.
  CC 120 (all sound off) silences every voice at once.

## Pitch bend

`fm_engine_pitch_bend(e, value)` takes the MIDI 14-bit value, 0 to 16383,
with 8192 as center (the host builds it from the message's two data bytes,
least significant first). Values above 16383 are ignored.

- **Range:** the `bend_range_semitones` parameter, 0 to 24, default 2. Full
  down (0) is exactly `-range` semitones; full up (16383) is `8191/8192` of
  `+range`, as MIDI defines it.
- **Format:** the bend is kept as Q16.16 semitones,
  `(value - 8192) * range * 8`. That equals
  `(value - 8192) / 8192 * range * 65536` exactly, so there is no rounding,
  and the largest value (24 semitones) is about 1.6 million, far inside
  `int32_t`.
- **Pitch:** each voice's pitch is `note << 16` plus the bend, in Q16.16
  semitones; each oscillator adds its own tuning (octave and fine) to it.
  A pitch outside the table's notes 0 to 131 folds back by whole octaves,
  with the increment shifted by the same power of two, so intervals stay
  exact up to the cap just under Nyquist. Between semitones the frequency is
  the note below times 2^(f/12), by a cubic series (under 0.001 cent off),
  and it is exact on whole notes (see
  [fixed-point.md](fixed-point.md#formats)).
  `pitch_bend_shifts_pitch_by_its_range` checks full down, full up, a
  semitone and an in-between value within 1 cent;
  `tuned_increment_matches_exact_pitch` checks every cent over two octaves
  each way within 0.01 cent.
- A bend retunes every sounding note at once and applies to new notes. A
  stolen voice's old note keeps its pitch for the 3 ms it is fading.
- Changing `bend_range_semitones` applies the last bend value again with the
  new range.

## Unison

The `voice_mode` parameter picks how notes get voices: 0 is poly (the
default, everything above), 1 is unison. In unison every voice plays the
same note, detuned, for big mono leads and basses. Exactly `voice_count`
voices stack (the voices past it in `fm_engine` never sound), and the
render still skips voices that are idle, so a unison note costs the same as
a chord of `voice_count` notes and silence costs nothing.

### Detune

`unison_detune_cents`, 0 to 100, default 20, is the spread from the lowest
voice to the highest. The voices sit evenly across it, centered on the note:
voice `i` of `n` is offset by `cents * (i / (n - 1) - 1/2)`. With 4 voices
and 60 cents they are at -30, -10, +10 and +30 cents; one voice has no
detune. The offsets are kept per voice in `fm_engine.detune` as Q16.16
semitones, the same format as the bend, and added to the note and bend
before the pitch-to-increment lookup:

    detune[i] = cents * 65536 * (2i - (n - 1)) / (200 * (n - 1))

The product is at most 100 * 65536 * 7 (about 46 million), inside
`int32_t`; the divisor is at least 200 because one voice skips the divide;
the division truncates toward zero, so the spread stays symmetric, and it is
off by under 1/65536 of a semitone. The offsets are computed when
`voice_mode` or the detune changes (a detune change retunes a sounding
unison note at once), never per note or per sample. The pitch error between
semitones (under 0.8 cents, see [Pitch bend](#pitch-bend)) applies to each
voice. `detune_spreads_voice_pitches_by_the_set_amount` checks each voice's
frequency for 2, 4 and 8 voices.

### Which note plays: last-note priority

Unison is monophonic. The engine remembers which keys are down, oldest
first (up to 16, `FM_HELD_MAX`; past that the oldest is forgotten):

- **Note on: retrigger.** The new note always wins. Every voice moves to it
  and its envelope restarts its attack from the current level, the same as
  playing a note again in poly. There is no steal fade: the oscillators keep
  their phase and only their increment changes, so the waveform has no jump
  and the level only rises from where it was.
- **Note off of the sounding note: legato.** If other keys are still down,
  the voices go back to the newest of them without retriggering: the pitch
  changes, the envelope carries on where it is. If no key is down, every
  voice releases, or is held by the sustain pedal.
- **Note off of any other key** just forgets that key.
- **Sustain pedal:** it holds the note once the last key is up, and a new
  note on while it holds retriggers as above. Pedal up releases it. A key
  released while others are down does not stay as a pedal-held fallback.
- **CC 123 / CC 120** release or silence every voice, as in poly, and forget
  the keys that were down.
- There is no glide (portamento): pitch changes are instant. A glide time
  would fit in this same place later, moving each voice's increment toward
  the target once per block.

`new_note_retriggers_and_release_returns_legato_to_held_note` checks the
retrigger, the legato return and both pitches, and
`unison_note_changes_do_not_click` checks a start, a retrigger, a legato
return and a release for jumps, as the poly click test does.

### Switching modes

Changing `voice_mode` releases every note (as CC 123 does) and forgets the
held keys, so each mode starts clean: the next poly note gets a voice by the
usual rule, in tune, and the next unison note takes every voice. Notes that
were sounding fade out with their release; a key that was down at the
switch does nothing when it comes up. The voices still sounding belong to
no note from then on: a note on never reuses one (it takes a voice by the
usual rule, which may steal it), and a bend does not retune it, so a unison
tail ends at its own detuned pitch instead of collapsing onto the new note.
Setting the mode to the value it already has changes nothing, and neither
does changing the detune in poly mode. In poly every voice's detune is 0.

### Gain

Unison changes no gain. Each voice is still mixed at `0.5 / voice_count`,
so a unison note at full level peaks exactly where a full chord does: at
most 16384 (-6 dBFS) when the saws line up, which they do at 0 cents (all
voices start a unison note from the same phase on a fresh engine). With
detune the saws drift in and out of phase and the peak moves between that
ceiling and well below it. That makes one unison note `voice_count` times
(at 6 voices, 15.6 dB) louder than one poly note, as stacking oscillators
on a real synth does. `unison_at_full_velocity_never_clips` checks 1 to 8
voices at 0, 7 and 100 cents, through a retrigger.

## Gain staging

| Stage | Format | Peak |
| --- | --- | --- |
| Saw | Q8.24 | 1.0 (polyBLEP keeps it within +-1.0) |
| Voice: oscillator mix times amp envelope | Q8.24 | 1.0 |
| Sum of voices (`fm_engine.mix`) | Q8.24 | `voice_count`, at most 8.0, which uses 4 of the 7 headroom bits |
| Mix gain | Q1.30 | `0.5 / voice_count` = `(1 << 29) / voice_count`, rounded down |
| Output before conversion | Q8.24 | 0.5 |
| Output | `int16_t` | 16384 (-6 dBFS) |

Each voice is scaled by `1 / voice_count`, so the worst case, every voice at
full level with their peaks lined up, reaches exactly full scale before a
fixed -6 dB. That -6 dB is headroom saved for the filter's resonance and the
chorus in later slices; until then nothing can come within 6 dB of clipping.
The int16 conversion still saturates as a last guard.

The cost of this is level: one note on a 6-voice engine peaks at
`16384 / 6` = 2731, about -21.6 dBFS (-18 dBFS for 4 voices). That is normal
for a polysynth without a limiter. Velocity
([modulation.md](modulation.md#velocity)) only lowers a note from there. Once
a patch level exists, the output gain can be revisited with real patches and
the CPU test's final voice count.

`full_chord_at_max_velocity_never_clips` plays every voice count from 1 to 8
in octaves (whose saws wrap together, the worst case) and checks no sample
reaches +-32767, and that the peak stays at or below 16384.

## Clicks

`no_click_when_a_voice_starts_is_stolen_or_ends` plays notes 0 to 4 (8 to
10 Hz, whose saws change by under 2 LSB per sample) on a 4-voice engine
through starts, a steal and two releases, and checks that no sample differs
from the one before by more than **128 LSB** (about -48 dBFS). A cut or an
instant restart anywhere but the start of a cycle would jump by hundreds to
thousands of LSB. Starts and ends are click-free with any attack or release
of a few ms; a 0 ms attack or release is a deliberate hard edge.

## Cost

Per sounding voice, per sample: one envelope step, one sample of the
oscillator pair (a saw has a 64-by-32 divide on the two samples around each
wrap, a pulse or square on both edges, and the mix adds a multiply when both
oscillators sound; see [fixed-point.md](fixed-point.md)), one 64-bit multiply, an add into the mix
buffer, and a test for a queued release (the steal fade case above). The LFO
and velocity add nothing per sample; their per-tick cost is in
[modulation.md](modulation.md#cost). Idle voices are skipped. Per sample for the whole engine: one
64-bit multiply (the mix gain) and the int16 conversion.

The mix buffer is 64 frames of `int32_t` inside `fm_engine` (256 bytes), so
the audio interrupt's stack holds only a few locals whatever the block size.
The voices take about 1.3 KiB with room for 8 (two oscillators each), and
unison adds 49 bytes (8 detune offsets and the 16 held keys). With the chorus
(its 2 KiB delay line and a 256-byte right-side buffer) `fm_engine` is about
4.0 KiB (4056 bytes on x86-64; the LFO and velocity state of #8 add 392), so
static storage is recommended.

Divisions all happen on events. A note on costs two 64-by-32 divides per
voice (one per oscillator's pitch, plus one more for each pulse oscillator's
gain), and a pitch bend message the same per sounding voice. In unison a note
on or a legato return costs that for every voice (up to 32 divides at 8
voices with two pulse oscillators); a detune or mode change adds one 32-by-32
detune divide per voice, and the device CPU test should time a burst of these.
Rendering in unison is no different from rendering the same number of poly
voices. A fast bend sends a few hundred messages a second, so a few dozen
divides each is far below the render's cost, but they run wherever the caller
applies MIDI events.
