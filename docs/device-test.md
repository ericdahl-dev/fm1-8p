# Running the CPU test on an FM-1 (#13)

This guide runs fm1-8p's CPU test on a real FM-1 and gets the numbers that decide the voice count. It takes
about 20 minutes. The test itself runs for 45 s.

Every fm1-dock command and behavior below is quoted from fm1-dock at the commit we pin, `9e6e08e`
(`firmware/pins.env`), with the file and line it comes from.

Set two paths once, in the terminal you use for the whole guide (change them to where yours are):

```sh
FM18P=~/Projects/fm1-8p                  # this repo
DOCK=$FM18P/build/fm1-deps/fm1-dock      # fm1-dock at 9e6e08e, made by tools/fm1-build.sh fetch
RUN=$FM18P/build/device-run              # where this run's files go
mkdir -p "$RUN"
```

Every command below runs from `$DOCK` (`cd "$DOCK"` first) and writes into `$RUN`.

## What the test does

The module `FM1-8P CPU` holds a chord and steps through 9 steps, 5 s each:

| step | what plays |
| --- | --- |
| 1 to 8 | 1 to 8 voices, one note each (C3 up in thirds), poly mode |
| U | the unison burst: 8 voices in unison; in every block the 8 keys go up, go down again (each note-on retriggers every voice) and the unison detune changes. After its 5 s the burst stops and the last note rings; the U row no longer changes |

For each step it shows the **worst** render time of any block, as a percentage of one 256-frame block (5.8 ms at
44.1 kHz). The block that starts a step (engine setup, first note-on) is not timed. Step U is the case from the #13
comment: unison does its divides when notes and the detune change, so a burst of note-ons inside one block is its
worst case.

What to expect, from fm1-emu's strict mode (it reads about 10-15 % below the device): about 3.4 % per voice, 28.1 % at
8 voices, 31.1 % for U. On the device, expect roughly 1.15 times those. A big difference from that is a finding in
itself: note it.

## 1. What you need

- The FM-1, a USB **data** cable, a computer (Linux or macOS).
- Python 3 and [uv](https://docs.astral.sh/uv/), or `pip3 install mido python-rtmidi` (fm1-dock README.md:70).
- Chrome or Edge, for the web installer and the Firmware Hub.
- The test build: the `fm1-8p-cpu` artifact of the latest green CI run on `main`
  (`gh run download -R ericdahl-dev/fm1-8p -n fm1-8p-cpu`), or build it yourself with `tools/fm1-build.sh fetch` and
  `tools/fm1-build.sh build` (README, "FM-1 CPU-test firmware"). Either way you get:
  - `fm1_8p_cpu.dock`: the module, for route A.
  - `fm1-8p-cpu.fwsc`: a whole firmware (identity `FM-1_948`), for route B.

## 2. Before you start: a way back to the stock firmware

fm1-dock at `9e6e08e` has **no tool that reads the installed firmware back** off the FM-1, so the stock firmware
can't be copied from the device. Instead:

1. **Write down what is installed.** Plug in the FM-1, then:

   ```sh
   cd "$DOCK"
   uv run --with mido --with python-rtmidi tools/fm1_install.py --info | tee "$RUN/stock-info.txt"
   ```

   It prints the running firmware's identity and port (tools/fm1_install.py:13, `fm1_install.py --info [--port NAME]`).
   Note it, and the version the FM-1 shows (V15).
2. **Check the way back exists before you change anything.** fm1-dock's README.md:77-78: "Back to any other firmware,
   or the official one, with the [FM-1 Firmware Hub](https://fm1.designburgapps.com) in Chrome or Edge: fm1-dock
   answers the hub's updater like every firmware there." Open the hub and confirm it offers the official M-VAVE FM-1
   firmware. If it doesn't list the official one, stop here and tell us.
3. **Know the safety nets** (fm1-dock README.md:176-186). Hold the key from before you switch on until the screen
   shows something:

   | at power-on | what happens |
   | --- | --- |
   | **OCT-** | the USB rescue: no sound, only the updater, for any install |
   | **OCT+** | safe start: the sine only, and the radio not started |
   | **HOME** | fm1-dock stays, even with a firmware in its boot slot |

   Also from there: "A module that crashes twice in a row is skipped at boot until you pick it again. One that renders
   slower than real time is stopped and the sine plays, so USB and the updater keep working." If a crash happens
   during boot twice, "the FM-1 starts in the USB rescue by itself" (docs/RULES.md:58). If even the rescue is gone,
   "the FM-1's own boot ROM still updates over USB: FM-1-transporter gets it back" (docs/RULES.md:61-62,
   [kurogedelic/FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter)).

Installing firmware is at your own risk (fm1-dock README.md:78).

## 3. Route A (do this first): the module in fm1-dock, nothing of ours flashed

Our module is a `.dock` for fm1-dock's module slots. It needs fm1-dock on the FM-1, but it is never flashed as
firmware: if it misbehaves, pick another synth or delete it.

1. **Install fm1-dock 0.8** (once). Either the web page (README.md:50-52: open
   [the install page](https://joakimeriksson.github.io/fm1-dock/#install), "Connect over USB", then "Install fm1-dock";
   "Nothing is written until you press a button"), or from a terminal with the release file (README.md:66-71):

   ```sh
   uv run --with mido --with python-rtmidi tools/fm1_install.py /path/to/fm1-dock-0.8.fwsc   # asks before it writes
   ```

   Our module is built against fm1-dock `9e6e08e` (0.8 plus 19 commits). Both have module ABI 2
   (`firmware/dock/hb_module.h:17`), so 0.8 runs it.
2. **Deploy the module** (tools/dock.py:17, its usage line):

   ```sh
   uv run --with python-rtmidi tools/dock.py deploy /path/to/fm1_8p_cpu.dock
   ```

   "the module into its flash, then running". With more than one MIDI device, add `--port NAME`.
3. The FM-1's screen shows `FM1-8P CPU` and the sweep starts by itself. If it doesn't show, hold **HOME** and pick
   `FM1-8P CPU` (README.md:54: "hold HOME on the FM-1, pick a synth and play").

If the deploy is refused (an ABI or room error), go to route B.

## 4. Run the test and read the screen

1. **Don't touch the FM-1.** The sweep starts by itself when the module starts; let that one run. (The module's
   controls, for later: PLAY sweeps again from 1; KNOB1 picks a step by hand, 1 to 8 then U, and stops the sweep.)
   If you did press PLAY or turn KNOB1 during the sweep, press PLAY once and start the 50 s again: the screen
   restarts, and the report in section 5 uses only the last sweep.
2. Wait **50 s**: 9 steps of 5 s, plus a little.
3. The screen has one row per step: `1` to `8`, then `U`. Each row is the step's worst block, like `4   11.5 %`.
   The current step's row is white, the others gray. **Write down all 9 rows** once `U` has run its 5 s.
4. Listen while it runs: a chord that gets richer for 40 s, then a fast-retriggered 8-voice unison note for 5 s. Clicks,
   dropouts or the sine taking over mean an overrun: note at which step.

## 5. Pull the numbers off the FM-1

With the FM-1 still on USB:

```sh
cd "$DOCK"
uv run --with python-rtmidi tools/dock.py logs      > "$RUN/fm1-8p-logs.txt"
uv run --with python-rtmidi tools/dock.py modules   > "$RUN/fm1-8p-modules.txt"
uv run --with python-rtmidi tools/dock.py info      > "$RUN/fm1-8p-info.txt"
cd "$FM18P"
FM1_BUILD_LIB=1 sh -c '. tools/fm1-build.sh; emu_report "$0"' "$RUN/fm1-8p-logs.txt" | tee "$RUN/report.md"
```

- **logs**: the host's log ring. Our module writes one line per step it held in full:
  `fm1-8p voices/worst permille 00040073`, where the hex value is `count << 16 | permille`
  (`00040073`: 4 voices, 0x73 = 115 per mille = 11.5 %). Count 9 is U. The last command above turns the lines
  into `$RUN/report.md`, a table with one row per step. It uses only the last sweep in the log (from its last count-1
  line on), so a sweep restarted with PLAY doesn't give duplicate rows; it fails and names any step that's
  missing. Use its rows for the "log" column in section 8.
- **modules**: fm1-dock's own measure of the whole render, from tools/dock.py's `modules` command: "render worst N %
  of a block, N overruns", and the second core's line if it runs.
- **info**: the build, the board, the chips, the free flash and RAM, "as OPTIONS > ABOUT shows them"
  (tools/dock.py:21). It needs fm1-dock 0.8 plus `c9abe9f`; on 0.8 itself it says "no INFO from this fm1-dock (an
  older one)": that's fine, skip it.

## 6. Route B (only if route A fails): the solo firmware

`fm1-8p-cpu.fwsc` replaces the whole firmware with fm1-dock's host plus only our module, identity `FM-1_948`. It keeps
fm1-dock's safety nets (section 2).

```sh
cd "$DOCK"
uv run --with mido --with python-rtmidi tools/fm1_install.py /path/to/fm1-8p-cpu.fwsc   # asks before it writes
```

It restarts into the test. Then run sections 4 and 5 the same way. Afterwards, go back with the Firmware Hub
(section 2) or reinstall fm1-dock 0.8.

## 7. Back to stock

When the numbers are in: open the [Firmware Hub](https://fm1.designburgapps.com) in Chrome or Edge with the FM-1 on
USB and install the official firmware. If the FM-1 doesn't connect, switch it on holding **OCT-** (USB rescue) and
try again.

## 8. Results to paste into #13

```markdown
### FM-1 CPU test, device run

- Date:
- Stock firmware before (fm1_install.py --info, version shown):
- Route: A (.dock in fm1-dock 0.8) / B (.fwsc FM-1_948)
- fm1-8p commit (CI run or `git rev-parse --short HEAD`):
- Top line during the run: 2CPU / 1CPU

| step | screen (worst %) | log (per mille) | fm1-emu strict (%) | device / emu |
| --- | --- | --- | --- | --- |
| 1 | | | 4.2 | |
| 2 | | | 7.6 | |
| 3 | | | 11.0 | |
| 4 | | | 14.5 | |
| 5 | | | 17.9 | |
| 6 | | | 21.3 | |
| 7 | | | 24.7 | |
| 8 | | | 28.1 | |
| U (unison burst) | | | 31.1 | |

- `dock.py modules`: render worst __ % of a block, __ overruns
- Heard any clicks, dropouts or the sine taking over? At which step?
- Logs attached (from `$RUN`): fm1-8p-logs.txt, fm1-8p-modules.txt, report.md, stock-info.txt (fm1-8p-info.txt
  if it worked)
```

The emu column is from fm1-emu `5c6ed85` in strict mode with the engine at `8ee3f5e` (unison and #5's oscillators,
before the filter). If the engine has changed since, rerun `tools/fm1-build.sh emu` and use its `build/fm1/emu-report.md`.
