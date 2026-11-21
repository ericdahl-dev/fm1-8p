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


def render_key(tree, root, midi, cc, env):
    """The key `tree`'s renders are kept by: its sources, the case list it was rendered by (root's
    tools/gate_cases.py), every MIDI file of the run (the folder `midi`), the host compiler (`cc`: its
    version line) and what `env` gives make for it: CC and CFLAGS (the Makefile's ?=, so unset is not
    empty). ValueError for no source or no MIDI; OSError for a file it can't read."""
    have, toggles = _dirs(tree)
    toggles.update({n: env[n] if n in env else "(unset)" for n in ("CC", "CFLAGS")})
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
    import hashlib

    def git(*a):
        p = subprocess.run(["git", "-C", dock] + list(a), stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if p.returncode != 0:
            raise OSError(f"{dock}: git {a[0]} failed (exit {p.returncode})")
        return p.stdout
    head = G.tool_version(["git", "-C", dock, "rev-parse", "HEAD"])
    diff = git("diff", "HEAD", "--binary")
    new = sorted(x.decode(errors="surrogateescape") for x in git("ls-files", "--others", "--exclude-standard",
                                                                  "-z").split(b"\0") if x)
    if not diff and not new:
        return head
    h = hashlib.sha256(diff)                     # (each edit its own dock: "modified" alone kept the old rows)
    for f in new:
        h.update(f"\0{f}\0{G.binary_identity(os.path.join(dock, f))}".encode(errors="surrogateescape"))
    return f"{head} (modified: {h.hexdigest()[:16]})"


def _clang(clang):
    """The Jieli clang by its version line and its bytes (fm1-build.sh pins only its folder's name)."""
    return f"{G.tool_version([clang, '--version'])}; {G.binary_identity(clang)}"


def fm1_tools(live, toolchain, dock):
    """The FM-1 tools' identity for cpu_key(): fm1-live by its contents (a rebuilt emulator is another
    tool), the Jieli clang by its version line and bytes, fm1-dock by its commit and the hash of its
    local edits. OSError when one can't be had.
    Out: AC79_SDK (pinned files, checked by fm1-build.sh) and fm1-dock's Python (its venv)."""
    return {"fm1-live": G.binary_identity(live),
            "jieli-clang": _clang(os.path.join(toolchain, "pi32v2", "bin", "clang")),
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


def host_cc(env):
    """The host compiler's version line: CC as make takes it (the Makefile's CC ?= cc)."""
    import shlex
    return G.tool_version(shlex.split(env.get("CC") or "cc") + ["--version"])


def _put(path, records, mode="w"):
    with open(path, mode) as f:
        f.writelines(G.line(r) + "\n" for r in records)


def judge(records, head, cpu):
    """The gate's exit: verdict() over the run's records, expecting every clip of HEAD's render folder
    `head` and, when `cpu`, HEAD's CPU run. 2 when it could not run (nothing compared among them), 1
    when a clip differs or HEAD's CPU rows failed, else 0."""
    expected = [("clip", n) for n in _cases(head)] + ([("cpu-run", "head")] if cpu else [])
    return G.verdict(expected, records, compared=("clip",))[0]


def main(argv):
    """gate.sh's and gate_cpu.sh's calls:
      render-key TREE ROOT MIDI          render_key(), with the host compiler (CC, or cc)
      cpu-key TREE ROOT SCRIPT TOOLS     cpu_key(), with the environment's toggles; TOOLS: SCRIPT
                                         --tools' JSON file
      clips BASE HEAD RECORDS            clip_records() into RECORDS (anew), its lines on stdout; exit 2
                                         when one could not be compared or none was
      cpu-record RECORDS EXIT            HEAD's CPU run as a record: gate_cpu.sh's exit 0 a pass, 1 a fail
                                         (a mean over the limit), anything else an error
      judge RECORDS HEAD [cpu]           judge()
      fm1-tools LIVE TOOLCHAIN DOCK      fm1_tools() as JSON (gate_cpu.sh --tools)"""
    import json
    cmd, a = argv[1], argv[2:]
    try:
        if cmd == "render-key":
            print(render_key(a[0], a[1], a[2], host_cc(os.environ), os.environ))
            return 0
        if cmd == "cpu-key":
            with open(a[3]) as f:
                tools = json.load(f)
            print(cpu_key(a[0], a[1], a[2], os.environ, tools))
            return 0
        if cmd == "clips":
            records, lines = clip_records(a[0], a[1])
            _put(a[2], records)
            for x in lines:
                print(x)
            bad = [r for r in records if r["status"] == "error"]
            if bad:
                print(f"gate: {bad[0]['why']}", file=sys.stderr)
                return 2
            if not any(r["status"] in ("pass", "fail") for r in records):
                print("gate: no clip on both trees: nothing compared", file=sys.stderr)
                return 2
            return 0
        if cmd == "cpu-record":
            rc = a[1]
            st = {"0": "pass", "1": "fail"}.get(rc, "error")
            why = None if st == "pass" else f"gate_cpu.sh exit {rc}"
            _put(a[0], [G.record("cpu-run", "head", st, why=why)], mode="a")
            return 0
        if cmd == "judge":
            return judge(G.load(a[0]), a[1], a[2:] == ["cpu"])
        if cmd == "fm1-tools":
            print(json.dumps(fm1_tools(*a), sort_keys=True))
            return 0
    except (OSError, ValueError, IndexError) as e:
        print(f"gate: {cmd}: {e}", file=sys.stderr)
        return 2
    print(f"gate_run: {cmd}: not a command", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
