#!/usr/bin/env python3
"""The no-sound gate's coverage (tools/gate.sh, #140): which engine functions each clip reaches.
usage: python3 tools/gate_coverage.py OBJDIR SRCDIR RUNS
  OBJDIR  a --coverage build's object folder for engine/ (holds the .gcno files)
  SRCDIR  the tree's sources
  RUNS    one folder per clip, each holding that clip's .gcda files (GCOV_PREFIX, flattened)
Prints, for every function in SRCDIR/engine/*.c, the number of clips that ran any of its lines.
Exit 2, naming why, when gcov fails or writes no JSON for an object (Apple's llvm-cov gcov has no -j) or
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


def gcov_json(tmp, src):
    """Runs gcov -j on SRC's notes in TMP; returns the JSON files it wrote. CoverageError when gcov is
    missing, fails or writes none."""
    try:
        p = subprocess.run(["gcov", "-j", "-o", tmp, src], cwd=tmp, capture_output=True, text=True)
    except OSError as e:
        raise CoverageError(f"gcov: {e}") from None
    if p.returncode != 0:
        raise CoverageError(f"gcov failed on {os.path.basename(src)} (exit {p.returncode}): "
                            f"{(p.stderr or p.stdout).strip()[:300]}")
    out = glob.glob(os.path.join(tmp, "*.gcov.json.gz"))
    if not out:
        raise CoverageError(f"gcov wrote no JSON for {os.path.basename(src)} (a gcov without -j?)")
    return out


def stem(gcno):
    """("osc.o", "osc") for .../osc.o.gcno (gcc 11 on), ("osc", "osc") for .../osc.gcno."""
    obj = os.path.basename(gcno)[:-len(".gcno")]
    return obj, obj[:-2] if obj.endswith(".o") else obj


def reached(objdir, srcdir, run):
    """{(file, function)} with a line run in this clip."""
    out = set()
    data = 0
    with tempfile.TemporaryDirectory() as tmp:
        for gcno in glob.glob(os.path.join(objdir, "*.gcno")):
            obj, base = stem(gcno)
            gcda = os.path.join(run, obj + ".gcda")
            if not os.path.exists(gcda):
                continue
            data += 1
            # gcov wants NAME.gcno and NAME.gcda beside each other; gcc names them NAME.o.gcno, .o.gcda
            shutil.copy(gcno, os.path.join(tmp, base + ".gcno"))
            shutil.copy(gcda, os.path.join(tmp, base + ".gcda"))
            src = os.path.join(srcdir, "engine", base + ".c")
            for js in gcov_json(tmp, src):
                with gzip.open(js) as fh:
                    data = json.load(fh)   # (json: gcov's; data above counts the clip's .gcda files)
                for f in own(data, base, js):
                    for line in f["lines"]:
                        if line["count"] > 0 and line.get("function_name"):
                            out.add((base + ".c", line["function_name"]))
                os.remove(js)
    if not data:
        raise CoverageError(f"clip {os.path.basename(run)}: no run data (no .gcda for any object in {run})")
    return out


def own(data, base, js):
    """The entries of gcov's JSON DATA for BASE.c; CoverageError when there are none (its functions would
    vanish from the report instead of showing 0)."""
    mine = [f for f in data["files"] if os.path.basename(f["file"]) == base + ".c"]
    if not mine:
        raise CoverageError(f"gcov's {os.path.basename(js)} has no entry for {base}.c")
    return mine


def functions(objdir, srcdir):
    """Every (file, function) gcov knows in the engine, reached or not (from an empty run)."""
    out = set()
    with tempfile.TemporaryDirectory() as tmp:
        for gcno in glob.glob(os.path.join(objdir, "*.gcno")):
            obj, base = stem(gcno)
            shutil.copy(gcno, os.path.join(tmp, base + ".gcno"))
            src = os.path.join(srcdir, "engine", base + ".c")
            for js in gcov_json(tmp, src):
                with gzip.open(js) as fh:
                    for f in own(json.load(fh), base, js):
                        out.update((base + ".c", fn["name"]) for fn in f["functions"])
                os.remove(js)
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
