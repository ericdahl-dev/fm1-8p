#!/usr/bin/env python3
"""#48's "saving never drops audio" check of an fm1-emu run (tools/fm1-build.sh emu-save).

The run holds a chord from PRESS ms, taps SAVE twice while it sounds (SAVE waits for silence), and
lets go at RELEASE ms. fm1-emu doesn't model the flash erase's stall, so this checks where the write
lands instead: one bank write (fm1-dock's "bank saved"), after the release, with silence around it,
and no gap in the sound while the chord is held. A write stalls the sound only if something sounds.

usage: emu_save.py LOG WAV PRESS_MS RELEASE_MS     (exit 1 with what failed)
Log times are the host's ms; the audio starts later (the boot), so the chord's onset, at PRESS, ties
the two clocks together."""
import re
import struct
import sys
import wave

ONSET = 200                             # a sample above this (of 32767) is the chord starting
SILENT = 16                             # the peak around a write must be at or below this (-66 dBFS)
QUIET_BEFORE_MS, QUIET_AFTER_MS = 50, 150   # the window around a write that must be silent
GAP_MS = 5                              # this long at exactly 0 while the chord is held is a gap
SETTLE_MS = 100                         # after PRESS: the attack, before gaps are looked for


def _left(path):
    with wave.open(path, "rb") as w:
        fs, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        data = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    return fs, data[::ch]


def check(log_text, wav_path, press_ms, release_ms):
    """What is wrong with the run, as a list of sentences ([]: it passed)."""
    bad = []
    if "fm1-8p: ready" not in log_text:
        bad.append("the module never started (no \"fm1-8p: ready\")")
    m = re.search(r"render: last.*?, (\d+) overruns", log_text)
    if not m:
        bad.append("no render meter in the log: the run ended early")
    elif m.group(1) != "0":
        bad.append("%s overruns: the module fell behind real time" % m.group(1))
    saves = [int(t) for t in re.findall(r"^(\d+) bank saved", log_text, re.M)]
    fs, s = _left(wav_path)
    onset = next((i for i, v in enumerate(s) if abs(v) > ONSET), None)
    if onset is None:
        return bad + ["no onset: the chord never sounded"]

    def frame(ms):                      # the log's ms as an audio frame
        return onset + (ms - press_ms) * fs // 1000

    gap = GAP_MS * fs // 1000
    run = 0
    for i in range(max(0, frame(press_ms + SETTLE_MS)), min(len(s), frame(release_ms))):
        run = run + 1 if s[i] == 0 else 0
        if run >= gap:
            bad.append("a gap in the held chord at %d ms" % (press_ms + (i - onset) * 1000 // fs))
            break
    if not saves:
        bad.append("no write (no \"bank saved\"): SAVE never wrote")
    elif len(saves) > 1:
        bad.append("%d writes (\"bank saved\" at %s ms): one SAVE is one write"
                   % (len(saves), ", ".join(map(str, saves))))
    for t in saves:
        if t < release_ms:
            bad.append("a write at %d ms, before the release (%d ms): while the chord sounds" % (t, release_ms))
            continue
        a, b = max(0, frame(t - QUIET_BEFORE_MS)), min(len(s), frame(t + QUIET_AFTER_MS))
        peak = max((abs(v) for v in s[a:b]), default=0)
        if peak > SILENT:
            bad.append("the write at %d ms is not silent: peak %d around it (at most %d)" % (t, peak, SILENT))
    return bad


def main(argv):
    if len(argv) != 5:
        sys.stderr.write(__doc__)
        return 2
    with open(argv[1]) as f:
        text = f.read()
    bad = check(text, argv[2], int(argv[3]), int(argv[4]))
    saves = re.findall(r"^(\d+) bank saved", text, re.M)
    print("A chord held from %s ms, SAVE tapped twice while it sounds, released at %s ms." % (argv[3], argv[4]))
    print("The write (\"bank saved\") at %s ms." % (", ".join(saves) or "none"))
    print()
    if bad:
        print("**Failed:**")
        for b in bad:
            print("- " + b)
        return 1
    print("Passed: one write, after the release, silent around it (peak at most %d), no gap while held." % SILENT)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
