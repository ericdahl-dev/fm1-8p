#!/usr/bin/env python3
"""scorecard.py - how close fm1-8p's 32 JX-8P factory tones are to PG-8X 2.0.13 (and, from the parameter
match harness, every tone parameter over its range). Needs numpy and scipy.

The PG-8X reference renders live outside this repo (SCORECARD_REF); nothing PG-8X is committed."""
import json
import os

import numpy as np
from scipy import signal

SCHEMA = 1   # history rows: bump when a field changes meaning

SR = 48000

# BS.1770 K-weighting at 48 kHz: a high shelf, then a high pass
_K1 = ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585])
_K2 = ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621])


def mono(x):
    x = np.asarray(x, dtype=np.float64)
    return x.mean(axis=1) if x.ndim == 2 else x


def lufs(x):
    """Loudness, LUFS: BS.1770 K-weighting, ungated, over the whole signal (mono)."""
    y = signal.lfilter(*_K2, signal.lfilter(*_K1, mono(x)))
    return -0.691 + 10 * np.log10(np.mean(y ** 2) + 1e-20)


def loudness_gap(ours, pg):
    """Ours minus PG-8X, in LU."""
    return lufs(ours) - lufs(pg)


_BANDS = 1000 * 2 ** (np.arange(-14, 10) / 3)   # 1/3-octave centers, about 63 Hz .. 6.3 kHz


def band_levels(x):
    """dB per 1/3-octave band (Hann-windowed power spectrum)."""
    x = mono(x)
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    floor = 1e-12 * sp.sum() + 1e-30   # relative, so a level change moves every band alike
    return np.array([10 * np.log10(sp[(f >= c * 2 ** (-1 / 6)) & (f < c * 2 ** (1 / 6))].sum() + floor)
                     for c in _BANDS])


def spectral_distance(ours, pg):
    """RMS of the band level differences after taking their median out (level-matched), dB."""
    d = band_levels(ours) - band_levels(pg)
    d -= np.median(d)
    return float(np.sqrt(np.mean(d ** 2)))


def frames_db(x, frame_s=0.01):
    """RMS level per frame, dB."""
    x = mono(x)
    n = int(frame_s * SR)
    k = len(x) // n
    r = np.sqrt(np.mean(x[:k * n].reshape(k, n) ** 2, axis=1))
    return 20 * np.log10(r + 1e-12)


def fall_ms(x, t0, t1, drop):
    """Milliseconds from the loudest 10 ms frame in [t0, t1) s until a frame `drop` dB below it, within
    the window; None if it never gets there."""
    db = frames_db(x)[int(t0 * 100):int(t1 * 100)]
    p = int(np.argmax(db))
    below = np.nonzero(db[p:] < db[p] - drop)[0]
    return float(below[0] * 10) if len(below) else None


_GRID = 30 * 2 ** (np.arange(0, 1200 * np.log2(8000 / 30), 5) / 1200)   # 5-cent steps, 30 Hz .. 8 kHz


def _logspec(x):
    x = mono(x)
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), 1 / SR)
    v = np.log1p(np.interp(_GRID, f, sp) / (sp.max() + 1e-12) * 1000)
    return v - v.mean()


def pitch_cents(ours, pg, span=200):
    """The shift (cents, 5-cent steps, within +-span) that best lines up ours' spectrum on PG-8X's;
    + means ours is sharp."""
    a, b = _logspec(pg), _logspec(ours)
    best, arg = -np.inf, 0
    for k in range(-span // 5, span // 5 + 1):
        c = np.dot(a[:len(a) - k], b[k:]) if k >= 0 else np.dot(a[-k:], b[:len(b) + k])
        if c > best:
            best, arg = c, k
    return arg * 5.0


# The scorecard MIDI's sections (tools/gate_midi.py score()), seconds
SECTIONS = {"chord": (0.3, 2.9), "v110": (4.0, 4.4), "v60": (4.5, 4.9), "v90": (5.0, 5.3), "v40": (5.4, 6.0),
            "low": (6.7, 8.0), "high": (8.9, 9.9)}
# (window start, end, dB): fall times compared, ms from the window's peak
TIMINGS = {"chord_decay20": (0.0, 3.0, 20), "chord_release40": (2.95, 4.0, 40),
           "low_decay20": (6.4, 8.1, 20), "low_release40": (8.05, 8.6, 40)}


def _cut(x, a, b):
    return mono(x)[int(a * SR):int(b * SR)]


def _gap(a, b):
    return None if a is None or b is None else a - b


def score_tone(ours, pg):
    """One tone, ours against PG-8X's render of the same MIDI: loudness gap (LU), spectral distance per
    section and its mean (dB), fall-time gaps (ms, ours minus PG-8X; None where either never falls that
    far), and the pitch offset of the held low and high notes (cents)."""
    spec = {k: round(spectral_distance(_cut(ours, *w), _cut(pg, *w)), 2) for k, w in SECTIONS.items()}
    return {
        "loudness_gap_lu": round(loudness_gap(ours, pg), 2),
        "spectral_db": spec,
        "spectral_db_mean": round(float(np.mean(list(spec.values()))), 2),
        "timing_gap_ms": {k: _gap(fall_ms(ours, *w), fall_ms(pg, *w)) for k, w in TIMINGS.items()},
        "pitch_cents": {k: pitch_cents(_cut(ours, *SECTIONS[k]), _cut(pg, *SECTIONS[k])) for k in ("low", "high")},
    }


def tone_row(commit, date, tone, name, score, ref):
    """One history row for one tone at one commit (schema SCHEMA)."""
    return {"schema": SCHEMA, "kind": "tone", "commit": commit, "date": date, "tone": tone, "name": name,
            "ref": ref, **score}


def append_rows(path, rows):
    """Appends one JSON line per row to `path` (created if missing); earlier lines are never rewritten."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "a") as fh:
        for r in rows:
            fh.write(json.dumps(r, sort_keys=True) + "\n")


def param_row(commit, date, result):
    """One history row for one tone parameter, from the match harness's results/NAME.json: its status,
    the worst error per metric (where it is, the unit and tolerance) and the factory tones over tolerance."""
    units = {m["key"]: m for m in result.get("metrics", [])}
    worst = {k: {"err": j.get("err"), "at": j.get("at"), "unit": units.get(k, {}).get("unit"),
                 "tol": units.get(k, {}).get("tol")} for k, j in result.get("judge", {}).items()}
    tones = list(result.get("tones_affected", []))
    return {"schema": SCHEMA, "kind": "param", "commit": commit, "date": date, "param": result["name"],
            "group": result.get("group"), "status": result.get("status"), "worst_ratio": result.get("worst_ratio"),
            "worst": worst, "tones_over": len(tones), "tones": tones}


def moved(base, head, eps=0.05):
    """Tones whose spectral mean or loudness gap changed between two runs (lists of tone rows), biggest
    spectral change first. verdict: closer / further (to PG-8X, by the spectral mean; by the loudness gap
    when that alone moved)."""
    b = {r["tone"]: r for r in base}
    out = []
    for h in head:
        r = b.get(h["tone"])
        if r is None:
            continue
        ds = h["spectral_db_mean"] - r["spectral_db_mean"]
        dl = abs(h["loudness_gap_lu"]) - abs(r["loudness_gap_lu"])
        if abs(ds) < eps and abs(dl) < eps:
            continue
        key = ds if abs(ds) >= eps else dl
        out.append({"tone": h["tone"], "name": h["name"], "spectral_base": r["spectral_db_mean"],
                    "spectral_head": h["spectral_db_mean"], "spectral_change": ds,
                    "gap_base": r["loudness_gap_lu"], "gap_head": h["loudness_gap_lu"],
                    "verdict": "closer" if key < 0 else "further"})
    return sorted(out, key=lambda m: -abs(m["spectral_change"]))


def moved_table(rows):
    """Markdown: tones moved, for a PR body."""
    lines = ["| tone | spectral dB (base → head) | loudness gap LU (base → head) | |", "|---|---|---|---|"]
    for m in rows:
        lines.append(f"| {m['tone']:02d} {m['name']} | {m['spectral_base']:.1f} → {m['spectral_head']:.1f} | "
                     f"{m['gap_base']:+.1f} → {m['gap_head']:+.1f} | {m['verdict']} |")
    return "\n".join(lines)


def worst(rows, n=5):
    """The n tones furthest from PG-8X by spectral mean."""
    return sorted(rows, key=lambda r: -r["spectral_db_mean"])[:n]


# ---- the run ----

HERE = os.path.dirname(os.path.abspath(__file__))


def write_midi(work):
    """The scorecard MIDI (tools/gate_midi.py score()) in work/midi; returns its path."""
    import sys
    sys.path.insert(0, HERE)
    import gate_midi
    d = os.path.join(work, "midi")
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, "score.mid")
    with open(path, "wb") as fh:
        fh.write(gate_midi.smf(gate_midi.score()))
    return path


def sha256(path):
    import hashlib
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def write_manifest(ref, mid):
    """A reference folder's manifest: the MIDI it was rendered from (make_ref.py writes the real one)."""
    with open(os.path.join(ref, "manifest.json"), "w") as fh:
        json.dump({"midi_sha256": sha256(mid)}, fh)


def tone_args(tree):
    """[(tone number, name, render args)] for the 32 factory tones, as TREE's converter maps them."""
    import re
    import sys
    sys.path.insert(0, os.path.join(tree, "tools"))
    import jx8p
    with open(os.path.join(tree, "engine", "params.c")) as fh:
        names = dict(re.findall(r'\{\s*FM_PARAM_(\w+)\s*,\s*"([^"]+)"', fh.read()))
    with open(os.path.join(tree, "data", "jx8p-factory-1-32.syx"), "rb") as fh:
        tones = jx8p.parse_syx(fh.read())
    out = []
    for i, t in enumerate(tones, 1):
        e = jx8p.to_engine(t)
        args = []
        for f in jx8p.FIELDS:
            if f.param:
                args += ["-s", f"{names[f.param]}={e[f.name]}"]
        out.append((i, t.name, args))
    return out


def render_tones(tree, mid, work):
    """Renders the 32 tones with TREE's build/fm1-8p-render (48 kHz, 3 s tail, 6 voices); yields
    (tone number, wav path)."""
    import subprocess
    d = os.path.join(work, "ours")
    os.makedirs(d, exist_ok=True)
    for i, _, args in tone_args(tree):
        path = os.path.join(d, f"{i:02d}.wav")
        subprocess.run([os.path.join(tree, "build", "fm1-8p-render"), "-r", str(SR), "-t", "3000", "-v", "6",
                        *args, mid, path], check=True, capture_output=True)
        yield i, path


def read_wav(path):
    from scipy.io import wavfile
    sr, a = wavfile.read(path)
    if sr != SR:
        raise SystemExit(f"scorecard: {path}: {sr} Hz, not {SR}")
    a = a.astype(np.float64) / (32768.0 if a.dtype.kind == "i" else 1.0)
    return a


def now():
    import datetime
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


def run(tree, commit, ref, history, work=None, match=None):
    """Scores TREE's 32 tones against the reference renders in REF; appends a row per tone (and, with
    MATCH, a match harness results folder, a row per parameter) to HISTORY; returns the tone rows."""
    work = work or os.path.join(tree, "build", "scorecard")
    mid = write_midi(work)
    try:
        with open(os.path.join(ref, "manifest.json")) as fh:
            man = json.load(fh)
    except OSError:
        raise SystemExit(f"scorecard: {ref}: no manifest.json (make the reference first)")
    if man.get("midi_sha256") != sha256(mid):
        raise SystemExit(f"scorecard: {ref} was rendered from other MIDI; make the reference again")
    names = {i: n for i, n, _ in tone_args(tree)}
    date, ref_id = now(), man["midi_sha256"][:12]
    rows = []
    for i, path in render_tones(tree, mid, work):
        score = score_tone(read_wav(path), read_wav(os.path.join(ref, f"{i:02d}.wav")))
        rows.append(tone_row(commit, date, i, names[i], score, ref_id))
    params = []
    if match:
        import glob
        for p in sorted(glob.glob(os.path.join(match, "results", "*.json"))):
            with open(p) as fh:
                params.append(param_row(commit, date, json.load(fh)))
    append_rows(history, rows + params)
    return rows


def rows_for(history, commit, ref):
    """The tone rows of COMMIT's latest run against reference REF in HISTORY, or None."""
    try:
        with open(history) as fh:
            rows = [json.loads(line) for line in fh if line.strip()]
    except OSError:
        return None
    rows = [r for r in rows if r.get("kind") == "tone" and r.get("commit") == commit and r.get("ref") == ref]
    if not rows:
        return None
    last = max(r["date"] for r in rows)
    return [r for r in rows if r["date"] == last]


def _fmt(v, f="{:+.0f}"):
    return "-" if v is None else f.format(v)


def report(head, base):
    """Markdown: every tone, the worst 5, and the tones moved against BASE's rows (None: no base)."""
    out = ["| tone | spectral dB (mean) | worst section | loudness gap LU | low decay gap ms | pitch low / high c |",
           "|---|---|---|---|---|---|"]
    for r in sorted(head, key=lambda r: r["tone"]):
        sec = max(r["spectral_db"], key=r["spectral_db"].get)
        out.append(f"| {r['tone']:02d} {r['name']} | {r['spectral_db_mean']:.1f} | {sec} {r['spectral_db'][sec]:.1f} | "
                   f"{r['loudness_gap_lu']:+.1f} | {_fmt(r['timing_gap_ms'].get('low_decay20'))} | "
                   f"{_fmt(r['pitch_cents']['low'])} / {_fmt(r['pitch_cents']['high'])} |")
    out += ["", "worst 5 (spectral dB): " + ", ".join(f"{r['tone']:02d} {r['name']} {r['spectral_db_mean']:.1f}"
                                                      for r in worst(head))]
    if base is None:
        out += ["", "tones moved: no base run to compare with"]
    else:
        m = moved(base, head)
        out += ["", f"tones moved: {len(m)} ({sum(x['verdict'] == 'closer' for x in m)} closer, "
                    f"{sum(x['verdict'] == 'further' for x in m)} further)"]
        if m:
            out += ["", moved_table(m)]
    return "\n".join(out)


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description="fm1-8p's 32 factory tones against PG-8X 2.0.13 renders")
    ap.add_argument("--tree", required=True, help="an fm1-8p source tree with build/fm1-8p-render built")
    ap.add_argument("--commit", required=True, help="the commit the tree is (for the history rows)")
    ap.add_argument("--ref", required=True, help="the PG-8X reference folder (NN.wav + manifest.json)")
    ap.add_argument("--history", required=True, help="history.jsonl to append to")
    ap.add_argument("--match", help="a match harness output folder (results/*.json): adds a row per parameter")
    ap.add_argument("--base", help="a commit already in the history: print the tones moved against it")
    ap.add_argument("--work", help="scratch folder (default TREE/build/scorecard)")
    a = ap.parse_args(argv)
    rows = run(a.tree, a.commit, a.ref, a.history, a.work, a.match)
    base = rows_for(a.history, a.base, rows[0]["ref"]) if a.base else None
    if a.base and base is None:
        print(f"scorecard: no rows for {a.base} against this reference in {a.history}")
    print(report(rows, base))
    print(f"\n{len(rows)} tone rows{' and the parameter rows' if a.match else ''} appended to {a.history}")


if __name__ == "__main__":
    main()
