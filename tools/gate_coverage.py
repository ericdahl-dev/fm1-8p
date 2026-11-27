#!/usr/bin/env python3
"""The no-sound gate's coverage (tools/gate.sh, #140): which engine functions each clip reaches.
usage: python3 tools/gate_coverage.py OBJDIR SRCDIR RUNS
  OBJDIR  a --coverage build's object folder for engine/ (holds the .gcno files)
  SRCDIR  the tree's sources
  RUNS    one folder per clip, each holding that clip's .gcda files (GCOV_PREFIX, flattened)
Prints, for every function in SRCDIR/engine/*.c, the number of clips that ran any of its lines.
Exit 2, naming why, when gcov fails or writes nothing for an object (gcc's JSON, or Apple's -i text) or
none for its source, when it lists no engine function, or when there is no clip, or a clip with no run
data: never a count of 0 read as coverage (#335)."""
import glob
import gzip
import json
import os
import shutil
import subprocess
import sys
import tempfile


class CoverageError(Exception):
    """gcov could not say what ran."""


def _gcov(flag, tmp, src):
    """gcov FLAG on SRC's notes in TMP: its finished process. CoverageError when gcov is missing."""
    try:
        return subprocess.run(["gcov", flag, "-o", tmp, src], cwd=tmp, capture_output=True, text=True)
    except OSError as e:
        raise CoverageError(f"gcov: {e}") from None


def _failed(p, src):
    return CoverageError(f"gcov failed on {os.path.basename(src)} (exit {p.returncode}): "
                         f"{(p.stderr or p.stdout).strip()[:300]}")


def gcov_files(tmp, src):
    """gcov's entries for SRC's notes (and run data) in TMP, as its JSON's "files": gcc's gcov -j, or
    Apple's (llvm-cov gcov, which refuses -j) -i. CoverageError when gcov is missing, fails or writes
    nothing."""
    p = _gcov("-j", tmp, src)
    if p.returncode != 0 and "'-j'" in p.stderr:   # Apple's: "Unknown command line argument '-j'"
        return gcov_text(tmp, src)
    if p.returncode != 0:
        raise _failed(p, src)
    out = glob.glob(os.path.join(tmp, "*.gcov.json.gz"))
    if not out:
        raise CoverageError(f"gcov wrote no JSON for {os.path.basename(src)} (a gcov without -j?)")
    files = []
    for js in out:
        with gzip.open(js) as fh:
            files += json.load(fh)["files"]
        os.remove(js)
    return files


def gcov_text(tmp, src):
    """Apple's gcov -i: its intermediate text (file:NAME, then function:LINE,COUNT,NAME), as gcov_files'
    entries, a function's entry count standing for its lines'."""
    p = _gcov("-i", tmp, src)
    if p.returncode != 0:
        raise _failed(p, src)
    out = glob.glob(os.path.join(tmp, "*.gcov"))
    if not out:
        raise CoverageError(f"gcov wrote no intermediate text for {os.path.basename(src)}")
    files = []
    for t in out:
        with open(t) as fh:
            for row in fh:
                key, _, value = row.rstrip("\n").partition(":")
                if key == "file":
                    files.append({"file": value, "functions": [], "lines": []})
                elif key == "function" and files:
                    _, count, name = value.split(",", 2)
                    files[-1]["functions"].append({"name": name})
                    files[-1]["lines"].append({"count": int(count), "function_name": name})
        os.remove(t)
    return files


def stem(gcno):
    """("osc.o", "osc") for .../osc.o.gcno (gcc 11 on), ("osc", "osc") for .../osc.gcno."""
    obj = os.path.basename(gcno)[:-len(".gcno")]
    return obj, obj[:-2] if obj.endswith(".o") else obj


def reached(objdir, srcdir, run):
    """{(file, function)} with a line run in this clip."""
    out = set()
    n_gcda = 0
    with tempfile.TemporaryDirectory() as tmp:
        for gcno in glob.glob(os.path.join(objdir, "*.gcno")):
            obj, base = stem(gcno)
            gcda = os.path.join(run, obj + ".gcda")
            if not os.path.exists(gcda):
                continue
            n_gcda += 1
            # gcov wants NAME.gcno and NAME.gcda beside each other; gcc names them NAME.o.gcno, .o.gcda
            shutil.copy(gcno, os.path.join(tmp, base + ".gcno"))
            shutil.copy(gcda, os.path.join(tmp, base + ".gcda"))
            src = os.path.join(srcdir, "engine", base + ".c")
            for f in own(gcov_files(tmp, src), base):
                for line in f["lines"]:
                    if line["count"] > 0 and line.get("function_name"):
                        out.add((base + ".c", line["function_name"]))
    if not n_gcda:
        raise CoverageError(f"clip {os.path.basename(run)}: no run data (no .gcda for any object in {run})")
    return out


def own(files, base):
    """The entries of gcov's FILES (gcov_files) for BASE.c; CoverageError when there are none (its
    functions would vanish from the report instead of showing 0)."""
    mine = [f for f in files if os.path.basename(f["file"]) == base + ".c"]
    if not mine:
        raise CoverageError(f"gcov's output has no entry for {base}.c")
    return mine


def functions(objdir, srcdir):
    """Every (file, function) gcov knows in the engine, reached or not (from an empty run)."""
    out = set()
    with tempfile.TemporaryDirectory() as tmp:
        for gcno in glob.glob(os.path.join(objdir, "*.gcno")):
            obj, base = stem(gcno)
            shutil.copy(gcno, os.path.join(tmp, base + ".gcno"))
            src = os.path.join(srcdir, "engine", base + ".c")
            for f in own(gcov_files(tmp, src), base):
                out.update((base + ".c", fn["name"]) for fn in f["functions"])
    return out


def main():
    objdir, srcdir, runs = sys.argv[1:4]
    try:
        count = {k: 0 for k in functions(objdir, srcdir)}
        if not count:
            raise CoverageError(f"gcov listed no engine function (no .gcno in {objdir}?)")
        clips = sorted(d for d in glob.glob(os.path.join(runs, "*")) if os.path.isdir(d))
        if not clips:
            raise CoverageError(f"no clip folder in {runs}: nothing ran")
        for run in clips:
            for k in reached(objdir, srcdir, run):
                count[k] = count.get(k, 0) + 1
    except CoverageError as e:
        print(f"gate_coverage: {e}", file=sys.stderr)
        sys.exit(2)
    print("clips reaching each engine function (head):")
    for (f, fn), n in sorted(count.items(), key=lambda kv: (kv[1], kv[0])):
        print(f"{n:6d}  {f:10s} {fn}{'   <- fewer than 2' if n < 2 else ''}")


if __name__ == "__main__":
    main()
