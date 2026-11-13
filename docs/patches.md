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
| MIX DCO-1, DCO-2 | `osc1_level_permille`, `osc2_level_permille` | each byte over 127; the DCOs sum, as PG-8X 2.0.13's mixer does (measured, #102) |
| VCA LEVEL | `amp_level_permille` | 0.30 dB a step below 127, linear in dB down to 0 (−38.1 dB, not silent), as PG-8X 2.0.13 plays it (measured within 0.1 dB at 127, 112, 96, 88, 64, 32, 16, 8, 4, 1, 0). It scales the whole note, read at note on with the velocity |
| MIX ENV, MIX ENV MODE | `osc_mix_env_permille`, `osc_mix_env_source` | depth x envelope is added to DCO-2's level byte (taken away when inverted), clamped to 0..127, as PG-8X 2.0.13 plays it (measured, #102); `osc_mix_env_permille` is the depth over 127, signed. It follows the amp envelope for ENV-2 when the VCA follows ENV-2, else the filter envelope, which runs ENV-1's own times (#59). Exact except with the VCA on GATE, where ENV-2 runs nowhere and the filter envelope takes ENV-2's times for a VCF that names it: E. BASS's MIX ENV (ENV-1) then follows ENV-2's times. 19 of the 32 tones use MIX ENV at full depth on ENV-1, so DCO-2 swells in with the filter envelope and holds at its own level plus the sustain's share. With DCO-1 at 0 osc1 is off |
| CROSS MOD | `osc_sync`, `osc_ring` | `osc_sync` on for SYNC 1 and SYNC 2; `osc_ring` (`PATCH_XMOD_AM`) on for SYNC 2 and X-MOD. The ring is PG-8X 2.0.13's measured law (#105), not the engine's FM (`osc_xmod`): DCO-2's signal becomes x2 (a + b m1) + c m1 before its level into the sum, m1 being DCO-1's wave at peak 1 (b near -1: nearly full AM by an inverted DCO-1). a / b / c are 1.12 / -0.85 / 0.235 for a DCO-1 saw, 1.66 / -1.0 / 0.15 for pulse (fit at PG-8X's 2-3 % pulse), 0.84 / -0.83 / 0.21 for square, and 1.0 / -1.0 / 0 for noise (not measured); b is fitted against the clean wave, as PG-8X's peaks overshoot its plateau. Held to +-2.0, and so is its sum with DCO-1. A product, so no pitch moves; it is PG-8X's +1.9 dB on SYNC 2 and X-MOD. With MIX DCO-2 at 0 it is silent |
| HPF | `highpass_step` | as PG-8X 2.0.13 plays it (2026-11-10 probe, noise against its HPF 1): position 1 is flat, so step 0; 2 and 3 are steps 2 and 3 (PG-8X -17.2 / -27.6 dB at 30 Hz, ours -18.4 / -25.9; within about 2 dB from 60 Hz up). Position 0 is a bass boost on PG-8X (+6.5 dB at 30 Hz, +2 dB from 500 Hz up) the engine lacks, so step 0 (flat) for now: #93. Step 1 (100 Hz) is not used by the converter |
| VCF FREQ | `filter_cutoff_cents` | 0.0988 x value^2 + 115.03 x value - 1474 cents from MIDI note 0 (about 1.2 to 1.4 semitones a step), fitted so the engine's resonant peak lands on PG-8X 2.0.13's (filter sweep 2026-11-11, VCF RES 96-127, noise in, no key follow, C4): within 0.45 semitone over 48..112, the same at each RES. Below 13 it clamps to 0 (8.2 Hz). VCF KEY adds to it (below). The engine keeps the peak the same distance under the cutoff up to near Nyquist (`engine/filter.c`, warp). PG-8X's knee is sharper, so by `match.py --param VCF_FREQ`'s asymptote reading the engine's cutoff reads lower than PG-8X's at the same peak |
| VCF RES | `filter_resonance_permille` | a table (`tools/jx8p.py` `RESONANCE_PERMILLE`, linear between its points) fitted to PG-8X 2.0.13's peak over the passband (`match.py --param VCF_RES`, VCF FREQ 64, 2026-11-10): 64 is 400 (+4 dB), 80 575 (+8), 96 795 (+16), 112 900 (+24), 127 935 (+31). It keeps PG-8X's hump: +6.5 dB at 32, down to +3.7 at 56 (likely PG-8X's own curve, but it is the reference). The filter matches PG-8X's shape with resonance too: the peak's place under the cutoff, the passband level and the treble against the bass (docs/fixed-point.md, Filter, #98) |
| VCF KEY | `filter_key_track_permille`, `filter_cutoff_cents` | value x 1290 / 127 per mille: up to 1.29 octaves an octave, as PG-8X 2.0.13 (#95). PG-8X pivots at C2 and the engine at C4, so the cutoff also gets the track's rise from C2 to C4 (track x 2.4 cents): every note then gets PG-8X's cutoff |
| VCF ENV, VCF ENV MODE | `filter_env_amount_cents`, `filter_env_source`, `filter_env_*` | 129 cents per step (127 is 16383 cents), fitted to PG-8X 2.0.13 (`match.py --param VCF_ENV`: within 5 % over 8..48), negative when inverted. The cutoff follows the amp envelope (`filter_env_source` 1) for ENV-2 when the VCA follows ENV-2, else the filter envelope. The filter envelope runs ENV-1's ADSR, except with the VCA on GATE and the mode naming ENV-2, where it runs ENV-2's (ENV-2 runs nowhere else until #63) |
| ENV-2, VCA MODE | `amp_*` | ENV-2's ADSR. GATE: attack 1 ms, sustain full, release 5 ms, and `PATCH_AMP_GATE` waits for #63 |
| ENV times | ms | attack: the time to the peak of an RC charge toward 1.6 x the peak (PG-8X 2.0.13's shape at every value, fitted within 0.4 % of level): 14.1821 x (2.6607^(value / 16) - 0.4954) ms, within 4 % of PG-8X from 40 up and 11 % at 16-32 (2026-11-11: an RC fit of the rise, |analytic signal| of a held C5 under a 5 ms moving median, which reads ours within 1 % of its setting; PG-8X 16 is 29 ms, 32 98, 64 687, 80 1854, 96 5000, 112 13505, 127 34190 ms). At most 32 s, the envelope range (the patch table's int16), so 127 plays 6 % short. Below 8 it is 1 ms (PG-8X under 2 ms; at least 1 ms, as GATE: a ramp, not a click); 8-15 are PG-8X's steps (3, 4, 6, 8, 11, 14, 18, 23 ms). Decay and release: exponential on PG-8X (t_40dB = 2 x t_20dB at every value), so the engine's decay and release times are the time constant tau = t_20dB / ln 10: 9.884 x (2.6717^(value / 16) - 0.1327) ms, fitted to PG-8X 2.0.13 (`match.py --param ENV2_DECAY / ENV2_REL`, 2026-11-10: tau 16 is 26 ms, 32 74, 48 187, 64 506, 80 1333, 96 3589, 112 9644, 127 24468 ms; release the same within 5 %), within 7 % from 16 up. Below 16 PG-8X steps in pairs and leaves the curve (sample-accurate probe, 2026-11-11): its decay floors near 4 ms tau, its release goes down to about 1 ms, so `decay_ms` and `release_ms` take its measured steps there (decay 4, 4, 4, 4, 4, 5, 7, 12 ms; release 1, 1, 2, 3, 4, 5, 9, 13 ms for bytes 0-1, 2-3 ... 14-15). Sustain: value x 1000 / 127 |
| ENV-1 KEY, ENV-2 KEY | `filter_env_key_follow_permille`, `amp_key_follow_permille` | as PG-8X 2.0.13 plays them (#60): every stage time x 2^(-follow x octaves above C2), longer below C2; 260 per mille per position (PG-8X measures 0.27, 0.52 and 0.77 an octave on attack, decay and release alike). The amp envelope takes ENV-2 KEY (0 on GATE); the filter envelope the KEY of the JX-8P envelope whose times it runs |
| VCA DYNAMICS, MIX DYNAMICS | `amp_dyna`, `osc_mix_env_dyna` | the position, 0 off .. 3: as PG-8X 2.0.13 plays them, the VCA's level, and the MIX ENV depth, times the DYNAMICS share at the note's velocity (`engine/dyna.c`, the curve measured for #8). PG-8X's v40 re v127 is that share within 0.1 dB at every position (VCA -4.1 / -10.2 / -15.5 dB, MIX -2.3 / -5.3 / -6.8). `amp_velocity_pct` is left at 0 |
| CHORUS | `chorus_mode` | off, I slow, II deep (the factory tones use off and I only); both play PG-8X 2.0.13's chorus: 0.68 Hz, dry plus wet (+3 dB), L/R correlation about 0.46 (#104) |
| LFO WAVEFORM | `lfo_wave` | random, square, triangle |
| LFO RATE | `lfo_rate_centihz` | as PG-8X 2.0.13 plays it: 0.537 x 2.664^((value - 64) / 16) Hz, so 0.076 Hz at 32, 0.54 Hz at 64, 3.8 Hz at 96 and 25.4 Hz at 127. Below 32 PG-8X flattens near 0.05 Hz (0.050 Hz measured at 16, 0 unmeasured): the curve stops at 0.05 Hz, which it reaches at 25, and holds it down to 0 |
| LFO DELAY | `lfo_delay_ms` | cubic, 0..3000 ms (*est.*) |
| DCO-1 LFO, DCO-2 LFO | `lfo_pitch_cents`, `lfo_pitch_osc2_cents` | as PG-8X 2.0.13 plays them: linear, 4.5 cents x value (127 = 572 cents), each on its own oscillator (CELLO SECT wobbles DCO-2 only) |
| DCO-1 ENV, DCO-2 ENV | `pitch_env_osc1_cents`, `pitch_env_osc2_cents` | as PG-8X 2.0.13 plays them (#58): at full envelope 24.8 cents x value less 13 (0 at 0, 3137 at 127), linear in the envelope's level, the same on every key, each on its own oscillator. Signed by DCO ENV MODE: inverted is the exact mirror. With sync on, DCO-1's moves the heard pitch and DCO-2's only the timbre, as on PG-8X |
| DCO ENV MODE | `pitch_env_source` | ENV-1: the filter envelope; ENV-2: the amp envelope when the VCA follows ENV-2, else the filter envelope (as MIX ENV MODE) |
| DCO DYNAMICS | `pitch_env_dyna` | the position, 0 off .. 3: the depth times the DYNAMICS curve's share at the note's velocity (`engine/dyna.c`) |
| (none) | `setting_glide` | 1 for every tone: PG-8X 2.0.13 glides its levels, filter and LFO depths to a new value as a one-pole of about 16 ms (measured mid-note through DawDreamer automation: VCA LEVEL 15.9 ms, MIX DCO-2 16.7 / 15.7, VCF FREQ 16.4 / 15.9; VCF ENV, KEY and RES alike; the LFO depth about 20; DCO tune, DCO ENV, ENV-2 SUS, VCA DYNA and HPF do not glide). The engine glides osc 1 and 2's levels, the cutoff (in cents), resonance, key track, filter envelope depth and both LFO pitch depths, from PG-8X's power-on values (DCO-1 full, the filter open) when a patch is set before the first render (#146) |

## What waits for the engine

These fields are in every patch now and do nothing until their feature lands. `patch_param()`
returns `PATCH_NONE` for them; when a feature lands, its field gets the new `fm_param_id` in
`FIELDS` (`tools/jx8p.py`) and the table is regenerated.

| Field | From | Waits for |
| --- | --- | --- |
| `AMP_GATE` | VCA MODE | #63 amp gate |
| `FILTER_LFO_PERMILLE`, `FILTER_VELOCITY_PCT` | VCF LFO, VCF DYNAMICS | #8 (LFO to cutoff, filter velocity) |
| `OSC1_COARSE_SEMIS` | DCO-1 TUNE | DCO-1 coarse tune (no issue yet) |

Aftertouch (#61) and portamento (#62) are not tone data on the JX-8P: they are panel settings
(the AFTER TOUCH buttons and the PORTAMENTO switch and knob), so no factory tone carries them, and
the table has no fields for them.

## Storage

Each patch is its name (12 characters and the end) and one `uint16_t` per field, 46 fields:
106 bytes, 3392 bytes for the 32. A value is stored with a bias of 32768 (`patch_value()` takes
it off): stored as plain `int16_t`, a value of -64..-49 next to another can make a 32-bit word
in 0xFFC00000..0xFFCFFFFF, which fm1-dock rejects as a ROM address (`make check-float-free`
catches it on the desktop).
