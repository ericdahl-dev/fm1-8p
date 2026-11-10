# On-device UI: visual directions

This document covers how fm1-8p looks on the FM-1's 240 x 240 screen and how its LEDs behave.
It sets out three directions, each with four mockup screens, and recommends one (**C, Night
Panel**). What each control does is the interaction model's job (`docs/ui/interaction.md`).
Where this document assumes something about controls, such as page order, parameter numbers or
which keys are the 16 steps, it says so. Those assumptions give way to the interaction model.

The look is inspired by a mid-80s two-oscillator polysynth panel and by synthwave. It copies no
logo, wordmark, panel artwork or factory patch name. The device is called "fm1-8p" on screen,
and every patch name here is invented.

## Mockups

`docs/ui/mockups/render.py` draws every screen with only what the fm1-dock host gives a module:
`fill`, lines, pixels, the host's own fonts and `blit`. It then quantizes each screen to RGB565
the way the panel shows it, so the PNGs show colors the device can actually display.

```sh
python3 docs/ui/mockups/render.py            # needs Pillow and fm1-dock's firmware/gen/felucca_font.h
python3 docs/ui/mockups/render.py --contrast # also prints the contrast and RGB565 tables
```

The script finds the font at `~/Projects/fm1-8p-ref/fm1-dock`, or at `$FM1_DOCK`, or at the path
given with `--font`. If the font is missing, the script stops rather than draw in a different
font.

| direction | patch | section page (VCF) | envelope (ENV-1) | sequencer |
| --- | --- | --- | --- | --- |
| A Membrane | [1x](mockups/a-membrane/1-patch.png) / [3x](mockups/a-membrane/1-patch-3x.png) | [1x](mockups/a-membrane/2-vcf.png) / [3x](mockups/a-membrane/2-vcf-3x.png) | [1x](mockups/a-membrane/3-env.png) / [3x](mockups/a-membrane/3-env-3x.png) | [1x](mockups/a-membrane/4-seq.png) / [3x](mockups/a-membrane/4-seq-3x.png) |
| B Outrun | [1x](mockups/b-outrun/1-patch.png) / [3x](mockups/b-outrun/1-patch-3x.png) | [1x](mockups/b-outrun/2-vcf.png) / [3x](mockups/b-outrun/2-vcf-3x.png) | [1x](mockups/b-outrun/3-env.png) / [3x](mockups/b-outrun/3-env-3x.png) | [1x](mockups/b-outrun/4-seq.png) / [3x](mockups/b-outrun/4-seq-3x.png) |
| C Night Panel | [1x](mockups/c-night-panel/1-patch.png) / [3x](mockups/c-night-panel/1-patch-3x.png) | [1x](mockups/c-night-panel/2-vcf.png) / [3x](mockups/c-night-panel/2-vcf-3x.png) | [1x](mockups/c-night-panel/3-env.png) / [3x](mockups/c-night-panel/3-env-3x.png) | [1x](mockups/c-night-panel/4-seq.png) / [3x](mockups/c-night-panel/4-seq-3x.png) |

[`mockups/compare.png`](mockups/compare.png) shows all twelve screens at 1x, one direction per
row.

The mockups all show the same state, so the directions can be compared like for like:

- **Patch:** A17.
- **VCF page:** knob 2 (RESO, parameter 42) is being turned.
- **ENV-1 page:** knob 2 (DECAY, parameter 62) is being turned.
- **Sequencer:** 112 BPM and 16 steps. Step 5 holds a C minor 7 chord, tied into step 6, and
  is open for editing. The playhead is on step 7. Steps 4 and 14 are ties, and steps 2, 8, 12
  and 16 are rests.

## Shared foundations (all three directions)

### Canvas and type

- **Canvas:** 240 x 240, RGB565. All colors below are given as RGB565 hex, which is what the
  `HB_RGB()` macro produces. Expect banding in any gradient. Every gradient here is drawn as
  deliberate, hard-edged bands, one `fill` per band, so the banding is part of the look and
  costs nothing per frame.
- **Type:** only the host's two fonts, which are Terminus.

  | style | size | grid | use |
  | --- | --- | --- | --- |
  | S | 16 px tall | 8 px advance, 30 columns | labels and secondary values; lower case available |
  | L | 32 px tall | 16 px advance, 15 columns | patch names and the focused value; upper case, digits and `ASCII 32..95` only |

  Labels are at most 6 characters (48 px) so they fit a 53 px knob column.
- **Custom fonts:** a module could draw its own font through `blit` or `cv_pixel`, as FM-106
  does with Dinghy's `gfx.c`. These directions do not, to save flash and keep text the same
  as the host's. The one extra glyph set is the 7-segment display below, which is drawn
  entirely with `fill`.

### The 7-segment display

The 7-segment display carries the panel DNA: a parameter number, a patch number or a step number
on a dark LED window.

- **Digit sizes:**

  | size | width | height | stroke |
  | --- | --- | --- | --- |
  | large | 20 px | 40 px | 4 px |
  | patch number in B | 18 px | 34 px | 3 px |
  | B header | 9 px | 16 px | 2 px |

- **Cut segments:** strokes of 3 px or more have a 1 px gap where segments meet, so they read
  as separate LED segments.
- **Ghost segments:** unlit segments are drawn as faint "ghost" segments, at 8-12 % of the lit
  color mixed into the window. This makes the window read as a real LED and keeps each digit's
  shape steady.
- **Characters:** `0-9 A b C d E F P U -`. A patch shows as bank letter + two digits (`A17`).
- **Cost:** at most 7 fills per digit, and only the segments that change are redrawn.

### The glow

The glow marks the active element. It is the element's frame in the accent color, plus two
frames outside it at 45 % and 20 % of the accent mixed into the background.

- **Cost:** 12 fills.
- **Where it applies:** only to the element under the hand. That is the knob being turned, the
  playing step, or the step being edited.
- **One at a time:** there is never more than one glow per screen, apart from the playhead plus
  the step being edited on the sequencer.

### Layout grid

- **Header and margins:** rows 0-23 are a header, which is the same band as the host's
  `HB_TOP`. The page body starts at row 24 or below, so the layouts still work if the host
  keeps its top line. The outer margin is 8 px on all sides.
- **Knob order:** the four knobs map left to right, to four 53 px columns with 4 px gaps
  (8 + 4 x 53 + 3 x 4 = 232) in C, and to rows K1-K4 in A. Wherever the layout allows, the
  screen order matches the panel order.
- **Graphs:** each graph fits in one off-screen canvas, which is at most 240 x 96.

### Section colors and parameter numbers (assumed)

Each section has its own accent, as a panel's sections do. The parameter number's tens digit is
the section, so 4x is always VCF. The numbers are placeholders until `interaction.md` fixes the
pages.

| page | numbers | A Membrane | B Outrun | C Night Panel |
| --- | --- | --- | --- | --- |
| DCO-1 | 11-14 | `EC07` orange | `F95A` magenta | `FAF4` pink |
| DCO-2 | 21-24 | `EDC7` amber | `B27F` violet | `FC47` orange |
| MIX | 31-34 | `CE15` stone | `F77F` white | `DEBE` lilac white |
| VCF | 41-44 | `4D9A` steel blue | `275F` cyan | `2EDF` cyan |
| VCA | 51-54 | `8E4C` green | `3FF5` mint | `7F8F` green |
| ENV-1 | 61-64 | `DB53` rose | `FC43` orange | `FE07` amber |
| ENV-2 | 71-74 | `B399` purple | `FE87` yellow | `FF0E` pale yellow |
| LFO | 81-84 | `9C5C` lavender | `FAD1` hot pink | `B3FF` violet |
| CHORUS | 91-94 | `5657` teal | `4CDF` blue | `3D1F` blue |
| SEQ | step no. | `EA8A` red | `F95A` magenta | `F9FB` magenta |

Every section color reaches at least 4.9:1 contrast against its own direction's background.
Text reaches at least 13.9:1, and dimmed text reaches at least 4.9:1 on every surface it is
used on. These are WCAG AA ratios, computed on the quantized RGB565 values.

### LEDs (the hardware)

The key and button LEDs are one color each, and the firmware cannot change that color. Each LED
has three states: off, lit (`led(id, 1)`) and glow (`led(id, 2)`, a dim light). Color coding
therefore lives only on the screen. The LEDs follow these rules in every direction:

| state | meaning |
| --- | --- |
| **lit** | where you are, or what is on: the current section's button, PLAY while playing, REC while recording, a step that has notes |
| **glow** | what you can press now: the other section buttons, PLAY when stopped with a pattern, an empty step inside the pattern length |
| **off** | nothing to do: a step beyond the length, a button that does nothing on this page |

Sequencer details:

- **Step keys (assumed):** the 16 white keys F3 to G5 are steps 1 to 16. The FM-1's 27 keys
  hold exactly 16 white keys, which leaves the black keys free for other functions.
- **Playhead:** the playhead inverts its step, so a lit step goes off and a glowing step goes
  lit. The playhead reads as a moving "hole" across both filled and empty steps.
- **Step being edited:** blinks between lit and glow at 2 Hz.
- **On-screen keyboard:** every sequencer mockup draws the key strip with the same three states,
  so the screen and the keys always agree.

### Redraw rules

The audio runs on the same CPU, so the screen does as little work as possible:

- **`draw(full)` only on a page change or the module's start.** The full frame is drawn once.
  After that, `draw()` updates only what changed, tracked as "drawn" copies of each value.
  Scintilla and Prophet do this the same way.
- **Throttle.** A knob turn marks its column dirty. The screen redraws dirty items at most
  every 33 ms (30 Hz), and coalesces fast turns into one redraw.
- **Small strips.** Text that changes goes into a canvas the size of its box and is blitted in
  one go (for example 53 x 32 for an L value), so there is no flicker from fill-then-text.
- **Graphs.** The filter response redraws on its own canvas, and only when CUTOFF or RESO
  changes. The ADSR graph does the same when A, D, S or R changes. Each graph is a polyline of
  about 30-110 `cv_line` calls, with the hatch under the curve on every other column.
- **The sunset and the grid** are drawn on the patch screen only, once on entry. That is about
  90 row fills plus about 25 lines. Changing patch redraws the name strip (240 x 32) and the
  LED digits, not the scene. No frame is ever a full-screen gradient.
- **Sequencer.** Each step is a pad, so moving the playhead redraws two pads (the old and the
  new). At 112 BPM in 16ths that is 7.5 steps a second, about 17 k pixels a second.

## Direction A: Membrane (panel first)

This direction is a charcoal hardware panel with cream legends, a red LED window and raised
membrane-key tiles. Synthwave appears only as a banded sunset badge on the patch screen.

| token | RGB565 | use |
| --- | --- | --- |
| bg | `18E4` | panel |
| panel | `2145` | tile, header, footer |
| raised | `31A7` | focused tile, a step with notes |
| line | `3A09` | rules, frames, bar track |
| text | `EF3A` cream | labels, values (13.9:1) |
| dim | `A513` | secondary labels (6.7:1 on bg, 4.9:1 on raised) |
| led | `F984` red | 7-segment display, the playing step's LED dot |
| led ghost | `2082` | unlit segments |
| led bg | `0861` | LED windows, graph windows |
| ink | `1082` | text on a section chip or on the sunset |
| sunset badge | `FE88 FD87 FC67 FB49 F24E D1D2` | six bands, patch screen only |

- **Layout:** a header with the section name on a chip in the section's color (the panel's
  colored section heading), and a footer with `< PREV  PAGE  NEXT >`. On parameter pages, a 66 x
  50 LED window sits beside the graph window.
- **Knobs:** four rows K1-K4, each 224 x 28. Each row has its number in a small LED cell, its
  label, its value on the right, and a 3 px bar.
- **Focus:** the focused row is raised and framed in the section color, and its number is full
  red (the other numbers are at 45 % red). This is a mechanical "pressed key" look, not a glow.
- **Sequencer:** 2 x 8 pads, each 26 x 34, showing the note name (`Eb3`, `Cm7`). A tie is a
  red bar from the step before. The playing step is a cream pad with dark text and a red LED
  dot. The step being edited is framed in red.
- **Strength:** the most faithful to the panel, calm, and very readable.
- **Weakness:** it is barely "synthwave". The rows do not match the left-to-right knobs.

## Direction B: Outrun (synthwave first)

This direction is a deep indigo night with neon magenta, cyan and orange. It has arc knobs, a
banded neon rule under the header, and a full sun and perspective grid on the patch screen.

| token | RGB565 | use |
| --- | --- | --- |
| bg | `0824` | night |
| plate | `1066` | knob plates |
| grid | `38CD` | knob tracks, grid lines, rules |
| grid dim | `2089` | the floor under the filter curve, beat lines |
| text | `F77F` | values (17.9:1) |
| dim | `9C59` | labels (6.6:1) |
| magenta | `F95A` | brand, notes |
| cyan | `275F` | LED digits, playhead |
| orange | `FC43` | chords, the step being edited |
| yellow | `FE87` | rule band |
| sun | `FF08 FE07 FD07 FBC9 FA8D F1D2 D157 A119` | eight bands, cut by widening gaps |

- **Header and footer:** the section name is in neon. The parameter number is a small cyan
  7-segment display at the top right. Under the header runs a 4-color rule (four fills). The
  footer has `< PREV` and `NEXT >` plus a strip of page dots in the section colors.
- **Knobs:** a 2 x 2 layout of 112 x 58 plates. Each plate has a 270° arc (radius 24, 4 px)
  with a pointer, the label in S and the value in L. The focused knob gets a glowing arc and the
  plate glow.
- **Graphs:** each curve is drawn three times, the center line plus two 35 % copies 2 px above
  and below, for a neon bloom. The envelope has a scanline fill under it.
- **Sequencer:** a 16 x 14 px piano-roll lane, pitch up. A chord is a stack of blocks, a tie
  extends its block, and the playhead is a cyan column with a center line. A step-detail block
  and the LED key strip sit below.
- **Strength:** the most "synthwave" of the three, and the most fun.
- **Weaknesses:** the arcs need a pixel table per knob and a 60 x 58 canvas redraw on every
  value change, which is the most expensive of the three. The 2 x 2 grid does not match the four
  knobs in a row. The 14 px piano-roll steps are hard to read at arm's length. Neon on neon
  tires the eye over a long session.

## Direction C: Night Panel (recommended)

This direction keeps the panel's structure and lights it like synthwave. The blocks are
indigo-black, and each section is marked by a 3 px band of its neon color across the top. The
parameter number sits in a 7-segment LED window lit in the section color. Four vertical LED
ladder columns stand for the four knobs, in the spirit of a programmer's sliders. The sun and
the grid appear on the patch screen only.

| token | RGB565 | use |
| --- | --- | --- |
| bg | `0863` | night panel |
| panel | `18C5` | knob columns, rest steps |
| raised | `2107` | the focused column, unlit ladder segments, steps with notes |
| line | `2969` | rules, frames |
| text | `EF7F` | values, focused labels (17.2:1) |
| dim | `9CB7` | other labels (7.0:1 on bg, 5.6:1 on raised) |
| led bg | `0021` | LED and graph windows |
| LED digits | the section color | ghost segments at 8 % |
| sun | `FEC9 FD88 FC48 FAEC F1F2 C198` | six bands, patch screen only |
| grid / horizon | `5953` / `F9FB` | patch screen only |

- **Header:** a 3 px section band, then the section name in its color, its long name dimmed,
  and the patch number on the right. The same band sits on every page, so you can tell the
  section from across the room.
- **Top block:** a 64 x 64 LED window showing the focused parameter's number, beside a 152 x
  64 graph window. On the VCF page the graph is the filter response, on ENV pages it is the
  ADSR, and on LFO pages it would be the wave shape.
- **Knobs:** four columns (53 x 120), left to right, one per knob. Each column has the number
  in S in the section color, the label in S, an 8-segment LED ladder, and the value in L.
- **Focus:** the focused column is raised, its ladder is lit at full color (the others at
  60 %), and it has the glow.
- **ENV page:** a 224 x 96 ADSR graph with dotted stage dividers and stage letters. The active
  stage is drawn thicker, in the full section color and with a denser hatch. The columns show
  `61 ATK`, the value in L, a horizontal ladder, and the time in plain units (`480ms`).
- **Page strip:** the footer is a row of nine small dots, one per section in its color, with
  the current page drawn wide. It matches the section buttons' LEDs.
- **Sequencer:** 2 x 8 pads, each 26 x 44. Each note is a bar, so a chord's stack of bars
  shows how many notes it has. A tie continues the held bars at 45 % through the gap into the
  next pad. The playing step gets the glow in cyan, and the step being edited a 2 px white
  frame. Below sit the step number in the LED window, the chord name in L, its notes, and the
  LED key strip.
- **Patch screen:** the patch number in the LED window, the name in L, and the voices and
  chorus mode. The banded sun and the purple grid fill the bottom 90 px, drawn once.

### Why C

1. **It maps to the hardware.** Four columns left to right match the FM-1's four knobs, so the
   screen shows what each knob will do.
2. **It has the most panel DNA per pixel.** It has section color bands, numbered parameters, the
   LED window, and LED ladders that echo sliders and LED meters. It is recognizably the same
   family as an 80s panel without copying one.
3. **The synthwave goes where it counts.** It shows in the neon accents, the glow on the one
   thing under your hand, and the sun and grid on the patch screen. It does not wash over every
   page.
4. **It is the most readable.** It has the highest contrast of the three (17.2:1 text, 7.0:1 dim
   labels), its values are in L, and it needs no 14 px detail to read.
5. **It is the cheapest to draw.** Columns and ladders are plain fills, with no arcs or pixel
   tables. A value change costs at most 8 ladder fills plus one 53 x 32 value blit.

## Open items for the interaction model

These are assumed here and owned by `interaction.md`:

- the page list and its order (DCO-1, DCO-2, MIX, VCF, VCA, ENV-1, ENV-2, LFO, CHORUS, then
  SEQ)
- the decade parameter numbering
- the white keys as the 16 steps
- how a page is chosen, which this document only shows as `< PREV  NEXT >` and the dot strip
