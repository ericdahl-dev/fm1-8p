# On-device UI: visual design

This document covers how fm1-8p looks on the FM-1's 240 x 240 screen. Eric's picks of
2026-11-10 settled the three main choices:

- **Two themes, switchable:** Membrane and Night Panel. There is one layout, and each theme is a
  table of colors and style choices, so switching theme is a data swap rather than a second code
  path.
- **A piano-roll sequencer** in both themes.
- **fm1-8p's own display font** for values and names. The host's Terminus stays for the small
  labels.

Eric's follow-up answers settled four more:

| decision | what it means |
| --- | --- |
| **Night Panel is the default theme** | a fresh device, or a settings record without THEME, starts in Night Panel |
| **Names are 12 characters at most** | patch and pattern names always fit the D20 focus row; SAVE's name editor stops at 12 |
| **SEQ view's footer shows the pattern** | `PTN 41 NIGHT DRIVE*`, and `NEXT 42` while one is queued; the patch shows again on leaving SEQ view |
| **THEME is live** | a turn applies at once, debounced by 150 ms (see [THEME in GLO](#theme-in-glo-and-what-a-switch-redraws)) |

What each control does is the interaction model's job, in [`interaction.md`](interaction.md)
(PR #51). This document follows it for:

- the section names (DCO-1, DCO-2, MIX, VCF, VCF ENV, VCA, LFO, CTRL);
- the two-digit parameter numbers, where the tens digit is the section (VCF is 41-44);
- the 12-page order;
- the split keyboard in SEQ view (F3..G#4 are the 16 step keys, and A4..G5 play chords);
- the separate pattern slots;
- the key LED rules;
- the per-state screen content of its section 6.

The look is inspired by a mid-80s two-oscillator polysynth panel and by synthwave. It copies no
logo, wordmark or panel artwork. The device is called "fm1-8p" on screen. The patches are the
JX-8P's 32 factory tones with their factory names (Eric's decision in #75; docs/patches.md); the
pattern names are invented, and the display font is an original design.

## Mockups

Each theme has five screens. Every screen comes at 240 x 240 and as a 3x copy (720 x 720) for a
phone. [`mockups/compare.png`](mockups/compare.png) shows all ten screens, one theme per row.

| screen | Membrane | Night Panel |
| --- | --- | --- |
| PLAY (patch) | [1x](mockups/membrane/1-play.png) / [3x](mockups/membrane/1-play-3x.png) | [1x](mockups/night-panel/1-play.png) / [3x](mockups/night-panel/1-play-3x.png) |
| EDIT 4A VCF | [1x](mockups/membrane/2-vcf.png) / [3x](mockups/membrane/2-vcf-3x.png) | [1x](mockups/night-panel/2-vcf.png) / [3x](mockups/night-panel/2-vcf-3x.png) |
| EDIT 5A VCF ENV | [1x](mockups/membrane/3-env.png) / [3x](mockups/membrane/3-env-3x.png) | [1x](mockups/night-panel/3-env.png) / [3x](mockups/night-panel/3-env-3x.png) |
| SEQ view (piano roll) | [1x](mockups/membrane/4-seq.png) / [3x](mockups/membrane/4-seq-3x.png) | [1x](mockups/night-panel/4-seq.png) / [3x](mockups/night-panel/4-seq-3x.png) |
| GLO, THEME setting | [1x](mockups/membrane/5-glo.png) / [3x](mockups/membrane/5-glo-3x.png) | [1x](mockups/night-panel/5-glo.png) / [3x](mockups/night-panel/5-glo-3x.png) |

The display font is shown in [`mockups/font-specimen-3x.png`](mockups/font-specimen-3x.png), and
its data is in [`mockups/display_font.h`](mockups/display_font.h).

`mockups/render.py` draws every screen with only what a module has: `fill`, lines, pixels,
`blit`, and the host's Terminus 8x16. The script reads Terminus from fm1-dock's
`firmware/gen/felucca_font.h`, and stops if that file is missing. It quantizes every pixel to
RGB565 the way the panel shows it, so the PNGs show colors the device can actually display. The
script also bakes the display font and writes `display_font.h`.

```sh
python3 docs/ui/mockups/render.py             # needs Pillow; fm1-dock at ~/Projects/fm1-8p-ref/fm1-dock, $FM1_DOCK or --font
python3 docs/ui/mockups/render.py --contrast  # also prints each theme's RGB565 values and contrast
```

The mockups show one moment in both themes:

- **Patch:** 23 CHROME DUSK, edited.
- **Pattern:** 41 NIGHT DRIVE, edited, playing at step 7, 112 BPM on the internal clock.
- **Focus:** on the edit pages, knob 2 is the focus (42 RESO, 52 DECAY). On GLO the focus is
  G34 THEME.
- **SEQ view:**
  - page S2;
  - the cursor on step 5, a C minor 7 chord, tied into step 6;
  - A4 C5 E5 held on the chord keys;
  - pattern 42 queued next.

## One layout

All the screens share one grid, the `LAYOUT` dict in `render.py`, which every screen function
reads its bands from. Every position below is the same in both themes.

| band | rows | contents |
| --- | --- | --- |
| header | 0-23 | the state or section chip, its context, and the transport on the right: a play or stop mark, step, tempo and clock source (`07 112 INT`). On EDIT pages a 14-segment strip on the header's bottom rule shows where the page sits in interaction.md's page order (`PAGE_ORDER` in `render.py`: 1A, 2A, 2B, 3A, 4A, 4B, 5A, 5B, 6A, 6B, 7A, 7B, 8A, 8B, so 4A VCF lights the 5th segment and 5A VCF ENV the 7th), and on GLO a 3-segment strip does the same. |
| focus | 28-73 | a 64 x 46 LED window with the parameter number, then the section name, the full label, and the value in D24 with its unit |
| slot | 78-133 | a single 230 x 56 canvas. It holds the filter response (VCF), the ADSR (VCF ENV and VCA), the art (PLAY) or the theme cards (GLO). |
| knob columns | 138-221 | four 56 x 84 columns with 2 px gaps, left to right for KNOB1-4. Each column holds the number, the label, a meter, and the value in D24 with its unit. |
| footer | 224-239 | the patch number and name with `*` when edited, and the pattern number on the right. A 1.5 s banner (`SAVED 23`, and so on) replaces the footer. |

The SEQ view keeps the header and footer, and uses its own body (see the piano roll below).

- **Margins and widths:** the outer margin is 5 px. Labels are at most 7 characters, which
  is 56 px in Terminus, the width of a column.
- **The PLAY screen** uses the same bands:
  - the LED window shows the patch number;
  - the focus row shows the name in D20;
  - the slot holds the art;
  - the columns are the four performance knobs: 41 CUTOFF, 42 RESO, 43 ENV and 64 RELEASE, each
    in its own section's color.

  After a turn, the focus readout takes the name row for 3 s.
- **Values:**
  - a column shows the value in D24 with its unit in Terminus (`1.25k`, `480ms`, `+24st`);
  - the focus row shows the unit in full (`1.25 kHz`);
  - a choice too long for D24 in 56 px, such as `MEMB` or `NIGHT`, falls back to Terminus.
- **Inactive and pending values:** an inactive value, such as 14 WIDTH when WAVE isn't PULSE,
  is drawn in `dim` and has no meter. A pending parameter shows `--`. A reserved slot leaves
  its column empty.

## Two themes: the style table

A theme is one table. These are all the keys, so they are also everything that differs between
the two themes:

| key | Membrane | Night Panel | what it changes |
| --- | --- | --- | --- |
| `bg` | `18E4` | `0863` | the screen |
| `surface` | `2145` | `18C5` | columns, footer, cells |
| `surface_hi` | `31A7` | `2107` | the focus column, legend chips |
| `line` | `3A09` | `2969` | rules, frames, unlit meter |
| `text` | `EF3A` cream | `EF7F` | values, focused labels |
| `dim` | `A513` | `9CB7` | other labels |
| `ink` | `1082` | `0863` | text on a chip or on the sunset |
| `window` | `0861` | `0021` | LED and graph windows |
| `led` | `F984` red | the section color | the 7-segment digits |
| `ghost` | 10 % | 8 % | unlit segments, as this much of the LED color |
| `header` | `chip` | `band` | the section name on a chip of its color, or in its color under a 3 px band |
| `meter_segments`, `meter_gap` | 0, 0 (one bar) | 8, 2 (an LED ladder) | the knob meter |
| `focus` | `(1.0, 1.0)` | `(1.0, 0.45, 0.2)` | frames inward from the focused element's edge, as mixes from `bg` to the accent: a 2 px frame, or a glow |
| `hatch` | 0 | 16 % | the fill under graph curves |
| `art` | `badge` | `sun` | the PLAY slot: six sunset bands with the device name, or the striped sun over a perspective grid |
| `playhead`, `cursor` | `EF3A`, `F984` | `2EDF`, `EF7F` | the SEQ playhead and step cursor |
| `sun` | `FE88 FD87 FC67 FB49 F24E D1D2` | `FEC9 FD88 FC48 FAEC F1F2 C198` | the sunset bands (PLAY only) |
| `grid`, `horizon` | none | `5953`, `F9FB` | the perspective grid (PLAY only) |

All values are RGB565, which is what `HB_RGB()` produces.

### Section colors

| section | Membrane | Night Panel |
| --- | --- | --- |
| 1x DCO-1 | `EC07` orange | `FAF4` pink |
| 2x DCO-2 | `EDC7` amber | `FC47` orange |
| 3x MIX | `CE15` stone | `DEBE` lilac white |
| 4x VCF | `4D9A` steel blue | `2EDF` cyan |
| 5x VCF ENV | `DB53` rose | `FE07` amber |
| 6x VCA | `8E4C` green | `7F8F` green |
| 7x LFO | `9C5C` lavender | `B3FF` violet |
| 8x CTRL | `5657` teal | `3D1F` blue |
| SEQ | `FB4D` red | `F9FB` magenta |
| GLO | `CE15` stone | `DEBE` lilac white |
| PLAY | `EF3A` cream | `F9FB` magenta |

### Contrast

All ratios are WCAG ratios, computed on the quantized RGB565 values.

- **Text:** at least 10.2:1 on every surface in Membrane, and 13.9:1 in Night Panel.
- **Dim labels:** at least 4.9:1 in Membrane and 5.6:1 in Night Panel.
- **Section colors:** at least 4.7:1 on `surface` and 4.9:1 on `bg`.

### Where the layouts differ, and why it stays minimal

The theme changes only the following, and none of it moves anything:

1. **The meter.** Membrane draws one continuous bar and Night Panel draws an 8-segment ladder.
   Both are the same `meter()` call in the same 44 x 6 box, with a segment count from the table
   (0 means continuous). A choice parameter always draws one segment per choice in both themes.
2. **The header.** Membrane puts the name on a chip and Night Panel puts a band over colored
   text. Both are 24 px tall, and the text starts at the same row.
3. **Focus.** Membrane draws a 2 px frame and Night Panel draws a 3-step glow. Both are frames
   drawn inward from the column's edge, at most 12 fills.
4. **The PLAY art.** Membrane fills the slot with the badge and Night Panel with the sun. Both
   fill the same slot and are drawn once.

The text sizes, the positions, the fonts, the 7-segment window, the graphs, the piano roll and
the key strip are identical.

### THEME in GLO, and what a switch redraws

THEME takes the reserved slot G34 (GLO page 3, K4): `MEMBRANE`, `NIGHT`. The default is Night
Panel, and the setting is one byte in the settings record.

| No. | Knob | Setting | Shown as | Default |
| --- | --- | --- | --- | --- |
| G34 | K4 | THEME | MEMBRANE, NIGHT (the column shows `MEMB` or `NIGHT`, and the focus row the full name) | NIGHT |

- **The GLO slot shows a card for each theme,** each drawn in its own colors, with the chosen
  card framed. You see both before you turn.
- **A turn applies at once.** It changes the theme pointer and calls `draw(full)` once:
  - everything is redrawn, about 57.6 k pixels;
  - that includes the slot art the next time PLAY shows;
  - it is debounced by 150 ms, so a fast turn back and forth redraws once.
- **The LEDs do not change.** They are one color each in hardware, and their states don't depend
  on the theme.
- **The setting is saved** 2 s after the last change, once nothing sounds, as GLO's other
  settings are.

## The display font

The display font is an original geometric face, upper case only. Glyphs are strokes on a grid 4
units wide and 6 units tall (I is 2 units wide, M and W are 5), with 45° chamfers instead of
curves. A square pen draws the strokes and the result is baked to 1 bit per pixel, so both sizes
come from one design. It has 45 characters: space, `#%+-./:`, 0-9 and A-Z.

Pen positions round half toward the glyph's middle (`floor(v + 0.5)` below it, mirrored above
it), never Python's half-to-even, so mirrored strokes land on mirrored pixels. A bar exactly on
the middle row of an odd span (H, E, F, A in D24 and D20) is drawn one row thicker, because a
3 px pen has no centered place there. `render.py` checks on every run that O and H are
symmetric top to bottom and that 0 is symmetric under a half turn.

| size | use | cap height | pen | digit width | advance |
| --- | --- | --- | --- | --- | --- |
| D24 | values: column values, the focus value | 24 px | 3 px | 11 px | 13 px |
| D20 | names: patch name, theme name, chord name | 20 px | 3 px | 11 px | 13 px |

The 3-digit value `480` is 37 px wide, so it fits a 56 px column with a Terminus unit after it.
The longest names fit the focus row's 158 px: `NIGHT PANEL` takes 136 px. Names are at most
12 characters (decided), which always fit.

- **Flash: 3960 bytes for both sizes,** under the 6 KB budget. D24 takes 2016 bytes of bitmaps
  plus 132 bytes of tables (2148). D20 takes 1680 plus 132 (1812).
- **Format (`display_font.h`):**
  - `DFONT_CHARS` lists the characters, and a glyph's index is its place in it;
  - per glyph, a width `W[i]` (u8) and an offset `OFF[i]` (u16) into `DATA`;
  - the bitmap is 1 bit per pixel, MSB first, `ceil(w / 8)` bytes per row, and `H` rows;
  - the advance is the width plus the size's gap, `D24_GAP` or `D20_GAP` (2 px each);
  - lower case folds to upper case, and any other character draws as a space.
- **Drawing it: expand, then one `blit`.** The module expands the text into a RGB565 row buffer
  the size of its box, high byte first as `blit` takes it, filled with the background, with
  each set bit written in the foreground. Then it makes one `blit`.
  - A 56 x 24 column value is 1344 pixels: a 2.7 KB buffer and one blit.
  - A 158 x 24 focus value is 7.6 KB.
  - Expanding is a few thousand cycles in the main loop, never in the audio interrupt.
  - The alternatives cost more. `cv_pixel` would be one host call per lit pixel, about 400 for
    a value. A fill per run would be about 100 host calls. One blit replaces the old value
    without flicker, because the background is part of it.
- **The host's Terminus (16 px)** stays for the small labels, numbers, units, the footer and the
  note names. It is the only font with lower case (`Eb3`, `ms`).
- **The 7-segment window stays fills-only.** It shows 2 digits at 16 x 36 with a 4 px stroke,
  ghost segments, and a 1 px cut between segments. It costs at most 7 fills per digit, and only
  the changed segments are redrawn.

## The piano-roll sequencer (SEQ view)

The SEQ view follows `interaction.md`'s SEQ list, top to bottom:

| rows | what |
| --- | --- |
| 0-23 | `SEQ`, the SEQ page (`S2 STEP`), the transport |
| 26-97 | the roll: 16 step columns of 14 px, pitch upward. The range fits the pattern's notes, at least an octave. Alternate groups of 4 steps are shaded as beats, with C lines. |
| 101-153 | the cursor step: its number in the LED window, the chord name in D20 (`C MIN7`), the notes in Terminus (`C3 Eb3 G3 Bb3`), `TIE > 6`, and `HELD` with the chord held now |
| 156-187 | the S1, S2 or S3 page's four knob values, one cell per knob |
| 191-206 | what the section buttons mean here: `SEL REST`, `ENV TIE`, `LFO MOVE`, and `FX BACK` in REC (`--` otherwise) |
| 208-222 | the 27-key strip (rows 208-219, its 2 px underline at 221-222, clear of the footer's rule at 223), mirroring the key LEDs. The step keys F3..G#4 are underlined in the SEQ color and the chord keys A4..G5 in dim, so the split is visible. |
| 224-239 | the pattern number and name with `*`, and `NEXT 42` while a pattern is queued |

The steps are drawn like this:

- **A note** is a 3 px block at its pitch.
- **A chord** is a stack of blocks.
- **A tie** runs the held step's blocks on through the gap at 45 %.
- **A rest** is an empty column.
- **Steps past the length** are drawn as `window` with no blocks.
- **The playhead** shades its column in the playhead color, adds a 1 px center line, and
  draws its notes in the playhead color.
- **The cursor** gets a 1 px frame in `cursor`.

The patch number is not in SEQ view's footer (decided). PRESETS browses patterns there, so the
pattern takes the footer. The patch shows in every other state's footer.

**Redraw cost.** Each step column is its own 14 x 72 canvas. A playhead move redraws only:

- the two columns, the old step and the new (2 x 1008 pixels);
- the header's step number (16 x 16);
- two LED dots on the key strip (two fills).

That is about 2.3 k pixels per step. At 112 BPM in 16ths, which is 7.5 steps a second, it comes
to about 17 k pixels a second, about 30 % of one full-screen redraw. The roll is never redrawn
whole while it plays.

- **A step edit** redraws that column and the readout.
- **A held chord** redraws the `HELD` line and the key dots.
- **A pattern change** redraws the roll (16 columns) once.

## LEDs

The key and button LEDs follow `interaction.md` section 5, "Key LEDs". Each LED is one color in
hardware, which the theme cannot change, and has three states: off, lit and glow. The screen
mirrors them in two places:

- **The SEQ key strip** draws every key's state. Lit is the SEQ color, glow is a pale tint, and
  off is the key's own color. The mockups show a still frame: blinking states (a tie's off
  blink, the cursor's 4 Hz blink) show their lit phase.
- **The edit pages** light the section button that owns the shown page, and the 14-segment page
  strip agrees with it.

The meaning stays the same in both themes:

- **lit** is where you are or what is on;
- **glow** is what you can press now;
- **off** is nothing to do.

## Redraw rules (both themes)

- **`draw(full)` only on a page change, a theme change, or a return from the host's picker.**
  After that, `draw()` updates only what changed, tracked as "drawn" copies of each value, as
  Scintilla and Prophet do.
- **Coalesced knob turns.** A turn marks its column dirty. Dirty items redraw at most every
  33 ms (30 Hz), and fast turns coalesce into one redraw.
- **A value change** costs:
  - a 56 x 24 blit for the value;
  - at most 8 meter fills;
  - a 158 x 24 blit for the focus value;
  - the slot canvas (230 x 56, about 12.9 k pixels), only when the parameter shapes the graph:
    41 and 42 for the filter response, 51-54 and 61-64 for the ADSR.
- **The sun, the grid and the badge** are drawn once, on entering PLAY: about 90 row fills
  and 25 lines. A patch change redraws only the name and the LED digits. No frame is ever a
  full-screen gradient.
- **Every gradient is deliberate, hard-edged bands,** one fill per band, so RGB565 banding is
  part of the look and costs nothing per frame.
