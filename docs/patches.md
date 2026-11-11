# Patches: the JX-8P factory tones

fm1-8p ships the 32 factory tones of the Roland JX-8P, with their factory names, as its patches
(Eric's decision in #75: patch data and names only; no Roland logos or panel art, and the README
disclaimer stays). The playable module boots on tone 17, **SOUNDTRACK**.

| Path | What it is |
| --- | --- |
| `data/jx8p-factory-1-32.syx` | the source: the 32 tones as JX-8P SysEx (APR messages), unchanged |
| `tools/jx8p.py` | the converter: SysEx to engine values, and the C table |
| `firmware/patches.c` | the table, generated. Do not edit it |
| `firmware/patches.h` | the table's fields (hand-written; a test checks it against the converter) |
| `tests/test_jx8p.py` | the converter's tests (`make check-patches`, part of `make check`) |
| `tests/test_patches.c` | every patch fits the engine's ranges (`make test`) |

To regenerate the table after changing the converter: `python3 tools/jx8p.py`.
`make check-patches` fails while `firmware/patches.c` is stale.

## Where the data comes from

- File: `JX-8P_Factory_Patches_1_through_32.zip`, from the "Factory JX-8P Patches" entry of the
  Super JX Zone's patch page, <https://llamamusic.com/super-jx/patches.html>, which links it on
  OneDrive: <https://1drv.ms/u/s!Agw-RS0C2EHVjWVD0rQIHzcbZepa?e=rPrO3c>. Downloaded 2026-11-10.
- sha256 of the zip: `1ed096b200176c0482ba736286e5f5f4fd5c5aaafb384778441be3106117814e`.
- It holds one file, `FACTORY BANK 1-32.syx` (2496 bytes), checked in unchanged as
  `data/jx8p-factory-1-32.syx`, sha256
  `afdea5a1ada435dc2e705fe56eb9b2c110772889dc018bd1628d3e70bc37722b`.
- It is 32 pairs of messages: an APR (`F0 41 35 00 21 20 01`, 59 data bytes, `F7`) with the tone,
  then a program write (`F0 41 34 ...`) that stores it in memory slot 1..32. The converter reads
  the APRs and skips the rest.
- The names and order match the factory list (synthmania, <https://www.synthmania.com/jx-8p.htm>):
  PIANO 1 to PSYCHMELLO, SOUNDTRACK at 17.
- License: none is stated. The data is Roland's 1985 factory programming, shared by owners for
  decades. Eric decided to ship it; the source is recorded here.

## The JX-8P tone parameters

An APR's 59 data bytes, numbered 0..58, are the tone: bytes 0..9 the name (ASCII, 10 characters),
10 unused, then the parameters below. The numbers are the SysEx parameter numbers of F. Vecoven's
JX8P manual ("SYSEX IPR and CC parameters", <https://www.vecoven.com/jx8p/downloads8P/files/JX8P.pdf>),
which also gives the PG-800 numbers (11..94) shown in the second column. Bytes 23..25 and 57 are
unused (127 in most factory tones).

Every value is 0..127. The 4-position switches send 0, 32, 64 or 96: the position is the value
divided by 32. The 3-position ones (CHORUS, LFO WAVE) take 0..31, 32..63 and 64..127.

| Byte | PG-800 | Parameter | Values |
| --- | --- | --- | --- |
| 11 | 11 | DCO-1 RANGE | 16', 8', 4', 2' |
| 12 | 12 | DCO-1 WAVEFORM | noise, square, pulse, sawtooth (see below) |
| 13 | 13 | DCO-1 TUNE | -12..+12 semitones (see below) |
| 14 | 14 | DCO-1 LFO | depth 0..127 |
| 15 | 15 | DCO-1 ENV | depth 0..127 |
| 16 | 21 | DCO-2 RANGE | as 11 |
| 17 | 22 | DCO-2 WAVEFORM | as 12 |
| 18 | 23 | CROSS MOD | off, SYNC 1, SYNC 2, X-MOD |
| 19 | 24 | DCO-2 TUNE | as 13 |
| 20 | 25 | DCO-2 FINE TUNE | -50..+50 cents, 64 center |
| 21 | 26 | DCO-2 LFO | depth |
| 22 | 27 | DCO-2 ENV | depth |
| 26 | 31 | DCO DYNAMICS | off, 1, 2, 3 |
| 27 | 32 | DCO ENV MODE | ENV-2 inverted, ENV-2, ENV-1 inverted, ENV-1 |
| 28 | 41 | MIX DCO-1 | level |
| 29 | 42 | MIX DCO-2 | level |
| 30 | 43 | MIX ENV | DCO-2 level envelope depth |
| 31 | 44 | MIX DYNAMICS | off, 1, 2, 3 |
| 32 | 45 | MIX ENV MODE | as 27 |
| 33 | 51 | HPF | 0, 1, 2, 3 |
| 34 | 52 | VCF FREQ | cutoff |
| 35 | 53 | VCF RES | resonance |
| 36 | 54 | VCF LFO | depth |
| 37 | 55 | VCF ENV | depth |
| 38 | 56 | VCF KEY | key follow |
| 39 | 57 | VCF DYNAMICS | off, 1, 2, 3 |
| 40 | 58 | VCF ENV MODE | as 27 |
| 41 | 61 | VCA LEVEL | level |
| 42 | 63 | VCA DYNAMICS | off, 1, 2, 3 |
| 43 | 64 | CHORUS | off, I, II |
| 44 | 71 | LFO WAVEFORM | random, square, triangle |
| 45 | 72 | LFO DELAY | time |
| 46 | 73 | LFO RATE | rate |
| 47..50 | 81..84 | ENV-1 A, D, S, R | time, time, level, time |
| 51 | 85 | ENV-1 KEY | off, 1, 2, 3 |
| 52..55 | 91..94 | ENV-2 A, D, S, R | as ENV-1 |
| 56 | 95 | ENV-2 KEY | off, 1, 2, 3 |
| 58 | 62 | VCA MODE | gate (0..63), ENV-2 (64..127) |

Sources for the value ranges: the Vecoven manual (numbers), gravestench's jx8p_patcher definition
file (<https://github.com/gravestench/jx8p_patcher>, `patchDefinition.txt`: the switch ranges, the
cross mod and VCA modes), Roland's JX-08 reference (the JX-8P controls and their positions,
<https://static.roland.com/manuals/jx-08_reference/eng/17811863.html>), and the JX-8P owner's
manual (cross mod modes: sync, sync with cross mod, cross mod, off).

Two encodings are **inferred from the factory data**, not documented in any of these sources:

- **Waveform order.** jx8p_patcher lists 0 noise, 32 sawtooth, 64 pulse, 96 square, and the Kiwi-8P
  manual's CC map (for its own firmware) the same. But with that order the factory strings, brass,
  organs and pianos would all be square waves and SQUARELEAD a sawtooth. Roland lists the
  positions sawtooth, pulse, square, noise from the top, so the converter takes 0 noise, 32
  square, 64 pulse, 96 sawtooth: then the brass and strings are saws and SQUARELEAD is square.
- **TUNE.** 25 semitone steps in 0..127, but not evenly: FAT FIFTH's DCO-2 (97) and ORGAN 1's (96)
  must be a fifth (+7) and the unisons of LO STRINGS (66 and 60), HI STRINGS (69 and 59) and PIPE
  ORGAN (58 and 58) unisons, which no even spacing gives. The converter takes value / 4 with a dead
  band at the center: 56..71 is 0, then one semitone per 4 (72..75 is +1, 96..99 is +7) up to +12,
  and the same down to -12.

## How each parameter maps

Engine units are those of `engine/fm1_8p.h` (cents, ms, per mille, modes). Curves that are an
estimate, not a measurement, are marked *est.*; Eric's listening against PG-8X decides them.

| JX-8P | Engine field | Mapping |
| --- | --- | --- |
| RANGE + TUNE | `osc1_octave`, `osc2_octave`, `osc2_coarse_semis` | 16' is octave -1, 8' 0, 4' +1, 2' +2; whole octaves of range + tune go to the octave, the rest to coarse. DCO-1 has no coarse in the engine yet: osc1 plays the whole octave nearest its pitch that leaves osc2 in range, and osc2 moves by what osc1 lacks, so the interval holds and only the whole patch shifts: MUSIC BOX 4 semitones down, WINDCHIMES 9 down. `PATCH_OSC1_COARSE_SEMIS` keeps that amount |
| WAVEFORM | `osc1_wave`, `osc2_wave` | noise, square, pulse (25 % wide, *est.*), saw |
| FINE TUNE | `osc2_fine_cents` | (value - 64) x 50 / 64 |
| MIX DCO-1, DCO-2 | `osc_mix_permille` | DCO-2 / (DCO-1 + DCO-2), with MIX ENV's envelope at rest; the overall level is lost |
| MIX ENV, MIX ENV MODE | `osc_mix_env_permille`, `osc_mix_env_source` | the envelope scales DCO-2's level from 1 - depth (at rest) to 1 (at the peak), the other way when inverted (*est.*); `osc_mix_env_permille` is the mix's change from rest to peak. It follows the amp envelope for ENV-2 when the VCA follows ENV-2, else the filter envelope (exact when VCF ENV MODE names the same envelope. When MIX ENV MODE names ENV-1 and VCF ENV MODE ENV-2, no engine envelope has ENV-1's times: the mix follows the filter envelope with ENV-2's, an approximation in PIANO 1, PIANO 3, TIMBALES, WINDCHIMES, E. BASS, SOFT BRASS and CHURCHBELL until #59). 19 of the 32 tones use MIX ENV at full depth on ENV-1, so DCO-2 swells in with the filter envelope. With DCO-1 at 0 the mix is osc2 only (the engine has no silent mix) |
| CROSS MOD | `osc_sync` | on for SYNC 1 and SYNC 2. The JX-8P's X-MOD is reportedly amplitude modulation (ring-mod-like), not the engine's FM (`osc_xmod`), so it waits in `PATCH_XMOD_AM` (SYNC 2 and X-MOD) |
| HPF | `highpass_step` | position 0..3 |
| VCF FREQ | `filter_cutoff_cents` | 0.2173 x value^2 + 95.87 x value - 807.5 cents from MIDI note 0 (the step widens from 1.17 to 1.4 semitones), fitted to PG-8X 2.0.13 (`match.py --param VCF_FREQ`, 2026-11-10, no key follow, C4): within 0.2 semitone over 40..104. Below 9 it clamps to 0 (8.2 Hz). PG-8X's key follow pivots at C2 and tracks up to 1.28 octaves an octave, ours at C4 and 1.0: engine work, #95 |
| VCF RES, VCF KEY | `filter_resonance_permille`, `filter_key_track_permille` | value x 1000 / 127 |
| VCF ENV, VCF ENV MODE | `filter_env_amount_cents`, `filter_env_*` | 129 cents per step (127 is 16383 cents), fitted to PG-8X 2.0.13 (`match.py --param VCF_ENV`: within 5 % over 8..48), negative when inverted; the filter envelope takes the ADSR of the envelope the mode names (ENV-1 or ENV-2) |
| ENV-2, VCA MODE | `amp_*` | ENV-2's ADSR. GATE: attack 1 ms, sustain full, release 5 ms, and `PATCH_AMP_GATE` waits for #63 |
| ENV times | ms | cubic in the value: attack 1..3000 ms, decay and release 2..10000 ms (*est.*). Sustain: value x 1000 / 127 |
| VCA DYNAMICS | `amp_velocity_pct` | off 0, 1 33, 2 67, 3 100 % |
| CHORUS | `chorus_mode` | off, I slow, II deep (the factory tones use off and I only) |
| LFO WAVEFORM | `lfo_wave` | random, square, triangle |
| LFO RATE | `lfo_rate_centihz` | 0.1..20 Hz, exponential (*est.*) |
| LFO DELAY | `lfo_delay_ms` | cubic, 0..3000 ms (*est.*) |
| DCO-1 LFO | `lfo_pitch_cents` | quadratic to 1200 cents (*est.*). The engine has one vibrato depth, which moves both oscillators, so DCO-1's sets it; DCO-2's waits in `LFO_PITCH_OSC2_CENTS` (CELLO SECT wobbles DCO-2 only on the JX-8P) |

## What waits for the engine

These fields are in every patch now and do nothing until their feature lands. `patch_param()`
returns `PATCH_NONE` for them; when a feature lands, its field gets the new `fm_param_id` in
`FIELDS` (`tools/jx8p.py`) and the table is regenerated.

| Field | From | Waits for |
| --- | --- | --- |
| `PITCH_ENV_OSC1_CENTS`, `PITCH_ENV_OSC2_CENTS` | DCO-1 ENV, DCO-2 ENV: quadratic to 2400 cents (*est.*), signed by DCO ENV MODE | #58 pitch envelope |
| `PITCH_ENV_SOURCE`, `PITCH_ENV_VELOCITY_PCT` | DCO ENV MODE (0..3), DCO DYNAMICS | #58 |
| `FILTER_ENV_SOURCE` | VCF ENV MODE (0..3) | #59 envelope source and invert |
| `ENV1_KEY_FOLLOW`, `ENV2_KEY_FOLLOW` | ENV-1 KEY, ENV-2 KEY (0..3) | #60 envelope key follow |
| `AMP_GATE` | VCA MODE | #63 amp gate |
| `OSC_MIX_ENV_VELOCITY_PCT` | MIX DYNAMICS | velocity on the mix envelope (no issue yet) |
| `FILTER_LFO_PERMILLE`, `FILTER_VELOCITY_PCT` | VCF LFO, VCF DYNAMICS | #8 (LFO to cutoff, filter velocity) |
| `XMOD_AM` | CROSS MOD SYNC 2 and X-MOD | the JX-8P's cross mod (no issue yet) |
| `OSC1_COARSE_SEMIS` | DCO-1 TUNE | DCO-1 coarse tune (no issue yet) |
| `AMP_LEVEL_PERMILLE` | VCA LEVEL | a patch level (no issue yet) |
| `LFO_PITCH_OSC2_CENTS` | DCO-2 LFO | a vibrato depth per oscillator (no issue yet) |

Aftertouch (#61) and portamento (#62) are not tone data on the JX-8P: they are panel settings
(the AFTER TOUCH buttons and the PORTAMENTO switch and knob), so no factory tone carries them, and
the table has no fields for them.

## Storage

Each patch is its name (12 characters and the end) and one `uint16_t` per field, 46 fields:
106 bytes, 3392 bytes for the 32. A value is stored with a bias of 32768 (`patch_value()` takes
it off): stored as plain `int16_t`, a value of -64..-49 next to another can make a 32-bit word
in 0xFFC00000..0xFFCFFFFF, which fm1-dock rejects as a ROM address (`make check-float-free`
catches it on the desktop).
