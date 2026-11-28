# Screen captures from fm1-emu

Real screens of FM1-8P, the raw 240 x 240 LCD as fm1-emu draws it (no emulator panel), for the
project page in place of the mockups.

- **Firmware:** main at `9bcfdef` (`site: a designed share card, absolute share links and a touch
  icon (#356) (#412)`).
- **Builds:** `tools/fm1-build.sh build` for Membrane, and `FM1_THEME=night tools/fm1-build.sh build`
  for Night Panel; each capture used `build/fm1/fm1-8p.fwsc` of its theme's build. The key scripts
  are the same for both themes, except on the GLO theme page (below).
- **Emulator:** fm1-live from fm1-emu `5c6ed85` (not the pinned v0.4.0 release), default timing (not
  `FM1_STRICT`), a fresh flash each run, so every run boots to P17 SOUNDTRACK with no edits.

**Note:** PLAY and the boot screen show the old sunset art (Membrane's bands, Night Panel's sun over
the grid). Eric has picked a skyline to replace it; these two will be captured again once it lands.

## Regenerating

Each capture is one run:

```sh
mkdir -p work
FM1_BENCH_KEYS="$KEYS" fm1-live --fwsc fm1-8p.fwsc --shot $MS --lcd-only   # writes work/lcd.ppm
```

then `work/lcd.ppm` saved as PNG (Pillow). Key script syntax is fm1-live's `FM1_BENCH_KEYS`:
`ms:id` presses button or key `id`, `ms:id^` releases it, and `ms:eN:clicks` turns encoder N.
Buttons: SEL 3, ENV 4, EDIT 6, GLO 7, PLAY 12, REC 13 (`firmware/ui.h`); note keys are 14 + k, with
k = 0 for F3. Encoders: 0 SELECT, 5 KNOB4.

| File(s) | Screen | `--shot` ms | `FM1_BENCH_KEYS` |
| --- | --- | --- | --- |
| `membrane-boot.png`, `night-panel-boot.png` | the boot screen | 1400 | (none) |
| `membrane-play.png`, `night-panel-play.png` | PLAY, P17 SOUNDTRACK (the boot tone, shown as `PATCH 17` and `P17 SOUNDTRACK`), no edits | 4000 | (none) |
| `membrane-vcf.png`, `night-panel-vcf.png` | page 5A VCF (EDIT tap) | 4000 | `3000:6,3060:6^` |
| `membrane-vcf-env.png`, `night-panel-vcf-env.png` | page 8A ENV-1, the filter envelope (ENV tapped three times: 5A, 5B, 8A) | 4500 | `3000:4,3060:4^,3400:4,3460:4^,3800:4,3860:4^` |
| `membrane-dco1.png`, `night-panel-dco1.png` | page 1A DCO-1, the focus on 12 WAVE (number jump 1, 2) | 4500 | `3000:3,3200:14,3260:14^,3400:16,3460:16^,3700:3^` |
| `membrane-chorus.png`, `night-panel-chorus.png` | page 6A VCA and CHORUS, the focus on 64 CHORUS (number jump 6, 4) | 4500 | `3000:3,3200:23,3260:23^,3400:20,3460:20^,3700:3^` |
| `membrane-lfo.png`, `night-panel-lfo.png` | page 7A LFO, the focus on 73 RATE (number jump 7, 3) | 4500 | `3000:3,3200:25,3260:25^,3400:18,3460:18^,3700:3^` |
| `membrane-seq.png`, `night-panel-seq.png` | SEQ view's piano roll while playing (playhead at step 8, 120 BPM) | 9000 | the SEQ script below |
| `membrane-glo-theme.png` | GLO page 3, the focus on G34 THEME at MEMBRANE | 4500 | `3000:7,3060:7^,3400:e0:2,3600:e5:1,3800:e5:-1` |
| `night-panel-glo-theme.png` | the same at NIGHT PANEL | 4500 | `3000:7,3060:7^,3400:e0:2,3600:e5:-1,3800:e5:1` |

DCO-1, CHORUS and LFO use the number jump (SEL held, two white keys as digits: F3..A4 are
1..9 and 0, SEL let go), which moves the focus without touching a value. A knob turned and turned
back can land on another stored value (LFO RATE's table does) and mark the patch edited.

On the GLO page, KNOB4 is turned one way and back so the focus lands on G34 with the theme
unchanged.

### The SEQ script

REC tapped (step record, which opens SEQ view), 16 steps written, then PLAY. The steps: C4-E4-G4,
G4, tie (ENV), E4, A3-C4-E4, C5, rest (SEL), A4, F3-A3-C4, F4, A4, C5, G3-B3-D4, D5, B4, G4. Each
step's keys go down together and up 150 ms later; one step every 300 ms.

```
3000:13,3060:13^,3300:21,3300:25,3300:28,3450:21^,3450:25^,3450:28^,3600:28,3750:28^,3900:4,3960:4^,4200:25,4350:25^,4500:18,4500:21,4500:25,4650:18^,4650:21^,4650:25^,4800:33,4950:33^,5100:3,5160:3^,5400:30,5550:30^,5700:14,5700:18,5700:21,5850:14^,5850:18^,5850:21^,6000:26,6150:26^,6300:30,6450:30^,6600:33,6750:33^,6900:16,6900:20,6900:23,7050:16^,7050:20^,7050:23^,7200:35,7350:35^,7500:32,7650:32^,7800:28,7950:28^,8100:12,8160:12^
```
