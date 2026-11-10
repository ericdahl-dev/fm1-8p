# Voices, stealing, sustain and bend

How the engine (`engine/engine.c`, `engine/voice.c`) turns notes into
voices. The number formats are in [fixed-point.md](fixed-point.md).

## Voice count

`fm_engine_init(e, sample_rate, voice_count)` sets the voice count, 1 to
`FM_MAX_VOICES` (8). The FM-1 target is 6, with 4 as the fallback if the CPU
test says 6 don't fit. The desktop render tool takes `-v VOICES` (default 6).
All voice state is in the caller's `fm_engine`; nothing is allocated.

## Which voice a note gets

On a note on, in this order:

1. **Same note.** If a voice is already sounding that note (held, held by
   the pedal, or releasing), it plays it again: its attack restarts from the
   current level, so there is no click and no second voice. At most one voice
   ever has a given note.
2. **Idle voice.** Otherwise the first idle voice takes it.
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
remembered during the fade, and if its key comes up before the fade ends, it
starts and releases at once (or is held, if the pedal is down).

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
  semitones, clamped to notes 0 to 131. The frequency between two semitones
  is a straight line between their table entries, which is at most about
  0.7 cents off the exact exponential curve (in the middle of the semitone)
  and exact on every semitone. `pitch_bend_shifts_pitch_by_its_range` checks
  full down, full up, a semitone and an in-between value within 1 cent.
- A bend retunes every sounding note at once and applies to new notes. A
  stolen voice's old note keeps its pitch for the 3 ms it is fading.
- Changing `bend_range_semitones` applies the last bend value again with the
  new range.

## Gain staging

| Stage | Format | Peak |
| --- | --- | --- |
| Saw | Q8.24 | 1.0 (polyBLEP keeps it within +-1.0) |
| Voice: saw times amp envelope | Q8.24 | 1.0 |
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
for a polysynth without a limiter. Once velocity (#8) and the patch level
exist, the output gain can be revisited with real patches and the CPU test's
final voice count.

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

Per sounding voice, per sample: one envelope step, one saw sample (with a
64-by-32 divide on the two samples around each wrap, see
[fixed-point.md](fixed-point.md)), one 64-bit multiply and an add into the
mix buffer. Idle voices are skipped. Per sample for the whole engine: one
64-bit multiply (the mix gain) and the int16 conversion.

The mix buffer is 64 frames of `int32_t` inside `fm_engine` (256 bytes), so
the audio interrupt's stack holds only a few locals whatever the block size.
`fm_engine` is about 0.7 KiB (692 bytes on x86-64) with room for 8 voices.

Divisions all happen on events: one 64-by-32 divide per note on (its pitch),
and one per voice for each pitch bend message. A fast bend sends a few
hundred messages a second, so up to 8 divides each is far below the render's
cost, but they run wherever the caller applies MIDI events.
