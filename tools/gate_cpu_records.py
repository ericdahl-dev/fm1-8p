#!/usr/bin/env python3
"""The gate's CPU rows as records (#335): one record a layout's run ("cpu-layout", "plain@1024"), read
from fm1-emu's own log lines, and one a row over its layouts ("cpu", "plain"). The lines tools/gate_cpu.sh
prints are a view of them. Standard library only.
"""
import re

import gate_record as G

_SWEEP = re.compile(r"fm1-8p voices/worst permille ([0-9a-fA-F]{8})")
_OVERRUNS = re.compile(r"render: last.*?(?<![0-9])([0-9]+) overruns")
_CODE = re.compile(r"code ([0-9]+) B")


def max_voices(tree):
    """`tree`'s FM_MAX_VOICES (engine/fm1_8p.h): the sweep's voice counts, 1 to it, then U and F.
    ValueError when it is not defined exactly once (fm1-build.sh's sed would read 0 voices and a table
    of two rows); OSError when the header is not there."""
    import os
    with open(os.path.join(tree, "engine", "fm1_8p.h")) as f:
        found = re.findall(r"^#define FM_MAX_VOICES ([0-9]+)\b", f.read(), re.M)
    if len(found) != 1:
        raise ValueError(f"{tree}: FM_MAX_VOICES defined {len(found)} times in engine/fm1_8p.h")
    return int(found[0])


def sweep(log):
    """{voice count: worst permille} of the log's last sweep (from its last count-1 line on), the last
    figure for each count."""
    out = {}
    for m in _SWEEP.finditer(log):
        x = int(m.group(1), 16)
        count, pm = x >> 16, x & 0xFFFF
        if count == 1:
            out = {}
        out[count] = pm
    return out


def layout_record(row, layout, log, build, max_voices):
    """The record of one layout's run of `row`: its 8, U and F (% of a block), overruns and code bytes,
    from fm1-emu's log and the build's output. Missing, saying why, when the log's last sweep lacks any
    count 1..max_voices + 2 (U: the unison burst; F: the full patch), the log its render meter, or the
    build its code size: never a figure read as 0."""
    name = f"{row}@{layout}"
    pm = sweep(log)
    burst, full = max_voices + 1, max_voices + 2
    label = {burst: "U", full: "F"}
    why = None
    if not pm:
        why = "no voice counts in the log"
    else:
        gaps = [label.get(n, str(n)) for n in range(1, full + 1) if n not in pm]
        if gaps:
            why = f"no {', '.join(gaps)} in the log's last sweep"
    ov, code = _OVERRUNS.search(log), _CODE.search(build)
    if why is None and ov is None:
        why = "no render meter (\"render: last\") in the log"
    if why is None and code is None:
        why = "no code size (\"code N B\") in the build's output"
    if why:
        return G.record("cpu-layout", name, "missing", why=why)
    value = {"8": pm[max_voices] / 10, "U": pm[burst] / 10, "F": pm[full] / 10,
             "overruns": int(ov.group(1)), "code_bytes": int(code.group(1))}
    return G.record("cpu-layout", name, "pass", value=value)


def layout_line(r):
    """A layout's row as gate_cpu.sh has printed it (into OUT/layouts.txt)."""
    v = r["value"]
    figs = "".join(f"{k} =  {v[k]:.1f} %   " for k in ("8", "U", "F"))
    return f"{r['name']:<14} {figs} {v['overruns']} overruns code {v['code_bytes']} B"
