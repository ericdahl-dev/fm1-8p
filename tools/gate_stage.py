#!/usr/bin/env python3
"""The CPU rows' staging edits (#335): a copy of a tree edited so its CPU test's step F plays one row
(tools/gate_cpu.sh), at one layout. What gate_cpu.sh did with GNU sed -i, which macOS's sed can't run;
each edit's line must be there exactly once, or it stops (a second match edited quietly would time
another patch). Standard library only.

usage: gate_stage.py DIR MODE BYTES PAD      (DIR: the copy; MODE: plain, sync, xmod, ring, ui, seq, each
                                             maybe -hn; BYTES: never-run code ahead of the engine, 0 none;
                                             PAD: tools/gate_pad.c)
"""
import os
import re
import sys

CPU_TEST = os.path.join("firmware", "cpu_test.c")
CPU_MOD = os.path.join("firmware", "fm1_8p_cpu", "fm1_8p_cpu.c")
# The high-note rows' chord (#269): notes 115..125, of which step F plays 115..121
HN_CHORD = ("static const uint8_t CHORD[FM_MAX_VOICES] = {115, 117, 119, 121, 122, 123, 124, 125};"
            "   /* #269: the high notes */")
ADD = {"plain": [], "ui": [],
       "sync": ["    {FM_PARAM_OSC_SYNC, 1},"],
       "xmod": ["    {FM_PARAM_OSC_RING, 1},"],
       "ring": ["    {FM_PARAM_OSC_SYNC, 1},", "    {FM_PARAM_OSC_RING, 1},"],
       "seq": ["    {FM_PARAM_OSC_SYNC, 1},", "    {FM_PARAM_OSC_RING, 1},"]}   # (seq: ring too)


class StageError(Exception):
    """An edit's line is not there exactly once, or the mode is not one."""


def _read(path):
    with open(path) as f:
        return f.read()


def _write(path, text):
    with open(path, "w") as f:
        f.write(text)


def _once(text, pattern, what, repl):
    """`text` with the one line matching `pattern` replaced by repl(match); StageError otherwise."""
    found = list(re.finditer(pattern, text, re.M))
    if not found:
        raise StageError(f"gate_cpu: {what} not found")
    if len(found) > 1:
        raise StageError(f"gate_cpu: {what} found {len(found)} times")
    m = found[0]
    return text[:m.start()] + repl(m) + text[m.end():]


def stage(d, mode, nbytes, pad):
    """Edits the tree copy `d` for `mode` at `nbytes` of dead code (see the module's usage)."""
    base = mode[:-3] if mode.endswith("-hn") else mode
    if base not in ADD:
        raise StageError(f"gate_cpu: {mode}: not a row")
    t = os.path.join(d, CPU_TEST)
    s = _read(t)
    if mode.endswith("-hn"):
        s = _once(s, r"^static const uint8_t CHORD\[FM_MAX_VOICES\] = \{[0-9, ]*\};.*$", "step F's chord",
                  lambda m: HN_CHORD)
    if ADD[base]:
        # step F's highpass line, whatever its step (1 before #136, the bass boost after): the params after it
        s = _once(s, r"^    \{FM_PARAM_HIGHPASS_STEP, [A-Z0-9_]*\},$", "step F's patch",
                  lambda m: "\n".join([m.group(0)] + ADD[base]))
    _write(t, s)
    if nbytes > 0:
        p = _once(_read(pad), r'"\.space [0-9]*"', "the pad's .space", lambda m: f'".space {nbytes}"')
        _write(os.path.join(d, "engine", "aa_pad.c"), p)
        f = os.path.join(d, CPU_MOD)
        s = _read(f)
        for pat in (r"^static int init\(const hb_host_t \*h\)$", r"^    H = h;$"):   # (both, before editing)
            if len(re.findall(pat, s, re.M)) != 1:
                raise StageError("gate_cpu: pad hook not found")
        s = _once(s, r"^static int init\(const hb_host_t \*h\)$", "pad hook",
                  lambda m: "int32_t fm1_8p_gate_pad(int32_t x);\n" + m.group(0))
        s = _once(s, r"^    H = h;$", "pad hook", lambda m: m.group(0) + "\n    if (fm1_8p_gate_pad(3) == 7) return 1;")
        _write(f, s)


def main(argv):
    d, mode, nbytes, pad = argv[1:5]
    try:
        stage(d, mode, int(nbytes), pad)
    except (StageError, OSError, ValueError) as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
