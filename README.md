# fm1-8p

An original firmware for the M-VAVE FM-1 that turns it into a polyphonic analog-style synth,
inspired by mid-80s two-oscillator polysynths with a chorus. It is not affiliated with or endorsed
by Roland or M-VAVE.

## Status

On the desktop, the fixed-point engine plays band-limited saw voices with an
ADSR amp envelope, up to 8 at once (6 by default), with voice stealing, the
sustain pedal and pitch bend, rendered from a MIDI file to a WAV.

## Build and run

Linux or macOS, with a C compiler (gcc or clang) and GNU make. No other
dependencies.

```sh
make                 # builds build/fm1-8p-render
make check           # runs the tests and the float-free engine check
make render-demo     # renders examples/demo.mid to build/demo.wav

./build/fm1-8p-render -r 44100 -v 6 -s amp_attack_ms=200 -s amp_release_ms=800 \
    song.mid song.wav
```

Run `./build/fm1-8p-render` with no arguments to list every option and
parameter.

The render tool plays one MIDI channel (`-c`, by default the channel of the
first note on) with 6 voices by default (`-v`). Each voice is mixed at
1/(2 x voices) so a full chord never clips, which makes a single note quieter
with more voices: -21.6 dBFS with 6, -6 dBFS with `-v 1`. After each render
the tool prints the voice count, channel and that level.

The engine uses only integer math. Its number formats, the sample rate and
block size rules, and how the float-free check works are in
[docs/fixed-point.md](docs/fixed-point.md).
How notes get voices (the stealing rule), the sustain pedal, pitch bend and
the output gain staging are in [docs/voices.md](docs/voices.md).

## Layout

- `engine/` - the synth engine: plain C99, fixed point, no hardware or OS code,
  no dynamic allocation. Public interface: `engine/fm1_8p.h`.
- `host/` - the desktop render tool: Standard MIDI File reader, WAV writer.
- `tests/` - tests, run with a small single-header runner (`tests/tinytest.h`).
- `tools/` - the float-free check and the scripts that generate the pitch
  table and `examples/demo.mid`.

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
