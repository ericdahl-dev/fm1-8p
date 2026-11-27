#!/usr/bin/env python3
"""scorecard.py - how close fm1-8p's 32 JX-8P factory tones are to PG-8X 2.0.13 (and, from the parameter
match harness, every tone parameter over its range). Needs numpy and scipy.

The PG-8X reference renders live outside this repo (SCORECARD_REF); nothing PG-8X is committed.

Quiet sections (s10): our renders are int16, so each spectral section scores PG-8X's side at our level in
int16 too, and our 16-bit floor isn't read as timbre. The other side of that: in a section under about
-55 dBFS, a difference counts only down to that floor. A band has to stand about 10 dB over it to count
near fully, so a low score there means a match above the floor, not in the detail under it. PG-8X's
float tail under one step, where our idle engine reads exact 0, floors to 0 or -1 on its side. That
reads 1.5-6 dB in bands 55-60 dB down, about 0.1 dB on the section (AGOGO BELL's chord: 1.47 s9, 1.58 s10).

Beat phase (s12, #382): two detuned DCOs beat slower than a section is long, so one render's score reads
where in that beat the note started, not the tone (one build with only DCO-2's start moved swung POLY
BRASS v40 1.62 .. 6.73 dB). Each tone is rendered at 8 DCO-2 start offsets an eighth of a cycle apart
(fm1-8p-render -o) and every figure is read from their power averaged: the band levels, the air share,
the loudness, the envelope the timings read and the spectrum the pitch reads. That is the tone's expected
spectrum under a random start: the beat's cross term cancels for its first 7 harmonics, so the score no
longer depends on the start (scored from offsets 0, 2/16 .. 14/16 against 1/16 .. 15/16, the factory
bank's sections agree within 0.21 dB). A reference can be several draws in the same way (manifest
shifts_ticks: the scorecard MIDI that many ticks late, 960 a second, its files NN.wav, NN-1.wav ..); PG-8X's
DCOs run free, so a later start meets them at another beat phase. With one draw the PG-8X side is that one
draw, beat and all; with one draw on both sides every figure is s11's.

Clock phase (s13, #392): what runs on the clock from the render's start (the chorus's LFO, the LFO, on both
sides) also meets a section at a phase set by when the MIDI starts. Against a reference of several draws,
ours render at the same leads, 4 DCO-2 offsets at each (scorecard_cmd.draw_plan): averaged over 16 leads
PG-8X's chorus was at 16 phases and ours at one, so the chorus tones scored 0.56 dB further on v7 than v6
while the others moved -0.05. One draw (v6) reads as s12."""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scorecard_cmd  # noqa: E402

import numpy as np
from scipy import signal

SCHEMA = 14  # history rows: bump when a field changes meaning (2: timings sample-accurate, on the high note;
             # 3: spectral and air floored 60 dB under the loudest band, so int16's floor isn't a difference;
             # 4: the MIDI starts 0.25 s in, past PG-8X's start-up burst and glide, #163;
             # 5: the phrase sections start 130 ms past their note on, past PG-8X's onset thump, #146;
             # 6: the reference has PG-8X's SELFNOISE at 0, v5, #247;
             # 7: both 5 and 6, merged;
             # 8: no pitch for a ring or cross-mod tone without sync (it has none), #253;
             # 9: the air gap takes PG-8X's side at our level in int16, so our 16-bit floor isn't air, #299;
             # 10: so does each spectral section, so the floor isn't timbre either;
             # 11: the phrase notes are 0.7 s, so every section reads 0.5 s or more; the reference is v6;
             # 12: every figure over draws, power averaged: ours at 8 DCO-2 start offsets, PG-8X's at its
             #     reference's shifts, so a slow beat's start phase isn't scored, #382;
             # 13: ours render each of the reference's K leads with 4 DCO-2 offsets (4K draws, draw_plan), so
             #     what runs on the clock (the chorus's LFO, the LFO) meets PG-8X's phases; v6 reads as s12, #392;
             # 14: a band at 31.5 Hz, so the low section's fundamental, C1, is read, not only its skirt, #326)
FLOOR_DB = 60.0   # bands (and the air share) this far under the loudest band count as silent

SR = scorecard_cmd.SR

# BS.1770 K-weighting at 48 kHz: a high shelf, then a high pass
_K1 = ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585])
_K2 = ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621])


def mono(x):
    x = np.asarray(x, dtype=np.float64)
    return x.mean(axis=1) if x.ndim == 2 else x


def draws(x):
    """A render, or several of one tone (s12, #382), as [(samples, shift s)]: one array is one draw at
    shift 0; a list holds arrays (shift 0) or (array, shift) pairs, a draw whose MIDI starts `shift` s
    late. Every figure is read from the draws' power averaged over them, so a slow beat's cross term
    cancels when the draws spread its start phase."""
    if isinstance(x, np.ndarray):
        return [(x, 0.0)]
    return [(d, 0.0) if isinstance(d, np.ndarray) else (np.asarray(d[0]), float(d[1])) for d in x]


def aligned(x):
    """Each draw from its own shift on, cut to the shortest: [samples], time 0 at the MIDI's start."""
    xs = [np.asarray(d)[int(round(sh * SR)):] for d, sh in draws(x)]
    n = min(len(d) for d in xs)
    return [d[:n] for d in xs]


def cuts(x, a, b):
    """Each draw's samples from a to b s past its MIDI's start, all as long as one cut at shift 0."""
    n = int(b * SR) - int(a * SR)
    return [np.asarray(d)[int(a * SR) + int(round(sh * SR)):][:n] for d, sh in draws(x)]


def lufs(x):
    """Loudness, LUFS: BS.1770 K-weighting, ungated, over the whole signal (mono); over several draws,
    of their mean power."""
    ms = [np.mean(signal.lfilter(*_K2, signal.lfilter(*_K1, mono(d))) ** 2) for d in aligned(x)]
    return -0.691 + 10 * np.log10(np.mean(ms) + 1e-20)


def loudness_gap(ours, pg):
    """Ours minus PG-8X, in LU."""
    return lufs(ours) - lufs(pg)


_BANDS = 1000 * 2 ** (np.arange(-15, 10) / 3)   # 1/3-octave centers, 31.5 Hz .. 8 kHz: C1, the low
                                                # section's fundamental (32.7 Hz), has its own (s14, #326)


def power_spectrum(x):
    """The Hann-windowed power spectrum (mono) and its frequencies; over several draws (cuts of one
    length), their mean."""
    xs = [mono(d) for d, _ in draws(x)]
    sp = np.mean([np.abs(np.fft.rfft(d * np.hanning(len(d)))) ** 2 for d in xs], axis=0)
    return sp, np.fft.rfftfreq(len(xs[0]), 1 / SR)


def band_levels(x):
    """dB per 1/3-octave band (Hann-windowed power spectrum; over several draws, their mean power)."""
    sp, f = power_spectrum(x)
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
    waveform's own ripple cancels (frame RMS shorter than a period reads crossings late); over several
    draws, their mean power, each from its MIDI's start."""
    n = max(1, int(round(2 * period_s * SR)))
    k = np.ones(n) / n
    p = np.mean([np.convolve(np.abs(signal.hilbert(mono(d))) ** 2, k, mode="same") for d in aligned(x)], axis=0)
    return 10 * np.log10(p + 1e-24)


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
    """Energy above AIR_HZ as a share of the whole, dB, floored at -FLOOR_DB (see _floored); over several
    draws, of their mean power."""
    sp, f = power_spectrum(x)
    return max(10 * np.log10(sp[f >= AIR_HZ].sum() / (sp.sum() + 1e-30) + 1e-30), -FLOOR_DB)


def as_ours_int16_draws(pg, ours, clip=True):
    """as_ours_int16 for PG's draws, all at one gain: their mean power to OURS' draws' mean power."""
    po = np.mean([np.mean(mono(d) ** 2) for d, _ in draws(ours)])
    pp = np.mean([np.mean(mono(d) ** 2) for d, _ in draws(pg)])
    return [(_on_int16(d, np.sqrt(po / (pp + 1e-30)), clip), sh) for d, sh in draws(pg)]


def _on_int16(pg, g, clip):
    """PG at gain G on 16 bits as output.c's to_int16 puts it, scaled back (as_ours_int16)."""
    pg = np.asarray(pg, dtype=np.float64)
    if g <= 0:
        return pg
    q = np.floor(pg * g * 32768)
    return (np.clip(q, -32768, 32767) if clip else q) / 32768 / g


def as_ours_int16(pg, ours, clip=True):
    """PG-8X's float render as ours would carry it: scaled to our RMS, put on 16 bits per channel as
    output.c's to_int16 does (floored, clipped to -32768..32767; read back over 32768), and scaled back. Our renders are int16, as the device's output is, so a quiet section has the 16-bit floor's
    hiss in it; scored against this, the floor is on both sides (#299). With clip False it is floored but
    not clipped: at our RMS PG-8X's peaks can pass full scale where ours don't, and clipped they'd gain
    harmonics neither render has (the spectral sections, s10)."""
    g = np.sqrt(np.mean(mono(ours) ** 2) / (np.mean(mono(pg) ** 2) + 1e-30))
    return _on_int16(pg, g, clip)


def air_gap(ours, pg):
    """Ours' air minus PG-8X's, dB: + means more energy up there than PG-8X has. PG-8X's side is taken as
    it would be at our level in int16 (as_ours_int16), so the 16-bit floor isn't read as air (#299)."""
    return float(air_db(ours) - air_db(as_ours_int16_draws(pg, ours)))


_GRID = 30 * 2 ** (np.arange(0, 1200 * np.log2(8000 / 30), 5) / 1200)   # 5-cent steps, 30 Hz .. 8 kHz


def _logspec(x):
    sp, f = power_spectrum(x)
    sp = np.sqrt(sp)
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


# The scorecard MIDI's sections (tools/gate_midi.py score()), seconds; each starts 130 ms or more past
# its note on. PG-8X puts a sub-230 Hz thump near -32 dBFS on every note on, a voice's first or not,
# that dies down over about 120 ms (#146); our engine has none, so a section inside it reads a gap in
# the lows that isn't timbre
SECTIONS = {"chord": (0.55, 3.15), "v110": (4.38, 4.95), "v60": (5.18, 5.75), "v90": (5.98, 6.55),
            "v40": (6.78, 7.35), "low": (8.05, 9.35), "high": (10.25, 11.25)}
HIGH = (9.95, 11.35, 1046.50)   # the high C6: onset, note-off (s), f0 (Hz): about 1 ms a period


def _cut(x, a, b):
    """The draws' cuts from a to b s, mono, as draws at shift 0."""
    return [mono(c) for c in cuts(x, a, b)]


def _gap(a, b):
    return None if a is None or b is None else a - b


def score_tone(ours, pg, harmonic=True):
    """One tone, ours against PG-8X's render of the same MIDI: loudness gap (LU), spectral distance per
    section and its mean (dB), the high note's decay (to -20 dB, from 30 ms after onset) and release (to
    -40 dB after note-off) gaps (ms, ours minus PG-8X; None where either never falls that far), the
    pitch offset of the held low and high notes (cents; None unless `harmonic`: a ring or cross-mod tone
    has no single pitch and reads nonsense, s8), and the chord's air gap (dB). The spectral sections and
    the air gap take PG-8X's side as it would be at our level in int16 (as_ours_int16), so a quiet
    section's 16-bit floor isn't read as timbre (s10) or air (s9)."""
    spec = {}
    for k, (a, b) in SECTIONS.items():   # PG-8X's side at our level in int16, per channel (s10)
        o, p = cuts(ours, a, b), cuts(pg, a, b)
        spec[k] = round(spectral_distance(o, as_ours_int16_draws(p, o, clip=False)), 2)
    return {
        "loudness_gap_lu": round(loudness_gap(ours, pg), 2),
        "spectral_db": spec,
        "spectral_db_mean": round(float(np.mean(list(spec.values()))), 2),
        "timing_gap_ms": {
            "high_decay20": _gap(decay_ms(ours, HIGH[0], HIGH[1], HIGH[2], 20), decay_ms(pg, HIGH[0], HIGH[1], HIGH[2], 20)),
            "high_release40": _gap(release_ms(ours, HIGH[1], HIGH[1] + 2.5, HIGH[2], 40),
                                   release_ms(pg, HIGH[1], HIGH[1] + 2.5, HIGH[2], 40))},
        "pitch_cents": {k: pitch_cents(_cut(ours, *SECTIONS[k]), _cut(pg, *SECTIONS[k])) if harmonic else None
                        for k in ("low", "high")},
        "air_gap_db": round(air_gap(_cut(ours, *SECTIONS["chord"]), _cut(pg, *SECTIONS["chord"])), 2),
    }


NOISE_SHIFT_S = 0.01


def ruler_noise(pg):
    """Each section's spectral distance between PG's render and itself NOISE_SHIFT_S later (dB): what the
    ruler reads from a change that small, which isn't timbre (s11); over PG's draws, power averaged."""
    later = [(d, sh + NOISE_SHIFT_S) for d, sh in draws(pg)]
    return {k: round(spectral_distance(cuts(pg, a, b), cuts(later, a, b)), 2) for k, (a, b) in SECTIONS.items()}


def noise_line(noises):
    """The report's line: each section's ruler noise, its mean over the tones' NOISES."""
    keys = [k for k in SECTIONS if all(k in n for n in noises)]
    return ("ruler noise (PG-8X against itself 10 ms later, dB): " +
            ", ".join(f"{k} {sum(n[k] for n in noises) / len(noises):.2f}" for k in keys))


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
    when that alone moved); new / gone for a tone on the head only / the base only (#335: never dropped),
    after the others, by tone, with None for the side it lacks. ValueError for a figure that is not a
    finite number."""
    import math
    base, head = list(base), list(head)          # (read twice: a generator would be empty the second time)
    for r in base + head:            # (a NaN would read as a verdict from the other figure)
        for f in ("spectral_db_mean", "loudness_gap_lu"):
            if isinstance(r[f], bool) or not isinstance(r[f], (int, float)) or not math.isfinite(r[f]):
                raise ValueError(f"tone {r['tone']:02d} {r['name']}: {f} is {r[f]!r}, not a number")
    b = {r["tone"]: r for r in base}
    hs = {h["tone"] for h in head}
    out, sides = [], []
    for h in head:
        r = b.get(h["tone"])
        if r is None:
            sides.append({"tone": h["tone"], "name": h["name"], "spectral_base": None,
                          "spectral_head": h["spectral_db_mean"], "spectral_change": None,
                          "gap_base": None, "gap_head": h["loudness_gap_lu"], "verdict": "new"})
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
    for r in base:
        if r["tone"] not in hs:
            sides.append({"tone": r["tone"], "name": r["name"], "spectral_base": r["spectral_db_mean"],
                          "spectral_head": None, "spectral_change": None,
                          "gap_base": r["loudness_gap_lu"], "gap_head": None, "verdict": "gone"})
    return sorted(out, key=lambda m: -abs(m["spectral_change"])) + sorted(sides, key=lambda m: m["tone"])


def moved_table(rows):
    """Markdown: tones moved, for a PR body."""
    lines = ["| tone | spectral dB (base → head) | loudness gap LU (base → head) | |", "|---|---|---|---|"]
    for m in rows:
        lines.append(f"| {m['tone']:02d} {m['name']} | {_fmt(m['spectral_base'], '{:.1f}')} → "
                     f"{_fmt(m['spectral_head'], '{:.1f}')} | "
                     f"{_fmt(m['gap_base'], '{:+.1f}')} → {_fmt(m['gap_head'], '{:+.1f}')} | {m['verdict']} |")
    return "\n".join(lines)


def worst(rows, n=5):
    """The n tones furthest from PG-8X by spectral mean."""
    return sorted(rows, key=lambda r: -r["spectral_db_mean"])[:n]


# ---- the run ----

HERE = os.path.dirname(os.path.abspath(__file__))


def write_midi(work, lead=0):
    """The scorecard MIDI (tools/gate_midi.py score()) in work/midi, every event LEAD ticks (960 a second)
    later when given (a PG-8X draw's, s12); returns its path."""
    import sys
    sys.path.insert(0, HERE)
    import gate_midi
    d = os.path.join(work, "midi")
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, f"score+{lead}.mid" if lead else "score.mid")
    with open(path, "wb") as fh:
        fh.write(gate_midi.smf(gate_midi.score(), lead=lead))
    return path


def lead_seconds(lead):
    """A lead of LEAD ticks, in seconds."""
    import gate_midi
    return lead / gate_midi.TPS


def draw_file(tone, j):
    """Draw J's file for TONE in a reference folder (or ours): NN.wav for the first, NN-J.wav after it."""
    return f"{tone:02d}.wav" if j == 0 else f"{tone:02d}-{j}.wav"


def sha256(path):
    import hashlib
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def write_manifest(ref, mid, bank=None, shifts_ticks=None):
    """A reference folder's manifest: the MIDI (and bank) it was rendered from, PG-8X's SELFNOISE at 0
    (make_ref.py writes the real one); with SHIFTS_TICKS, a draw per lead (s12: draw_file names them),
    each with its MIDI's sha256 (write_midi with that lead)."""
    man = {"midi_sha256": sha256(mid), "selfnoise": 0}
    if shifts_ticks is not None:
        work = os.path.dirname(os.path.dirname(mid))
        man["shifts_ticks"] = list(shifts_ticks)
        man["midi_sha256s"] = [sha256(write_midi(work, t)) for t in shifts_ticks]
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
    names = getattr(jx8p, "PARAM_NAMES", None)   # the param list (#196)
    if names is None:                       # a tree older than the list: its params.c's table
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


WHOLE_RATIO_CENTS = 25.0


def off_whole_ratio_cents(cents):
    """How far an interval of `cents` (osc2 over osc1) is from the nearest whole-number ratio, either way
    up, in cents: the interval less the nearest whole ratio's, both in cents, so a whole number of cents
    off unison or an octave stays exact (25 c is at WHOLE_RATIO_CENTS, not past it)."""
    c = abs(cents)
    k = max(1, round(2 ** (c / 1200)))
    return abs(c - 1200 * np.log2(k))


def inharmonic_tones(tree, bank=None):
    """The tone numbers of BANK (default the factory bank) with ring or cross-mod on (XMOD_AM), no sync, and
    oscillators more than WHOLE_RATIO_CENTS from a whole-number ratio: they have no single pitch, so
    score_tone reports none for them (s8). With sync osc2 restarts every osc1 cycle, so even the ring's
    product repeats at osc1's period; near a whole ratio (a detuned unison) the ring sounds at its
    pitch, beating. Both keep a pitch."""
    import sys
    sys.path.insert(0, os.path.join(tree, "tools"))
    import jx8p
    with open(bank or os.path.join(tree, "data", "jx8p-factory-1-32.syx"), "rb") as fh:
        tones = jx8p.parse_syx(fh.read())
    def off_whole(e):
        return off_whole_ratio_cents(
            1200 * (e["OSC2_OCTAVE"] - e["OSC1_OCTAVE"]) + 100 * (e["OSC2_COARSE_SEMIS"] - e["OSC1_COARSE_SEMIS"])
            + e.get("OSC2_FINE_CENTS", 0) - e.get("OSC1_FINE_CENTS", 0))
    engine = [jx8p.to_engine(t) for t in tones]
    return {i for i, e in enumerate(engine, 1)
            if e.get("XMOD_AM") and not e.get("OSC_SYNC") and off_whole(e) > WHOLE_RATIO_CENTS}


def render_tones(tree, work, bank=None, plan=None):
    """Renders the 32 tones with TREE's build/fm1-8p-render (48 kHz, 3 s tail, 6 voices), a draw for each
    (lead ticks, DCO-2 offset) of PLAN (scorecard_cmd.draw_plan; default a one-draw reference's, s12's 8
    offsets at lead 0): the scorecard MIDI that late, DCO-2 that far on; yields (tone number, [wav path per
    draw])."""
    import subprocess
    from concurrent.futures import ThreadPoolExecutor
    plan = plan or scorecard_cmd.draw_plan([0])
    d = os.path.join(work, "ours")
    os.makedirs(d, exist_ok=True)
    mids = {t: write_midi(work, t) for t in {t for t, _ in plan}}

    def one(job):   # (a tone's draws render side by side: 64 a tone against a 16-draw reference, s13)
        i, args, j, t, o = job
        path = os.path.join(d, draw_file(i, j))
        r = subprocess.run(scorecard_cmd.render_command(tree, args, mids[t], path, o), capture_output=True, text=True)
        if r.returncode == 2 and "usage: fm1-8p-render" in r.stderr and "-o MILLI" not in r.stderr:
            raise SystemExit(f"scorecard: {tree}'s renderer has no -o (DCO-2 start offset, #382): s12 scores a "
                             "tree that has it; merge main into it first")
        if r.returncode != 0:
            raise SystemExit(f"scorecard: tone {i:02d} at offset {o} did not render: {r.stderr.strip()}")
        return path
    with ThreadPoolExecutor(max(1, min(8, os.cpu_count() or 1))) as ex:
        for i, _, args in tone_args(tree, bank):
            yield i, list(ex.map(one, [(i, args, j, t, o) for j, (t, o) in enumerate(plan)]))


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


def _code_id():
    """The scorecard's own code, hashed: a cached score holds only while the code that made it is the same."""
    import hashlib
    h = hashlib.sha256()
    for f in ("scorecard.py", "scorecard_cmd.py"):
        with open(os.path.join(HERE, f), "rb") as fh:
            h.update(fh.read())
    return h.hexdigest()


def tone_key(ours, pg, manifest, harmonic, plan=None):
    """The cache key of one tone's score: a sha256 of the scorecard's code and schema, the reference's
    manifest, whether the tone has a pitch, the draw plan and every byte of our draws' wavs (OURS) and the
    reference's (PG), in order. Same key, same score."""
    import hashlib
    h = hashlib.sha256()
    h.update(json.dumps({"code": _code_id(), "schema": SCHEMA, "manifest": manifest, "harmonic": bool(harmonic),
                         "plan": plan}, sort_keys=True).encode())
    for side in (ours, pg):
        h.update(b"|side|")
        for path in side:
            with open(path, "rb") as fh:
                data = fh.read()
            h.update(len(data).to_bytes(8, "little"))
            h.update(data)
    return h.hexdigest()


def run(tree, commit, ref, history, work=None, match=None, bank=None, bank_name="factory", cache=None):
    """Scores BANK's tones (default: the factory bank) on TREE against the reference renders in REF;
    appends a row per tone (and, with MATCH, a match harness results folder, a row per parameter) to
    HISTORY; returns the tone rows. REF must have been rendered from the same MIDI and bank. With CACHE (a
    folder), each tone's score is kept there under tone_key and read back while its renders, the reference
    and the code are the same, so a commit whose sound didn't change scores in the time of its renders."""
    work = work or os.path.join(tree, "build", "scorecard")
    mid = write_midi(work)
    try:
        with open(os.path.join(ref, "manifest.json")) as fh:
            man = json.load(fh)
    except OSError:
        raise SystemExit(f"scorecard: {ref}: no manifest.json (make the reference first)")
    if man.get("midi_sha256") != sha256(mid):
        raise SystemExit(f"scorecard: {ref} was rendered from other MIDI; make the reference again")
    if man.get("selfnoise") != 0:
        # PG-8X 2.0.13's SELFNOISE (default 0.5) puts an analog noise floor under every sounding voice, read
        # as a 27 dB gap in the lows that isn't timbre; fm1-8p doesn't model it (#247)
        raise SystemExit(f"scorecard: {ref} was rendered with PG-8X's SELFNOISE on (v4 and older); use v5, or "
                         "make the reference again with make_ref.py (SELFNOISE 0)")
    if bank and man.get("bank_sha256") != sha256(bank):
        raise SystemExit(f"scorecard: {ref} was not rendered from {bank}")
    shifts = man.get("shifts_ticks", [0])
    if "shifts_ticks" in man:
        shas = man.get("midi_sha256s", [])
        for j, t in enumerate(shifts):
            if isinstance(t, bool) or not isinstance(t, int) or t < 0:
                raise SystemExit(f"scorecard: {ref}: draw {j}'s shift {t!r} is not a whole number of ticks, 0 or more")
            if j >= len(shas) or shas[j] != sha256(write_midi(work, t)):
                raise SystemExit(f"scorecard: {ref}: draw {j} (a lead of {t} ticks) was rendered from other MIDI")
    names = {i: n for i, n, _ in tone_args(tree, bank)}
    missing = [draw_file(i, j) for i in names for j in range(len(shifts))
               if not os.path.isfile(os.path.join(ref, draw_file(i, j)))]
    if missing:
        raise SystemExit(f"scorecard: {ref}: no {', '.join(missing[:4])}{' ..' if len(missing) > 4 else ''}")
    plan = scorecard_cmd.draw_plan(shifts)
    draw_info = {"ours_leads_ticks": [t for t, _ in plan], "ours_offsets_milli": [o for _, o in plan],
                 "pg_shifts_ticks": list(shifts)}
    odd = inharmonic_tones(tree, bank)
    date, ref_id = now(), man["midi_sha256"][:12]
    rows = []
    for i, paths in render_tones(tree, work, bank, plan):
        pg_paths = [os.path.join(ref, draw_file(i, j)) for j in range(len(shifts))]
        got = None
        if cache:
            key = tone_key(paths, pg_paths, man, i not in odd, plan)
            hit = os.path.join(cache, key[:2], key + ".json")
            if os.path.isfile(hit):
                with open(hit) as fh:
                    got = json.load(fh)
        if got is None:
            pg = [(read_wav(p), lead_seconds(t)) for p, t in zip(pg_paths, shifts)]
            ours = [(read_wav(p), lead_seconds(t)) for p, (t, _) in zip(paths, plan)]
            got = {"score": score_tone(ours, pg, harmonic=i not in odd), "ruler_noise_db": ruler_noise(pg)}
            got = json.loads(json.dumps(got))   # (as the cache keeps it: a read-back row is the same row)
            if cache:
                os.makedirs(os.path.dirname(hit), exist_ok=True)
                with open(hit + ".tmp", "w") as fh:
                    json.dump(got, fh)
                os.replace(hit + ".tmp", hit)
        rows.append({**tone_row(commit, date, i, names[i], got["score"], ref_id, bank_name),
                     "ruler_noise_db": got["ruler_noise_db"], "draws": draw_info})
    params = []
    if match:
        import glob
        for p in sorted(glob.glob(os.path.join(match, "results", "*.json"))):
            with open(p) as fh:
                params.append(param_row(commit, date, json.load(fh)))
    append_rows(history, rows + params)
    return rows


def missing_base(history, commit, ref, bank="factory"):
    """Why rows_for found no rows for COMMIT: rows on another schema only (score it again), or none."""
    try:
        with open(history) as fh:
            rows = [json.loads(line) for line in fh if line.strip()]
    except OSError:
        rows = []
    other = sorted({r.get("schema") for r in rows if r.get("kind") == "tone" and r.get("commit") == commit
                    and r.get("ref") == ref and r.get("bank", "factory") == bank})
    if other:
        return (f"scorecard: {commit} has rows on schema {', '.join(map(str, other))} only, not {SCHEMA}: "
                f"score it again (make scorecard COMMIT={commit}) to compare")
    return f"scorecard: no rows for {commit} against this reference in {history}"


def rows_for(history, commit, ref, bank="factory"):
    """The tone rows of COMMIT's latest run of BANK against reference REF in HISTORY on this SCHEMA, or
    None: a run on another ruler is not comparable, so it doesn't count."""
    try:
        with open(history) as fh:
            rows = [json.loads(line) for line in fh if line.strip()]
    except OSError:
        return None
    rows = [r for r in rows if r.get("kind") == "tone" and r.get("commit") == commit and r.get("ref") == ref
            and r.get("bank", "factory") == bank and r.get("schema") == SCHEMA]
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
        n = {v: sum(x["verdict"] == v for x in m) for v in ("closer", "further", "new", "gone")}
        sides = "".join(f", {n[v]} {v}" for v in ("new", "gone") if n[v])   # (only when there are any)
        out += ["", f"tones moved: {len(m)} ({n['closer']} closer, {n['further']} further{sides})"]
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
    ap.add_argument("--cache", help="a folder that keeps each tone's score by tone_key, read back while the "
                                    "renders, the reference and the code are the same")
    a = ap.parse_args(argv)
    if a.bank and a.bank_name == "factory":
        ap.error("--bank needs a --bank-name")
    rows = run(a.tree, a.commit, a.ref, a.history, a.work, a.match, a.bank, a.bank_name, a.cache)
    asked = a.base is not None                   # (--base "" is a base asked for, and not found: exit 2)
    base = rows_for(a.history, a.base, rows[0]["ref"], a.bank_name) if asked else None
    note = base_note(a.root, a.commit, a.base) if a.base and a.root else None
    if note:
        print(note)
    print(f"bank: {a.bank_name}")
    if asked and base is None:
        print(missing_base(a.history, a.base, rows[0]["ref"], a.bank_name))
    print(report(rows, base))
    if rows and all("ruler_noise_db" in r for r in rows):
        print("\n" + noise_line([r["ruler_noise_db"] for r in rows]))
    print(f"\n{len(rows)} tone rows{' and the parameter rows' if a.match else ''} appended to {a.history}")
    if asked and base is None:
        sys.exit(2)                              # (#335: asked to compare, and nothing was; the head's rows kept)


if __name__ == "__main__":
    main()
