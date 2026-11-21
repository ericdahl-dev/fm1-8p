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
             "overruns": int(ov.group(1)), "code_bytes": int(code.group(1)),
             "layout": int(layout), "shipped": int(layout) == 0}   # (0: no dead code, the build that ships)
    return G.record("cpu-layout", name, "pass", value=value)


def layout_line(r):
    """A layout's row as gate_cpu.sh has printed it (into OUT/layouts.txt)."""
    v = r["value"]
    figs = "".join(f"{k} =  {v[k]:.1f} %   " for k in ("8", "U", "F"))
    return f"{r['name']:<14} {figs} {v['overruns']} overruns code {v['code_bytes']} B"


def _limit(text):
    import math
    x = float(text)                              # (ValueError: "64,8", "": awk read either as 0)
    if not math.isfinite(x):
        raise ValueError(f"limit {text!r} is not a number")
    return x


def row_record(row, layouts, n_all, limit):
    """`row` over its layout records (in GATE_CPU_LAYOUTS' order, `n_all` of them asked for): (its record,
    the lines gate_cpu.sh prints for it). Its 8, U and F are their means over the layouts that passed, F
    with its range; overruns summed, the code size the first layout's. A fail when F's mean is over `limit`
    (% of a block); a layout over it only adds a warning line. An error when a layout has no figures."""
    lim = _limit(limit)
    got = [r for r in layouts if r["status"] == "pass"]
    n = len(got)
    if n < n_all or n == 0:
        why = f"F at only {n} of {n_all} layouts"
        return G.record("cpu", row, "error", why=why), [f"{row}: {why}"]
    e = u = s = 0.0
    ov = 0
    lo = hi = None
    for r in got:                                # (summed in order, as awk did: the same doubles)
        v = r["value"]
        e += v["8"]
        u += v["U"]
        s += v["F"]
        ov += v["overruns"]
        if lo is None or v["F"] < lo:
            lo = v["F"]
        if hi is None or v["F"] > hi:
            hi, hilay = v["F"], r["name"].rsplit("@", 1)[1]
    m = s / n
    code = got[0]["value"]["code_bytes"]        # (every layout passed: the first is GATE_CPU_LAYOUTS' first)
    value = {"F": m, "F_min": lo, "F_max": hi, "spread": hi - lo, "8": e / n, "U": u / n,
             "overruns": ov, "code_bytes": code, "layouts": n, "limit": lim}
    lines = [f"{row:<10} 8 = {e / n:4.1f} %   U = {u / n:4.1f} %   F = {m:4.1f} % ({lo:.1f}..{hi:.1f}, "
             f"spread {hi - lo:.1f}, {n} layouts)    {ov} overruns code {code} B"]
    status = "pass"
    if m > lim:
        status = "fail"
        lines.append(f"{row}: F mean {m:.1f} % over the limit {lim:.1f} %")
    elif hi > lim:
        lines.append(f"{row}: max {hi:.1f} % over the limit {lim:.1f} % at layout {hilay} (mean ok)")
    return G.record("cpu", row, status, value=value, why=lines[1] if len(lines) > 1 else None), lines
