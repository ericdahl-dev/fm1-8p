#!/usr/bin/env python3
"""The gate's results as records (#335): one result a record, one JSON line, with "no result" a state of
its own (missing, error), never read as 0 or as a pass. Standard library only (Python 3.8+, macOS's own).

A record: {"kind", "name", "status", "value", "why", "key"}.
  kind     what was checked: "clip", "cpu", "tone", ... (the caller's word)
  name     which one: a clip, a CPU row (and layout), a tone
  status   one of STATUSES; anything else is an error, never ignored
  value    the figures (a dict), or null when there are none
  why      a reason, for anything but a plain pass, or null
  key      the cache key the result was made under (key()), or null
"""
import json
import os

# pass: ran, good. fail: ran, bad. missing: expected and not there (or only part of it). error: could not
# run. skipped: not run on purpose (why says why). new / gone: on one side only (head / base).
STATUSES = ("pass", "fail", "missing", "error", "skipped", "new", "gone")
FIELDS = ("kind", "name", "status", "value", "why", "key")


def record(kind, name, status, value=None, why=None, key=None):
    """A record; ValueError for a status not in STATUSES."""
    if status not in STATUSES:
        raise ValueError(f"{kind} {name}: unknown status {status!r}")
    return {"kind": kind, "name": name, "status": status, "value": value, "why": why, "key": key}


def line(r):
    """A record as its JSON line (no newline)."""
    return json.dumps(r, sort_keys=True)


def parse(text):
    """A record from its JSON line; ValueError for anything that is not one (not JSON, a field missing or
    extra, an unknown status)."""
    try:
        d = json.loads(text)
    except json.JSONDecodeError as e:
        raise ValueError(f"not a record: {text[:60]!r} ({e})") from None
    return checked(d)


def checked(d):
    """`d` again as a record, if it is one; ValueError for a field missing or extra, or an unknown status."""
    if not isinstance(d, dict) or sorted(d) != sorted(FIELDS):
        raise ValueError(f"not a record (its fields): {str(d)[:60]!r}")
    return record(d["kind"], d["name"], d["status"], d["value"], d["why"], d["key"])


def verdict(expected, records, required=()):
    """The one verdict rule: (exit code, the records to report) for the (kind, name) pairs a run expects
    and the records it made. 2 (could not run): any error or missing, an expected name with no record
    (missing), a record nobody expected (but gone: on the base only), two records for one name, nothing
    expected, or a kind in `required` skipped. 1: any fail. Else 0. Reported: every record that is not a
    plain pass, the missing ones made here among them. ValueError for a record with an unknown status."""
    want = set(expected)
    seen = {}
    report = []
    code = 0
    if not want:
        code = 2
        report.append(record("run", "", "error", why="nothing expected: nothing to judge"))
    for r in records:
        r = checked(r)
        k = (r["kind"], r["name"])
        if k in seen:
            r = record(r["kind"], r["name"], "error", why="two records for one name")
        elif k not in want and r["status"] != "gone":
            r = record(r["kind"], r["name"], "error", why=f"not expected ({r['status']})")
        seen[k] = r
        if r["status"] == "skipped" and r["kind"] in required:
            r = record(r["kind"], r["name"], "error", why=f"skipped, but required: {r['why']}")
        if r["status"] in ("error", "missing"):
            code = 2
        elif r["status"] == "fail" and code == 0:
            code = 1
        if r["status"] != "pass":
            report.append(r)
    for kind, name in expected:
        if (kind, name) not in seen:
            report.append(record(kind, name, "missing", why="no record"))
            code = 2
    return code, report


def _sha(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as fh:                 # (OSError when it can't be read: the key stops)
        for b in iter(lambda: fh.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def sources(tree, dirs):
    """The files under `dirs` of `tree` (each a folder or a file), relative and sorted, no __pycache__.
    OSError for one that is not there."""
    out = []
    for d in dirs:
        p = os.path.join(tree, d)
        if os.path.isfile(p):
            out.append(d)
            continue
        if not os.path.isdir(p):
            raise FileNotFoundError(f"{tree}: no {d}")
        for top, subs, files in os.walk(p, onerror=_raise):
            subs[:] = [s for s in subs if s != "__pycache__"]
            out += [os.path.relpath(os.path.join(top, f), tree) for f in files]
    return sorted(out)


def _raise(e):
    raise e


def key(tree, dirs, inputs, toggles, tools):
    """A run's cache key, 16 hex digits: a hash of each of `tree`'s source files under `dirs` (its path and
    contents), of each named input file ({label: path}: the clip list, the MIDI written for the run, the
    scripts), of `toggles` ({name: value}) and of `tools` ({name: identity}: tool_version's line, a
    binary's path). OSError for a file it can't read or a folder that is not there; ValueError for a tree
    with no source files. Never empty, never partial."""
    import hashlib
    files = sources(tree, dirs)
    if not files:
        raise ValueError(f"{tree}: no source files under {', '.join(dirs) or '(none)'}")
    h = hashlib.sha256()
    for rel in files:
        h.update(f"src {rel} {_sha(os.path.join(tree, rel))}\n".encode())
    for label in sorted(inputs):
        h.update(f"input {label} {_sha(inputs[label])}\n".encode())
    for name in sorted(toggles):
        h.update(f"toggle {name}={toggles[name]}\n".encode())
    for name in sorted(tools):
        h.update(f"tool {name} {tools[name]}\n".encode())
    return h.hexdigest()[:16]


def tool_version(argv, timeout=60):
    """The first line `argv` (a tool and its version flag) prints, for key()'s tools. OSError when the tool
    is not there, fails, prints nothing or takes longer than `timeout` seconds."""
    import subprocess
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise OSError(f"{argv[0]}: no version in {timeout} s") from None
    first = p.stdout.decode(errors="replace").strip().splitlines()[:1]
    if p.returncode != 0 or not first:
        raise OSError(f"{argv[0]}: no version (exit {p.returncode})")
    return first[0].strip()


def load(path):
    """The records in the file at `path`, one a line. ValueError, naming the line, for a line that is not
    one (a blank line too: a run that was cut off is not quietly short)."""
    out = []
    with open(path) as fh:
        for n, text in enumerate(fh, 1):
            try:
                out.append(parse(text))
            except ValueError as e:
                raise ValueError(f"{path}:{n}: {e}") from None
    return out
