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
    """A record; ValueError for a status not in STATUSES, or a field of the wrong type (kind, name: str;
    value: a dict or None; why, key: str or None)."""
    if status not in STATUSES:
        raise ValueError(f"{kind} {name}: unknown status {status!r}")
    if not isinstance(kind, str) or not isinstance(name, str):
        raise ValueError(f"{kind!r} {name!r}: kind and name must be text")
    if value is not None and not isinstance(value, dict):
        raise ValueError(f"{kind} {name}: value must be a dict or null, not {value!r}")
    for f, x in (("why", why), ("key", key)):
        if x is not None and not isinstance(x, str):
            raise ValueError(f"{kind} {name}: {f} must be text or null, not {x!r}")
    return {"kind": kind, "name": name, "status": status, "value": value, "why": why, "key": key}


def line(r):
    """A record as its JSON line (no newline)."""
    return json.dumps(r, sort_keys=True)


def parse(text):
    """A record from its JSON line; ValueError for anything that is not one (not JSON, a field missing or
    extra, an unknown status)."""
    try:
        d = json.loads(text, object_pairs_hook=_no_twice)
    except json.JSONDecodeError as e:
        raise ValueError(f"not a record: {text[:60]!r} ({e})") from None
    return checked(d)


def _no_twice(pairs):
    d = {}
    for k, v in pairs:
        if k in d:
            raise ValueError(f"not a record: {k!r} given twice")
        d[k] = v
    return d


def checked(d):
    """`d` again as a record, if it is one; ValueError for a field missing or extra, or an unknown status."""
    if not isinstance(d, dict) or sorted(d) != sorted(FIELDS):
        raise ValueError(f"not a record (its fields): {str(d)[:60]!r}")
    return record(d["kind"], d["name"], d["status"], d["value"], d["why"], d["key"])


def _number(x):
    import math
    if isinstance(x, bool) or not isinstance(x, (int, float)):
        return False
    return isinstance(x, int) or math.isfinite(x)   # (isfinite overflows on a huge int)


def verdict(expected, records, required=(), figures=None, compared=()):
    """The one verdict rule: (exit code, the records to report) for the (kind, name) pairs a run expects
    and the records it made. 2 (could not run): any error or missing, an expected name with no record
    (missing), a record nobody expected (but gone: on the base only), two records for one name, nothing
    expected, nothing compared (no record a pass or a fail; and per kind, for each kind in `compared`),
    a kind in `required` skipped, or a pass or fail of a kind in `figures` ({kind: field names}) without
    each named field a finite number (not a bool). 1: any fail. Else 0. Reported: every record that is not a
    plain pass, the missing ones made here among them. ValueError for a record that is not one."""
    expected = list(expected)                    # (read twice: an iterator would be empty the second time)
    want = set(expected)
    figures = figures or {}
    for kind, fields in figures.items():         # (a bare str would read as its letters)
        if isinstance(fields, str) or not fields:
            raise ValueError(f"figures for {kind}: name its fields, as a tuple ({fields!r})")
    n_compared = {}
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
        if r["status"] in ("pass", "fail") and r["kind"] in figures:
            bad = [f for f in figures[r["kind"]] if not _number((r["value"] or {}).get(f))]
            if bad:
                r = record(r["kind"], r["name"], "error",
                           why=f"a {r['status']} without its figures: {', '.join(bad)}")
        if r["status"] in ("pass", "fail"):
            n_compared[r["kind"]] = n_compared.get(r["kind"], 0) + 1
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
    if want and not n_compared:
        report.append(record("run", "", "error", why="nothing compared: no record is a pass or a fail"))
        code = 2
    for kind in compared:
        if not n_compared.get(kind):
            report.append(record(kind, "", "error", why=f"no {kind} compared: none is a pass or a fail"))
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
            for s_ in subs:                      # (os.walk would skip a linked folder's files)
                if os.path.islink(os.path.join(top, s_)):
                    raise OSError(f"{tree}: {os.path.relpath(os.path.join(top, s_), tree)} is a linked folder")
            out += [os.path.relpath(os.path.join(top, f), tree) for f in files]
    return sorted(out)


def _raise(e):
    raise e


def key(tree, dirs, inputs, toggles, tools):
    """A run's cache key, 16 hex digits: a hash of each of `tree`'s source files under `dirs` (its path and
    contents), of each named input file ({label: path}: the clip list, the MIDI written for the run, the
    scripts), of `toggles` ({name: value}, text) and of `tools` ({name: identity}, text: tool_version's line, a
    binary_identity). OSError for a file it can't read or a folder that is not there; ValueError for a tree
    with no source files or a toggle or tool that is not text. Never empty, never partial."""
    import hashlib
    for what, d in (("toggle", toggles), ("tool", tools)):
        for name, v in d.items():
            if not isinstance(name, str) or not isinstance(v, str):
                raise ValueError(f"{what} {name!r}: {v!r} is not text")   # (1 and "1" would hash alike)
    files = sources(tree, dirs)
    if not files:
        raise ValueError(f"{tree}: no source files under {', '.join(dirs) or '(none)'}")
    h = hashlib.sha256()
    def item(*x):                                # one JSON line an item: no separator can make two alike
        h.update((json.dumps(x) + "\n").encode())
    for rel in files:
        item("src", rel, _sha(os.path.join(tree, rel)))
    for label in sorted(inputs):
        item("input", label, _sha(inputs[label]))
    for name in sorted(toggles):
        item("toggle", name, toggles[name])
    for name in sorted(tools):
        item("tool", name, tools[name])
    return h.hexdigest()[:16]


def tool_version(argv, timeout=60):
    """The first line `argv` (a tool and its version flag) prints, for key()'s tools. OSError when the tool
    is not there, fails, prints nothing or takes longer than `timeout` seconds."""
    import subprocess
    if not argv:
        raise OSError("no tool given")
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


def binary_identity(path):
    """A binary's identity for key()'s tools: its path and the hash of its contents (a tool rebuilt in
    place, its version line the same, is another tool). OSError when it can't be read."""
    return f"{path} sha256 {_sha(path)}"
