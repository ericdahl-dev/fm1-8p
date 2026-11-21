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


def _dock(dock):
    """fm1-dock's identity: its commit, and whether it has local edits; or that it is not a checkout."""
    import subprocess
    if not os.path.exists(os.path.join(dock, ".git")):
        return "not a git checkout"
    head = G.tool_version(["git", "-C", dock, "rev-parse", "HEAD"])
    p = subprocess.run(["git", "-C", dock, "status", "--porcelain"], stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL)
    if p.returncode != 0:
        raise OSError(f"{dock}: git status failed (exit {p.returncode})")
    return head + (" (modified)" if p.stdout.strip() else "")


def fm1_tools(live, toolchain, dock):
    """The FM-1 tools' identity for cpu_key(): fm1-live by its contents (a rebuilt emulator is another
    tool), the Jieli clang by its version line, fm1-dock by its commit. OSError when one can't be had.
    Out: AC79_SDK (pinned files, checked by fm1-build.sh) and fm1-dock's Python (its venv)."""
    return {"fm1-live": G.binary_identity(live),
            "jieli-clang": G.tool_version([os.path.join(toolchain, "pi32v2", "bin", "clang"), "--version"]),
            "fm1-dock": _dock(dock)}


def _cases(o):
    """The clip names of a render folder's case list (o/.cases: name, MIDI, args; tab-separated)."""
    with open(os.path.join(o, ".cases"), newline="") as f:
        return [line.rstrip("\n").split("\t", 1)[0] for line in f if line.rstrip("\n")]


def _same(a, b):
    with open(a, "rb") as fa, open(b, "rb") as fb:
        while True:
            x, y = fa.read(1 << 20), fb.read(1 << 20)
            if x != y:
                return False
            if not x:
                return True


def clip_records(base, head):
    """The clips of the render folders `base` and `head` compared byte for byte, as (records, lines): a
    pass, a fail ("differs: NAME"), new (head only), gone (base only), and the count line; in gate.sh's
    order (head's list, then base's). A clip the base lists but did not render is an error, and the
    lines stop there, as gate.sh's did."""
    b, h = _cases(base), _cases(head)
    bset, hset = set(b), set(h)
    records, lines = [], []
    n = d = new = 0
    for name in h:
        if name not in bset:
            new += 1
            lines.append(f"new: {name}")
            records.append(G.record("clip", name, "new", why="not on the base's list"))
            continue
        bw, hw = os.path.join(base, name + ".wav"), os.path.join(head, name + ".wav")
        if not os.path.isfile(bw):
            records.append(G.record("clip", name, "error", why=f"{name}: listed on the base but not rendered"))
            return records, lines
        if not os.path.isfile(hw):               # (cmp read it as differs)
            records.append(G.record("clip", name, "error", why=f"{name}: listed on HEAD but not rendered"))
            return records, lines
        n += 1
        if _same(bw, hw):
            records.append(G.record("clip", name, "pass"))
        else:
            d += 1
            lines.append(f"differs: {name}")
            records.append(G.record("clip", name, "fail", why="differs"))
    for name in b:
        if name not in hset:
            lines.append(f"gone: {name}")
            records.append(G.record("clip", name, "gone", why="not on HEAD's list"))
    if n:
        lines.append(f"{d} of {n} clips differ" + (f" ({new} new, not compared)" if new else ""))
    return records, lines


def main(argv):
    """gate.sh's and gate_cpu.sh's calls:
      fm1-tools LIVE TOOLCHAIN DOCK     fm1_tools() as JSON (gate_cpu.sh --tools)"""
    import json
    cmd, a = argv[1], argv[2:]
    try:
        if cmd == "fm1-tools":
            print(json.dumps(fm1_tools(*a), sort_keys=True))
            return 0
    except (OSError, ValueError) as e:
        print(f"gate: {cmd}: {e}", file=sys.stderr)
        return 2
    print(f"gate_run: {cmd}: not a command", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
