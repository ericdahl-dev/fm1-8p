# fm1-8p

An original firmware for the M-VAVE FM-1 that turns it into a polyphonic analog-style synth,
inspired by mid-80s two-oscillator polysynths with a chorus. It is not affiliated with or endorsed
by Roland or M-VAVE.

## Status

On the desktop, the fixed-point engine plays band-limited voices of two
oscillators each (saw, pulse, square or noise, with octave, fine tune, pulse
width and a mix between them) and an ADSR amp envelope, up to 8 at once (6 by
default), with voice stealing, the sustain pedal, pitch bend, a unison
mode, an LFO routed to pitch and pulse width with a delay, and velocity,
rendered from a MIDI file to a WAV.

## Build and run

Linux or macOS, with a C compiler (gcc or clang) and GNU make. No other
dependencies. On macOS, the Xcode command line tools (`xcode-select --install`)
give clang and make. CI builds and tests on both: `build-test (gcc)` and
`build-test (clang)` on Ubuntu, `build-test (macos-latest, clang)` on macOS.

```sh
make                 # builds build/fm1-8p-render and build/fm1-8p-bench
make check           # runs the tests and the float-free engine check
make bench           # times the engine per voice count (see Benchmark)
make render-demo     # renders examples/demo.mid to build/demo.wav

./build/fm1-8p-render -r 44100 -v 6 -s amp_attack_ms=200 -s amp_release_ms=800 \
    -s chorus_mode=1 song.mid song.wav
```

`chorus_mode` is 0 (off, the default), 1 (slow), 2 (deep) or 3 (fast); the
modes are described in [docs/fixed-point.md](docs/fixed-point.md#chorus).

Run `./build/fm1-8p-render` with no arguments to list every option and
parameter.

The render tool plays one MIDI channel (`-c`, by default the channel of the
first note on) with 6 voices by default (`-v`). Each voice is mixed at
1/(2 x voices) so a full chord never clips, which makes a single note quieter
with more voices: -21.6 dBFS with 6, -6 dBFS with `-v 1` or in unison
(`-s voice_mode=1`, where one note takes every voice). After each render
the tool prints the voice count, channel and that level.

### Benchmark

`build/fm1-8p-bench [-b BLOCKS] [-r RUNS] [-w WORKLOAD]` (`-h` for help) times the engine on the
FM-1 CPU test's workload (`firmware/cpu_test.c`): the same held chord at 1 to 8 voices,
44 100 Hz, 256-frame blocks. For each run and voice count it renders one block
untimed (the notes start there), then times `BLOCKS` blocks one by one
(default 2000) and keeps the median block. The table gives the median of the
`RUNS` runs (default 5).

It prints one table per workload, under `## default patch` and `## full patch`; `-w default` or
`-w full` runs only that one (default `all`):

| workload | what plays |
| --- | --- |
| default | the default patch, as in the CPU test's steps 1 to 8. With every modulation depth at 0 the LFO costs nothing here |
| full | the CPU test's step F patch on the same chord: osc1 pulse and osc2 saw mixed half and half, 30 cents of vibrato, 25 % pulse-width modulation, amp velocity at 100 % with a different velocity per note, and the deep chorus |

The columns:

| column | meaning |
| --- | --- |
| ns / block | time for one 256-frame block |
| ns / frame | ns / block divided by 256: compare with the FM-1's 22 676 ns per frame |
| ns / voice-frame | ns / frame divided by the voice count |
| spread | (slowest run - fastest run) / median, in % |

The clock is `CLOCK_MONOTONIC` on Linux and `CLOCK_UPTIME_RAW` on macOS (whose
`CLOCK_MONOTONIC` ticks in microseconds, too coarse for a 1-2 µs block). The
first line of the output gives the clock's smallest step. If a voice count's
blocks read as 0 ns, the bench says "timer too coarse" and exits with 1.

Repeatability, measured on a ThinkPad T480s (i5-8350U, Linux, other jobs
running): over three runs of the default workload, each voice count's
ns / block stayed within 0.2 %. The spread column is the warning light: a run
that something else interrupted shows up there (one run in five often reads
10-90 % slow on a busy machine), while the median ignores it. Compare numbers
from the same machine only, and rerun when the spread is above a few percent.
The FM-1's own numbers come from fm1-emu or the device (FM-1 CPU-test firmware
below); the desktop numbers are for spotting changes in cost between commits.

The engine uses only integer math. Its number formats, the sample rate and
block size rules, and how the float-free check works are in
[docs/fixed-point.md](docs/fixed-point.md).
How notes get voices (the stealing rule), the sustain pedal, pitch bend,
unison mode (`-s voice_mode=1 -s unison_detune_cents=20`) and the output gain
staging are in [docs/voices.md](docs/voices.md).
The LFO (waveforms, rate, delay and fade-in, depths, routing to pitch and
pulse width) and the velocity curve are in
[docs/modulation.md](docs/modulation.md), for example
`-s lfo_pitch_cents=20 -s lfo_delay_ms=300 -s amp_velocity_pct=70`.

## FM-1 CPU-test firmware

`firmware/` runs the unchanged engine on the FM-1 as an
[fm1-dock](https://github.com/joakimeriksson/fm1-dock) module and measures it: it holds a chord
and steps from 1 to 8 voices, 5 s each, then runs the unison burst (row U: 8 voices in unison,
8 note-ons and a detune change in every block), then the full patch (row F: the 8-voice chord with
both oscillators, vibrato, pulse-width modulation, amp velocity and the chorus), showing the worst
render time per step on screen (as % of a 256-frame block, 5.8 ms; the block that starts a step,
engine init and note on, isn't timed) and logging each step a sweep held in full (`tools/dock.py
logs` in fm1-dock; the burst logs as count 9, the full patch as 10). KNOB1 picks a step by hand, 1
to 8, U, then F (it stops there; a hand-picked U keeps bursting), and stops the sweep; PLAY sweeps
again from 1. To run it on an FM-1,
follow [docs/device-test.md](docs/device-test.md).

Linux x86-64 (macOS through fm1-dock's Docker fallback, untested), with curl, git, tar, xz and
Python 3 with Pillow:

```sh
tools/fm1-build.sh fetch     # fm1-dock, JieLi toolchain, AC79 SDK files into build/fm1-deps
tools/fm1-build.sh build     # build/fm1/fm1_8p_cpu.dock and build/fm1/fm1-8p-cpu.fwsc
tools/fm1-build.sh canaries  # each firmware/canary module must fail fm1-dock's safety checks
tools/fm1-build.sh emu       # the CPU test in fm1-emu, strict timing: build/fm1/emu-report.md
```

Every input is pinned in [firmware/pins.env](firmware/pins.env) and checked before a build:

| input | pin |
| --- | --- |
| fm1-dock (GPL-3.0-only) | commit `9e6e08e`, fetched, not vendored |
| JieLi Linux toolchain, clang 4.0.1 for pi32v2 | tarball sha256 and directory `jieli-linux-toolchains-20260730.1` (the download URL always serves the latest) |
| JieLi AC79 SDK `AC79NN_SDK_V1.2.1_2023-12-13` | commit `d179b448`; the 3 files the package needs, each by sha256 |
| fm1-emu (MIT) | release v0.4.0, sha256 |
| Pillow | 13.0.0 |

To use inputs you already have, set `JIELI_TOOLCHAIN`, `AC79_SDK`, `FM1_DOCK`, `PYTHON` or
`FM1_LIVE`. They are checked the same way, and `fetch` never changes them: a mismatch stops the
build. `FM1_LIVE` must be the pinned fm1-live build unless `FM1_EMU_UNPINNED=1`, which the report
then notes.

The module lists no engine files: the build fills in every `engine/*.c`, so a new engine file needs
no firmware edit.

**Known risk:** JieLi's toolchain URL always serves their latest tarball. When they publish a new
one, `fetch` stops with "JieLi published a new toolchain" (CI too, once its cache is gone). Review
the new one, then update `JIELI_TOOLCHAIN_SHA256` and `JIELI_TOOLCHAIN_DIR` in `firmware/pins.env`.
We don't mirror the tarball: its license for redistribution is unknown.

- The **`.dock`** goes onto an FM-1 that runs fm1-dock, over USB, without flashing
  (`tools/dock.py deploy` in fm1-dock).
- The **`.fwsc`** is a solo firmware package, identity `FM-1_948`, with fm1-dock's safety checks
  and rescue modes (OCT- at power-on: USB rescue; OCT+: safe start). Keep the stock firmware for
  recovery.

CI builds both on every push (job `fm1-firmware`, artifact `fm1-8p-cpu`) and runs the emulator
step as a report in the job summary; it becomes a gate once the emulator is calibrated against
the device (#13).

## Layout

- `engine/` - the synth engine: plain C99, fixed point, no hardware or OS code,
  no dynamic allocation. Public interface: `engine/fm1_8p.h`.
- `host/` - the desktop render tool: Standard MIDI File reader, WAV writer.
- `firmware/` - the FM-1 CPU test: `cpu_test.[ch]` (plain C, tested on the desktop),
  `fm1_8p_cpu/` (the fm1-dock module), `canary/` (modules the build must reject), `pins.env`.
- `tests/` - tests, run with a small single-header runner (`tests/tinytest.h`).
- `tools/` - the float-free check, the FM-1 build (`fm1-build.sh`) and the scripts that
  generate the pitch table and `examples/demo.mid`.

## Plan

1. **Synth engine in plain C** (`engine/`), with no hardware code: two oscillators per voice
   (saw, pulse, square, noise, sync, cross-mod), a 24 dB resonant lowpass, a stepped highpass,
   two envelopes, an LFO and a stereo chorus.
2. **Desktop build** (`host/`): renders the engine to WAV or plays it live on Linux and macOS,
   for listening tests against reference recordings.
3. **CPU spike on the device**: one voice with filter and chorus on the FM-1 to measure load
   and decide the voice count (target 6, fallback 4).
4. **FM-1 firmware** (`fw/`): built on JieLi's Linux toolchain and AC79 SDK, the same way as
   [fm1-dock](https://github.com/joakimeriksson/fm1-dock), released through
   [zvenson/fm1-build](https://github.com/zvenson/fm1-build) for The Firmware Hub.

## Hardware notes

- The FM-1 runs on a JieLi AC791N (pi32v2 CPU). Expect fixed-point DSP.
- Keep the stock M-VAVE V15 firmware ("PC Firmware" at m-vave.com/download) for recovery.

## License

GPL-3.0. See [LICENSE](LICENSE).
