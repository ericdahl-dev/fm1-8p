#!/usr/bin/env python3
"""scorecard.py - how close fm1-8p's 32 JX-8P factory tones are to PG-8X 2.0.13 (and, from the parameter
match harness, every tone parameter over its range). Needs numpy and scipy.

The PG-8X reference renders live outside this repo (SCORECARD_REF); nothing PG-8X is committed."""
import json
import os

import numpy as np
from scipy import signal

SCHEMA = 3   # history rows: bump when a field changes meaning (2: timings sample-accurate, on the high note;
             # 3: spectral and air floored 60 dB under the loudest band, so int16's floor isn't a difference)
FLOOR_DB = 60.0   # bands (and the air share) this far under the loudest band count as silent

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


def _floored(levels):
    """Band levels with everything more than FLOOR_DB under the loudest band raised to that floor: our
    renders are int16 (as the device's output is) and PG-8X's float, so a band near silence would
    otherwise read the 16-bit floor against PG-8X's nothing. That floor is real, but it isn't timbre."""
    return np.maximum(levels, levels.max() - FLOOR_DB)


def spectral_distance(ours, pg):
    """RMS of the band level differences after taking their median out (level-matched), dB; each side's
    bands floored FLOOR_DB under its loudest."""
    d = _floored(band_levels(ours)) - _floored(band_levels(pg))
    d -= np.median(d)
    return float(np.sqrt(np.mean(d ** 2)))


def envelope_db(x, period_s):
    """Level in dB per sample: the Hilbert magnitude, RMS-smoothed over two periods of the note, so the
    waveform's own ripple cancels (frame RMS shorter than a period reads crossings late)."""
    x = mono(x)
    e = np.abs(signal.hilbert(x))
    n = max(1, int(round(2 * period_s * SR)))
    k = np.ones(n) / n
    return 10 * np.log10(np.convolve(e ** 2, k, mode="same") + 1e-24)


def decay_ms(x, onset, end, f0, drop, skip=0.03):
    """ms the level takes to fall `drop` dB, from `skip` s after the note's onset (past PG-8X's onset burst);
    None if it doesn't get there before `end` s."""
    db = envelope_db(x, 1.0 / f0)
    i0 = int((onset + skip) * SR)
    below = np.nonzero(db[i0:int(end * SR)] < db[i0] - drop)[0]
    return float(below[0] * 1000.0 / SR) if len(below) else None


def release_ms(x, off, end, f0, drop):
    """ms from note-off until the level is `drop` dB below its median over the 20 ms before it; None if it
    doesn't get there before `end` s."""
    db = envelope_db(x, 1.0 / f0)
    i = int(off * SR)
    ref = float(np.median(db[i - int(0.02 * SR):i]))
    below = np.nonzero(db[i:int(end * SR)] < ref - drop)[0]
    return float(below[0] * 1000.0 / SR) if len(below) else None


AIR_HZ = 6300.0


def air_db(x):
    """Energy above AIR_HZ as a share of the whole, dB, floored at -FLOOR_DB (see _floored)."""
    x = mono(x)
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return max(10 * np.log10(sp[f >= AIR_HZ].sum() / (sp.sum() + 1e-30) + 1e-30), -FLOOR_DB)


def air_gap(ours, pg):
    """Ours' air minus PG-8X's, dB: + means more energy up there than PG-8X has."""
    return float(air_db(ours) - air_db(pg))


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


# The scorecard MIDI's sections (tools/gate_midi.py score()), seconds; each starts 30 ms or more past
# its note's onset, clear of PG-8X's onset burst (#146)
SECTIONS = {"chord": (0.3, 2.9), "v110": (4.03, 4.4), "v60": (4.53, 4.9), "v90": (5.03, 5.3), "v40": (5.43, 6.0),
            "low": (6.7, 8.0), "high": (8.9, 9.9)}
HIGH = (8.6, 10.0, 1046.50)   # the high C6: onset, note-off (s), f0 (Hz): about 1 ms a period


def _cut(x, a, b):
    return mono(x)[int(a * SR):int(b * SR)]


def _gap(a, b):
    return None if a is None or b is None else a - b


def score_tone(ours, pg):
    """One tone, ours against PG-8X's render of the same MIDI: loudness gap (LU), spectral distance per
    section and its mean (dB), the high note's decay (to -20 dB, from 30 ms after onset) and release (to
    -40 dB after note-off) gaps (ms, ours minus PG-8X; None where either never falls that far), the
    pitch offset of the held low and high notes (cents; harmonic tones only: an inharmonic one, a ring
    or cross-mod, has no single pitch and reads nonsense), and the chord's air gap (dB)."""
    spec = {k: round(spectral_distance(_cut(ours, *w), _cut(pg, *w)), 2) for k, w in SECTIONS.items()}
    return {
        "loudness_gap_lu": round(loudness_gap(ours, pg), 2),
        "spectral_db": spec,
        "spectral_db_mean": round(float(np.mean(list(spec.values()))), 2),
        "timing_gap_ms": {
            "high_decay20": _gap(decay_ms(ours, HIGH[0], HIGH[1], HIGH[2], 20), decay_ms(pg, HIGH[0], HIGH[1], HIGH[2], 20)),
            "high_release40": _gap(release_ms(ours, HIGH[1], HIGH[1] + 2.5, HIGH[2], 40),
                                   release_ms(pg, HIGH[1], HIGH[1] + 2.5, HIGH[2], 40))},
        "pitch_cents": {k: pitch_cents(_cut(ours, *SECTIONS[k]), _cut(pg, *SECTIONS[k])) for k in ("low", "high")},
        "air_gap_db": round(air_gap(_cut(ours, *SECTIONS["chord"]), _cut(pg, *SECTIONS["chord"])), 2),
    }


def tone_row(commit, date, tone, name, score, ref, bank="factory"):
    """One history row for one tone of BANK at one commit (schema SCHEMA). A row without "bank" (written
    before banks) is the factory bank."""
    return {"schema": SCHEMA, "kind": "tone", "commit": commit, "date": date, "bank": bank, "tone": tone,
            "name": name, "ref": ref, **score}


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


def write_manifest(ref, mid, bank=None):
    """A reference folder's manifest: the MIDI (and bank) it was rendered from (make_ref.py writes the
    real one)."""
    man = {"midi_sha256": sha256(mid)}
    if bank:
        man["bank_sha256"] = sha256(bank)
    with open(os.path.join(ref, "manifest.json"), "w") as fh:
        json.dump(man, fh)


def tone_args(tree, bank=None):
    """[(tone number, name, render args)] for BANK's tones (default: TREE's factory bank), as TREE's
    converter maps them."""
    import re
    import sys
    sys.path.insert(0, os.path.join(tree, "tools"))
    import jx8p
    with open(os.path.join(tree, "engine", "params.c")) as fh:
        names = dict(re.findall(r'\{\s*FM_PARAM_(\w+)\s*,\s*"([^"]+)"', fh.read()))
    with open(bank or os.path.join(tree, "data", "jx8p-factory-1-32.syx"), "rb") as fh:
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


def render_tones(tree, mid, work, bank=None):
    """Renders the 32 tones with TREE's build/fm1-8p-render (48 kHz, 3 s tail, 6 voices); yields
    (tone number, wav path)."""
    import subprocess
    d = os.path.join(work, "ours")
    os.makedirs(d, exist_ok=True)
    for i, _, args in tone_args(tree, bank):
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


def run(tree, commit, ref, history, work=None, match=None, bank=None, bank_name="factory"):
    """Scores BANK's tones (default: the factory bank) on TREE against the reference renders in REF;
    appends a row per tone (and, with MATCH, a match harness results folder, a row per parameter) to
    HISTORY; returns the tone rows. REF must have been rendered from the same MIDI and bank."""
    work = work or os.path.join(tree, "build", "scorecard")
    mid = write_midi(work)
    try:
        with open(os.path.join(ref, "manifest.json")) as fh:
            man = json.load(fh)
    except OSError:
        raise SystemExit(f"scorecard: {ref}: no manifest.json (make the reference first)")
    if man.get("midi_sha256") != sha256(mid):
        raise SystemExit(f"scorecard: {ref} was rendered from other MIDI; make the reference again")
    if bank and man.get("bank_sha256") != sha256(bank):
        raise SystemExit(f"scorecard: {ref} was not rendered from {bank}")
    names = {i: n for i, n, _ in tone_args(tree, bank)}
    date, ref_id = now(), man["midi_sha256"][:12]
    rows = []
    for i, path in render_tones(tree, mid, work, bank):
        score = score_tone(read_wav(path), read_wav(os.path.join(ref, f"{i:02d}.wav")))
        rows.append(tone_row(commit, date, i, names[i], score, ref_id, bank_name))
    params = []
    if match:
        import glob
        for p in sorted(glob.glob(os.path.join(match, "results", "*.json"))):
            with open(p) as fh:
                params.append(param_row(commit, date, json.load(fh)))
    append_rows(history, rows + params)
    return rows


def rows_for(history, commit, ref, bank="factory"):
    """The tone rows of COMMIT's latest run of BANK against reference REF in HISTORY, or None."""
    try:
        with open(history) as fh:
            rows = [json.loads(line) for line in fh if line.strip()]
    except OSError:
        return None
    rows = [r for r in rows if r.get("kind") == "tone" and r.get("commit") == commit and r.get("ref") == ref
            and r.get("bank", "factory") == bank]
    if not rows:
        return None
    last = max(r["date"] for r in rows)
    return [r for r in rows if r["date"] == last]


def _fmt(v, f="{:+.0f}"):
    return "-" if v is None else f.format(v)


def report(head, base):
    """Markdown: every tone, the worst 5, and the tones moved against BASE's rows (None: no base)."""
    out = ["| tone | spectral dB (mean) | worst section | loudness gap LU | air gap dB | high decay / release gap ms | "
           "pitch low / high c |", "|---|---|---|---|---|---|---|"]
    for r in sorted(head, key=lambda r: r["tone"]):
        sec = max(r["spectral_db"], key=r["spectral_db"].get)
        out.append(f"| {r['tone']:02d} {r['name']} | {r['spectral_db_mean']:.1f} | {sec} {r['spectral_db'][sec]:.1f} | "
                   f"{r['loudness_gap_lu']:+.1f} | {_fmt(r.get('air_gap_db'), '{:+.1f}')} | "
                   f"{_fmt(r['timing_gap_ms'].get('high_decay20'))} / "
                   f"{_fmt(r['timing_gap_ms'].get('high_release40'))} | "
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


def base_note(root, commit, base):
    """None when COMMIT holds every commit of BASE; else a note naming their merge base, the fair base
    (a head behind main moves tones by what it lacks, not by what it changes)."""
    import subprocess
    git = lambda *a: subprocess.run(["git", "-C", root, *a], capture_output=True, text=True, check=True).stdout.strip()
    missing = int(git("rev-list", "--count", f"{commit}..{base}"))
    if missing == 0:
        return None
    mb = git("rev-parse", "--short=7", git("merge-base", commit, base))
    return (f"note: {commit} lacks {missing} commits of {base}; tones it moves may be what it lacks. "
            f"Score its merge base {mb} and compare against that.")


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
    ap.add_argument("--bank", help="a .syx bank of 32 tones to score instead of the factory bank (kept out of "
                                   "this repo when it isn't ours)")
    ap.add_argument("--bank-name", default="factory", help="the bank's name in the history rows")
    ap.add_argument("--root", help="the git repo, to check BASE is an ancestor of COMMIT")
    a = ap.parse_args(argv)
    if a.bank and a.bank_name == "factory":
        ap.error("--bank needs a --bank-name")
    rows = run(a.tree, a.commit, a.ref, a.history, a.work, a.match, a.bank, a.bank_name)
    base = rows_for(a.history, a.base, rows[0]["ref"], a.bank_name) if a.base else None
    note = base_note(a.root, a.commit, a.base) if a.base and a.root else None
    if note:
        print(note)
    print(f"bank: {a.bank_name}")
    if a.base and base is None:
        print(f"scorecard: no rows for {a.base} against this reference in {a.history}")
    print(report(rows, base))
    print(f"\n{len(rows)} tone rows{' and the parameter rows' if a.match else ''} appended to {a.history}")


if __name__ == "__main__":
    main()
