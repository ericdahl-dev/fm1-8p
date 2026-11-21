#!/usr/bin/env python3
"""The no-sound gate's keys and verdict (#335), for tools/gate.sh: one place builds every cache key
(gate_record.key()), and one rule (gate_record.verdict()) decides the exit. Standard library only.
"""
import os
import sys

import gate_record as G

# The folders of a tree a render or a CPU row depends on (gate.sh's key since #140)
DIRS = ("Makefile", "engine", "host", "firmware", "tools", "data", "examples")


def _dirs(tree):
    """DIRS that `tree` has, and the ones it lacks (an older base: before data/ and examples/), as a
    toggle: a tree that lacks one never shares a key with one that has it empty."""
    have = [d for d in DIRS if os.path.exists(os.path.join(tree, d))]
    return have, {"absent": " ".join(d for d in DIRS if d not in have)}


def render_key(tree, root, midi, cc):
    """The key `tree`'s renders are kept by: its sources, the case list it was rendered by (root's
    tools/gate_cases.py), every MIDI file of the run (the folder `midi`) and the host compiler (`cc`: its
    version line). ValueError for no source or no MIDI; OSError for a file it can't read."""
    have, toggles = _dirs(tree)
    names = sorted(os.listdir(midi))
    if not names:
        raise ValueError(f"{midi}: no MIDI")
    inputs = {"gate_cases.py": os.path.join(root, "tools", "gate_cases.py")}
    inputs.update({f"midi/{n}": os.path.join(midi, n) for n in names})
    return G.key(tree, have, inputs, toggles, {"cc": cc})


# gate_cpu.sh's toggles and their defaults (unset or empty: the default, as ${X:-default} reads it)
CPU_TOGGLES = {"GATE_CPU_LAYOUTS": "0 1024 2048 3072", "GATE_CPU_HINOTE": "1", "GATE_CPU_LIMIT": "64.8"}
CPU_FILES = ("gate_pad.c", "gate_stage.py", "gate_cpu_records.py", "gate_record.py")


def cpu_key(tree, root, script, env, tools):
    """The key `tree`'s CPU rows are kept by: its sources, `script` (gate_cpu.sh, or a stand-in), root's
    files the rows are staged, read and judged by (CPU_FILES), gate_cpu.sh's toggles from `env` and `tools`
    ({name: identity}, text: gate_cpu.sh --tools' lines). A row a toggle adds or skips never comes back
    missing; a rule changed in one of those files never leaves the old rows standing."""
    have, toggles = _dirs(tree)
    inputs = {"gate_cpu.sh": script}
    inputs.update({f: os.path.join(root, "tools", f) for f in CPU_FILES})
    toggles.update({n: env.get(n) or d for n, d in CPU_TOGGLES.items()})
    return G.key(tree, have, inputs, toggles, tools)
