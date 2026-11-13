#!/usr/bin/env python3
"""The no-sound gate's coverage (tools/gate.sh, #140): which engine functions each clip reaches.
usage: python3 tools/gate_coverage.py OBJDIR SRCDIR RUNS
  OBJDIR  a --coverage build's object folder for engine/ (holds the .gcno files)
  SRCDIR  the tree's sources
  RUNS    one folder per clip, each holding that clip's .gcda files (GCOV_PREFIX, flattened)
Prints, for every function in SRCDIR/engine/*.c, the number of clips that ran any of its lines."""
import glob
import gzip
import json
import os
import shutil
import subprocess
import sys
import tempfile


def stem(gcno):
    """("osc.o", "osc") for .../osc.o.gcno (gcc 11 on), ("osc", "osc") for .../osc.gcno."""
    obj = os.path.basename(gcno)[:-len(".gcno")]
    return obj, obj[:-2] if obj.endswith(".o") else obj


def reached(objdir, srcdir, run):
    """{(file, function)} with a line run in this clip."""
    out = set()
    with tempfile.TemporaryDirectory() as tmp:
        for gcno in glob.glob(os.path.join(objdir, "*.gcno")):
            obj, base = stem(gcno)
            gcda = os.path.join(run, obj + ".gcda")
            if not os.path.exists(gcda):
                continue
            # gcov wants NAME.gcno and NAME.gcda beside each other; gcc names them NAME.o.gcno, .o.gcda
            shutil.copy(gcno, os.path.join(tmp, base + ".gcno"))
            shutil.copy(gcda, os.path.join(tmp, base + ".gcda"))
            src = os.path.join(srcdir, "engine", base + ".c")
            subprocess.run(["gcov", "-j", "-o", tmp, src], cwd=tmp, capture_output=True, check=True)
            for js in glob.glob(os.path.join(tmp, "*.gcov.json.gz")):
                with gzip.open(js) as fh:
                    data = json.load(fh)
                for f in data["files"]:
                    if os.path.basename(f["file"]) != base + ".c":
                        continue
                    for line in f["lines"]:
                        if line["count"] > 0 and line.get("function_name"):
                            out.add((base + ".c", line["function_name"]))
                os.remove(js)
    return out


def functions(objdir, srcdir):
    """Every (file, function) gcov knows in the engine, reached or not (from an empty run)."""
    out = set()
    with tempfile.TemporaryDirectory() as tmp:
        for gcno in glob.glob(os.path.join(objdir, "*.gcno")):
            obj, base = stem(gcno)
            shutil.copy(gcno, os.path.join(tmp, base + ".gcno"))
            src = os.path.join(srcdir, "engine", base + ".c")
            subprocess.run(["gcov", "-j", "-o", tmp, src], cwd=tmp, capture_output=True)
            for js in glob.glob(os.path.join(tmp, "*.gcov.json.gz")):
                with gzip.open(js) as fh:
                    for f in json.load(fh)["files"]:
                        if os.path.basename(f["file"]) == base + ".c":
                            out.update((base + ".c", fn["name"]) for fn in f["functions"])
                os.remove(js)
    return out


def main():
    objdir, srcdir, runs = sys.argv[1:4]
    count = {k: 0 for k in functions(objdir, srcdir)}
    for run in sorted(glob.glob(os.path.join(runs, "*"))):
        for k in reached(objdir, srcdir, run):
            count[k] = count.get(k, 0) + 1
    print("clips reaching each engine function (head):")
    for (f, fn), n in sorted(count.items(), key=lambda kv: (kv[1], kv[0])):
        print(f"{n:6d}  {f:10s} {fn}{'   <- fewer than 2' if n < 2 else ''}")


if __name__ == "__main__":
    main()
