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
