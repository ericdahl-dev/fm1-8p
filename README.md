# fm1-8p

An original firmware for the M-VAVE FM-1 that turns it into a polyphonic analog-style synth,
inspired by mid-80s two-oscillator polysynths with a chorus. It is not affiliated with or endorsed
by Roland or M-VAVE.

## Status

Planning. Nothing builds yet.

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
