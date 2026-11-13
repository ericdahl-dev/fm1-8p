#!/usr/bin/env python3
"""JX-8P tone data to fm1-8p patches (docs/patches.md).

Reads a SysEx dump of JX-8P tones (APR messages, F0 41 35 0n 21 20 01 <59 bytes> F7) and converts
each tone to fm1-8p engine values. The dump is data only: nothing in it is run.
"""
import ast
import glob
import hashlib
import math
import os
import re
import sys
from collections import namedtuple

Tone = namedtuple("Tone", "name params")   # params: the 59 data bytes, PG-800 / SysEx numbering

APR_HEADER = bytes([0xF0, 0x41, 0x35])
APR_LEN = 67                                # header 7, data 59, F7
NAME_LEN = 10


def parse_syx(data, source="the dump"):
    """Every APR message in data, in order, as Tones. Other messages (the 0x34 program writes
    that follow each tone in a bank dump) are skipped. source names data in errors."""
    tones = []
    i = 0
    while True:
        start = data.find(0xF0, i)
        if start < 0:
            return tones
        end = data.find(0xF7, start)
        if end < 0:
            raise ValueError("%s: the SysEx message at offset %d has no F7" % (source, start))
        msg = data[start:end + 1]
        if msg[:3] == APR_HEADER and len(msg) == APR_LEN:
            params = msg[7:-1]
            tones.append(Tone(params[:NAME_LEN].decode("ascii").strip(), bytes(params)))
        i = end + 1


# PG-800 / SysEx parameter numbers (byte offsets in an APR's data), from F. Vecoven's JX8P manual,
# "SYSEX IPR and CC parameters" (docs/patches.md).
DCO1_RANGE, DCO1_WAVE, DCO1_TUNE = 11, 12, 13
DCO2_RANGE, DCO2_WAVE, DCO2_TUNE, DCO2_FINE = 16, 17, 19, 20
DCO_XMOD = 18
MIX_DCO1, MIX_DCO2 = 28, 29
HPF, VCF_FREQ, VCF_RES, VCF_ENV, VCF_KEY, VCF_ENV_MODE = 33, 34, 35, 37, 38, 40
ENV1, ENV2 = 47, 52                         # each: attack, decay, sustain, release, key follow
ENV1_KEY, ENV2_KEY = ENV1 + 4, ENV2 + 4
KEY_FOLLOW_PERMILLE = 260                   # per ENV KEY position: PG-8X 2.0.13, 0.26 octave an octave
VCA_MODE = 58                               # 0..63 gate, 64..127 ENV-2
DCO1_LFO, DCO2_LFO = 14, 21
LFO_WF, LFO_DELAY, LFO_RATE = 44, 45, 46

LFO_WAVE = [3, 2, 0, 0]                     # random, square, triangle -> engine fm_lfo_wave
VCA_DYNA, CHORUS = 42, 43
DCO1_ENV, DCO2_ENV, DCO_DYNA, DCO_ENV_MODE = 15, 22, 26, 27
MIX_ENV, MIX_DYNA, MIX_ENV_MODE = 30, 31, 32
VCF_LFO, VCF_DYNA, VCA_LEVEL = 36, 39, 41
DYNAMICS_PCT = [0, 33, 67, 100]             # DYNAMICS off, 1, 2, 3

# The 4-position switches send 0, 32, 64, 96: position = value >> 5. Waveform positions run noise,
# square, pulse, sawtooth (Roland lists them sawtooth, pulse, square, noise from the top).
WAVE = [3, 2, 1, 0]                         # position -> engine fm_wave (noise, square, pulse, saw)


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def c_constants(root=ROOT):
    """The integer #defines and enum members of engine/*.h, by name."""
    names = {}
    for path in sorted(glob.glob(os.path.join(root, "engine", "*.h"))):
        with open(path) as f:
            text = re.sub(r"/\*.*?\*/|//[^\n]*", "", f.read(), flags=re.S)
        for name, v in re.findall(r"^\s*#define\s+(\w+)\s+\(?(-?\d+)u?\)?\s*$", text, re.M):
            names[name] = int(v)
        for body in re.findall(r"enum\s*\w*\s*\{([^}]*)\}", text):
            n = 0
            for member in body.split(","):
                m = re.match(r"\s*(\w+)\s*(?:=\s*(-?\d+))?\s*$", member)
                if m:
                    n = int(m.group(2)) if m.group(2) else n
                    names[m.group(1)] = n
                    n += 1
    return names


def c_int(expr, names):
    """A C integer expression of numbers, names and + - * ( ) as its value."""
    def ev(n):
        if isinstance(n, ast.Constant) and isinstance(n.value, int):
            return n.value
        if isinstance(n, ast.Name):
            return names[n.id]
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.USub):
            return -ev(n.operand)
        if isinstance(n, ast.BinOp) and type(n.op) in (ast.Add, ast.Sub, ast.Mult):
            a, b = ev(n.left), ev(n.right)
            return a + b if isinstance(n.op, ast.Add) else a - b if isinstance(n.op, ast.Sub) else a * b
        raise ValueError("not a plain C integer expression: %r" % expr)
    return ev(ast.parse(expr.strip(), mode="eval").body)


def engine_ranges(root=ROOT):
    """Each engine param's (min, max) from engine/params.c's table, by FM_PARAM_ suffix."""
    with open(os.path.join(root, "engine", "params.c")) as f:
        text = re.sub(r"/\*.*?\*/", "", f.read(), flags=re.S)
    names = c_constants(root)
    ranges = {}
    for param, lo, hi in re.findall(r"\{\s*FM_PARAM_(\w+)\s*,\s*\"[^\"]*\"\s*,([^,]+),([^,]+),[^}]*\}", text):
        ranges[param] = (c_int(lo, names), c_int(hi, names))
    return ranges


# The patch table's fields, in firmware/patches.h's order (PATCH_<name>). param: the engine's
# fm_param_id (FM_PARAM_<param>) the field sets, or None while the engine lacks the feature (the
# field waits for the issue). lo, hi: the field's own range; for the fields the engine has, its range
# from engine/params.c (filled in below).
Field = namedtuple("Field", "name param lo hi waits")
FIELDS = [
    Field("OSC1_WAVE", "OSC1_WAVE", None, None, None),
    Field("OSC1_PULSE_WIDTH_PCT", "OSC1_PULSE_WIDTH_PCT", None, None, None),
    Field("OSC1_OCTAVE", "OSC1_OCTAVE", None, None, None),
    Field("OSC1_COARSE_SEMIS", None, -12, 12, "DCO-1 coarse tune, folded into osc2 for now (no issue yet)"),
    Field("OSC2_WAVE", "OSC2_WAVE", None, None, None),
    Field("OSC2_PULSE_WIDTH_PCT", "OSC2_PULSE_WIDTH_PCT", None, None, None),
    Field("OSC2_OCTAVE", "OSC2_OCTAVE", None, None, None),
    Field("OSC2_COARSE_SEMIS", "OSC2_COARSE_SEMIS", None, None, None),
    Field("OSC2_FINE_CENTS", "OSC2_FINE_CENTS", None, None, None),
    Field("OSC1_LEVEL_PERMILLE", "OSC1_LEVEL", None, None, None),
    Field("OSC2_LEVEL_PERMILLE", "OSC2_LEVEL", None, None, None),
    Field("OSC_SYNC", "OSC_SYNC", None, None, None),
    Field("XMOD_AM", "OSC_RING", None, None, None),
    Field("HIGHPASS_STEP", "HIGHPASS_STEP", None, None, None),
    Field("FILTER_CUTOFF_CENTS", "FILTER_CUTOFF_CENTS", None, None, None),
    Field("FILTER_RESONANCE_PERMILLE", "FILTER_RESONANCE", None, None, None),
    Field("FILTER_KEY_TRACK_PERMILLE", "FILTER_KEY_TRACK", None, None, None),
    Field("FILTER_ENV_AMOUNT_CENTS", "FILTER_ENV_AMOUNT_CENTS", None, None, None),
    Field("FILTER_ENV_ATTACK_MS", "FILTER_ENV_ATTACK_MS", None, None, None),
    Field("FILTER_ENV_DECAY_MS", "FILTER_ENV_DECAY_MS", None, None, None),
    Field("FILTER_ENV_SUSTAIN_PERMILLE", "FILTER_ENV_SUSTAIN", None, None, None),
    Field("FILTER_ENV_RELEASE_MS", "FILTER_ENV_RELEASE_MS", None, None, None),
    Field("FILTER_ENV_SOURCE", "FILTER_ENV_SOURCE", None, None, None),
    Field("AMP_ATTACK_MS", "AMP_ATTACK_MS", None, None, None),
    Field("AMP_DECAY_MS", "AMP_DECAY_MS", None, None, None),
    Field("AMP_SUSTAIN_PERMILLE", "AMP_SUSTAIN", None, None, None),
    Field("AMP_RELEASE_MS", "AMP_RELEASE_MS", None, None, None),
    Field("AMP_VELOCITY_PCT", "AMP_VELOCITY", None, None, None),
    Field("AMP_GATE", None, 0, 1, "#63"),
    Field("CHORUS_MODE", "CHORUS_MODE", None, None, None),
    Field("LFO_WAVE", "LFO_WAVE", None, None, None),
    Field("LFO_RATE_CENTIHZ", "LFO_RATE_CENTIHZ", None, None, None),
    Field("LFO_DELAY_MS", "LFO_DELAY_MS", None, None, None),
    Field("LFO_PITCH_CENTS", "LFO_PITCH_CENTS", None, None, None),
    Field("LFO_PITCH_OSC2_CENTS", "LFO_PITCH_OSC2_CENTS", None, None, None),
    Field("PITCH_ENV_OSC1_CENTS", "PITCH_ENV_OSC1_CENTS", None, None, None),
    Field("PITCH_ENV_OSC2_CENTS", "PITCH_ENV_OSC2_CENTS", None, None, None),
    Field("PITCH_ENV_SOURCE", "PITCH_ENV_SOURCE", None, None, None),
    Field("PITCH_ENV_DYNA", "PITCH_ENV_DYNA", None, None, None),
    Field("OSC_MIX_ENV_PERMILLE", "OSC_MIX_ENV", None, None, None),
    Field("OSC_MIX_ENV_SOURCE", "OSC_MIX_ENV_SOURCE", None, None, None),
    Field("OSC_MIX_ENV_VELOCITY_PCT", None, 0, 100, "velocity on the mix envelope (no issue yet)"),
    Field("FILTER_LFO_PERMILLE", None, 0, 1000, "#8"),
    Field("FILTER_ENV_DYNA", "FILTER_ENV_DYNA", None, None, None),
    Field("AMP_KEY_FOLLOW_PERMILLE", "AMP_KEY_FOLLOW", None, None, None),
    Field("FILTER_ENV_KEY_FOLLOW_PERMILLE", "FILTER_ENV_KEY_FOLLOW", None, None, None),
    Field("AMP_LEVEL_PERMILLE", None, 0, 1000, "a VCA level (no issue yet)"),
    Field("ONSET_THUMP", "ONSET_THUMP", None, None, None),
]
_RANGES = engine_ranges()
FIELDS = [f._replace(lo=_RANGES[f.param][0], hi=_RANGES[f.param][1]) if f.param else f for f in FIELDS]


NAME_MAX = 12                               # firmware/patches.h PATCH_NAME_MAX


def emit_c(tones, source="data/jx8p-factory-1-32.syx", sha256=""):
    """firmware/patches.c: the tones as the patch table, one designated initializer per field."""
    out = ["/* The 32 JX-8P factory tones as fm1-8p patches. Generated by tools/jx8p.py from",
           " * %s%s" % (source, ", sha256" if sha256 else ".")]
    if sha256:
        out.append(" * %s." % sha256)
    out += [" * Do not edit: run `python3 tools/jx8p.py`. Where the data comes from and how each",
            " * parameter maps: docs/patches.md. */",
            '#include "patches.h"',
            '#include "../engine/fm1_8p.h"',
            "",
            "#define V(x) ((uint16_t)((x) + PATCH_BIAS))",
            "",
            "const patch PATCHES[PATCH_COUNT] = {"]
    for n, t in enumerate(tones):
        assert len(t.name) <= NAME_MAX and '"' not in t.name and "\\" not in t.name
        e = to_engine(t)
        out.append('    {"%s", {      /* %d */' % (t.name, n + 1))
        line = ""
        for f in FIELDS:
            item = "[PATCH_%s] = V(%d)," % (f.name, e[f.name])
            if line and len(line) + 1 + len(item) > 100:
                out.append(line)
                line = ""
            line = (line + " " if line else "        ") + item
        out.append(line)
        out.append("    }},")
    out += ["};",
            "",
            "/* the engine param each field sets; PATCH_NONE: the field waits for its feature */",
            "static const int8_t PARAM[PATCH_FIELD_COUNT] = {"]
    for f in FIELDS:
        out.append("    [PATCH_%s] = %s,%s" % (
            f.name, "FM_PARAM_" + f.param if f.param else "PATCH_NONE",
            "" if f.param else "   /* %s */" % f.waits))
    out += ["};",
            "",
            "int32_t patch_param(patch_field f)",
            "{",
            "    return (uint32_t)f < PATCH_FIELD_COUNT ? PARAM[f] : PATCH_NONE;",
            "}"]
    return "\n".join(out) + "\n"


def read_c(text):
    """The table in emit_c's text, as [(name, {field: value})]."""
    rows = []
    for line in text.splitlines():
        m = re.match(r'\s*\{"([^"]*)", \{', line)
        if m:
            rows.append((m.group(1), {}))
        for name, v in re.findall(r"\[PATCH_(\w+)\] = V\((-?\d+)\)", line):
            rows[-1][1][name] = int(v)
    return rows


def switch(v):
    return v >> 5


def rnd(x):
    """Round half away from zero, to an int (the same on every Python)."""
    return int(math.copysign(math.floor(abs(x) + 0.5), x))


def permille(v):
    """A knob, 0..127, as 0..1000."""
    return rnd(v * 1000 / 127)


# VCF RES byte -> the engine's resonance per mille, for PG-8X 2.0.13's peak heights over the passband
# (VCF FREQ 64 and 80, noise in): +4 dB at 64, +8 at 80, +16 at 96, +24 at 112, +31 at 127. PG-8X's
# curve has a hump: +6.5 dB at 32, down to +3.7 at 56 (#98; docs/patches.md).
RESONANCE_PERMILLE = ((0, 0), (8, 180), (12, 245), (16, 290), (20, 340), (24, 390), (28, 445), (32, 500),
                      (36, 465), (40, 435), (44, 415), (48, 395), (52, 380), (56, 375), (60, 380),
                      (64, 400), (68, 425), (72, 465), (80, 575), (88, 690), (96, 795), (104, 860),
                      (112, 900), (120, 925), (127, 935))


def resonance_permille(v):
    """VCF RES, 0..127, as the engine's resonance: RESONANCE_PERMILLE, linear between its points."""
    for (b0, p0), (b1, p1) in zip(RESONANCE_PERMILLE, RESONANCE_PERMILLE[1:]):
        if v <= b1:
            return rnd(p0 + (p1 - p0) * (v - b0) / (b1 - b0))
    return RESONANCE_PERMILLE[-1][1]


def env(p, base):
    """The envelope at base (ENV1 or ENV2) as engine attack ms, decay ms, sustain per mille and
    release ms. Attack is the time to the peak of PG-8X 2.0.13's RC charge (attack_ms), 1 ms to
    about 33.5 s. Decay and release are exponential on both, and their engine times are the time
    constant (decay_ms, release_ms)."""
    a, d, s, r = p[base:base + 4]
    return attack_ms(a), decay_ms(d), permille(s), release_ms(r)


# Below 16 PG-8X 2.0.13 steps in pairs and leaves the curve: its decay floors near 4 ms, its release
# goes down to about 1 ms (tau in ms per even byte, sample-accurate probe, 2026-11-11).
_LOW_DECAY_MS = (4, 4, 4, 4, 4, 5, 7, 12)
_LOW_RELEASE_MS = (1, 1, 2, 3, 4, 5, 9, 13)


def _tau_ms(v):
    return rnd(9.884 * (2.6717 ** (v / 16) - 0.1327))


def decay_ms(v):
    """ENV DECAY, 0..127, as the engine's time constant tau in ms: 9.884 x (2.6717^(v / 16) - 0.1327),
    fitted to PG-8X 2.0.13's t_20dB / ln 10 (within 7 % from 16 up): x2.67 per 16 steps, 127 about
    24.6 s. Below 16, PG-8X's measured steps (_LOW_DECAY_MS)."""
    return _LOW_DECAY_MS[v >> 1] if v < 16 else _tau_ms(v)


def release_ms(v):
    """ENV RELEASE, 0..127: the same curve as decay_ms from 16 up; below it PG-8X's measured steps,
    which go shorter than the decay's (_LOW_RELEASE_MS)."""
    return _LOW_RELEASE_MS[v >> 1] if v < 16 else _tau_ms(v)


# Below 16 PG-8X's attack shortens to a few ms by 8 and under 2 ms below it (time to the peak in ms
# for bytes 8..15, the same probe, 2026-11-11)
_LOW_ATTACK_MS = {8: 3, 9: 4, 10: 6, 11: 8, 12: 11, 13: 14, 14: 18, 15: 23}


def attack_ms(v):
    """ENV ATTACK, 0..127, as the engine's attack time in ms, the time to the peak of its RC charge
    toward 1.6 x the peak: 14.1821 x (2.6607^(v / 16) - 0.4954), fitted to PG-8X 2.0.13 (within 4 %
    from 40 up, 11 % at 16..32; x2.66 per 16 steps, as its decay and release), at most 32 s, the
    range's end (the patch table's int16; PG-8X's 127 is 34 s). Below 8 it is 1 ms (PG-8X under
    2 ms: a ramp, not a click, as GATE), 8..15 PG-8X's steps."""
    if v < 8:
        return 1
    if v < 16:
        return _LOW_ATTACK_MS[v]
    return min(32000, rnd(14.1821 * (2.6607 ** (v / 16) - 0.4954)))


def env_mode(v):
    """An ENV MODE switch (0 ENV-2 inverted, 32 ENV-2, 64 ENV-1 inverted, 96 ENV-1) as the
    envelope's offset and the sign it is applied with."""
    pos = switch(v)
    return (ENV1 if pos >= 2 else ENV2), (1 if pos & 1 else -1)


def cutoff_cents(v):
    """VCF FREQ, 0..127, as the engine's cutoff in cents from MIDI note 0: 0.0988 v^2 + 115.03 v - 1474,
    fitted so the engine's resonant peak lands on PG-8X 2.0.13's (VCF RES 96-127, no key follow, C4;
    within 0.45 semitone over 48..112; docs/patches.md). Below 13 it is under the engine's lowest
    cutoff, note 0 (8.2 Hz), and clamps there."""
    return max(0, rnd(0.0988 * v * v + 115.03 * v - 1474))


def key_track_permille(v):
    """VCF KEY, 0..127, as the engine's key track: up to 1.29 octaves an octave at 127, as PG-8X
    2.0.13 (match.py --param VCF_KEY, #95)."""
    return rnd(1290 * v / 127)


def filter_env_cents(v):
    """VCF ENV, 0..127, as the filter envelope's depth in cents: 129 a step, fitted to PG-8X 2.0.13
    (within 5 % over 8..48; docs/patches.md). 127 is 16383 cents."""
    return 129 * v


def hpf_step(v):
    """HPF, 0..127 (four positions), as the engine's highpass step, as PG-8X 2.0.13 plays it
    (docs/patches.md): 1 is flat and 2 and 3 are the engine's steps 2 and 3. 0 is a bass boost
    the engine lacks, so it is flat too for now (#93)."""
    pos = switch(v)
    return 0 if pos < 2 else pos


def tune_semis(v):
    """DCO TUNE, 0..127, in semitones -12..+12. Inferred, not documented (docs/patches.md): value >> 2
    with a dead band of four steps (56..71) at 0, so the factory tones' fifths (FAT FIFTH 97, ORGAN 1
    96) are fifths and their unisons (LO STRINGS 66 / 60, PIPE ORGAN 58 / 58) are unisons."""
    step = v >> 2
    if step < 14:
        return max(step - 14, -12)
    if step > 17:
        return min(step - 17, 12)
    return 0


def pitch_semis(rng, v):
    """Range (16' 8' 4' 2') and tune as semitones from 8'."""
    return 12 * (switch(rng) - 1) + tune_semis(v)


def octave_and_semis(total):
    """Semitones from 8' (-36..+36) as engine octave (-2..+2) and semitones (-12..+12): whole
    octaves go to the octave, so a tune of -12 on 8' becomes 16'."""
    octave = max(-2, min(2, (total + 6) // 12))
    return octave, total - 12 * octave


def to_engine(tone):
    """One tone's engine values: {field name: value}, field names as in firmware/patches.h."""
    p = tone.params
    e = {
        "OSC1_WAVE": WAVE[switch(p[DCO1_WAVE])],
        "OSC2_WAVE": WAVE[switch(p[DCO2_WAVE])],
    }
    for n in (1, 2):
        e["OSC%d_PULSE_WIDTH_PCT" % n] = 25 if e["OSC%d_WAVE" % n] == 1 else 50
    # osc1 has no coarse: it plays whole octaves, the nearest to its pitch that leaves osc2 in range,
    # and osc2 moves by what osc1 lacks so the interval holds. OSC1_COARSE_SEMIS keeps that amount.
    pitch1, pitch2 = pitch_semis(p[DCO1_RANGE], p[DCO1_TUNE]), pitch_semis(p[DCO2_RANGE], p[DCO2_TUNE])
    octave1 = min((o for o in range(-2, 3) if -36 <= pitch2 - (pitch1 - 12 * o) <= 36),
                  key=lambda o: abs(pitch1 - 12 * o))
    lack = pitch1 - 12 * octave1
    e["OSC1_OCTAVE"], e["OSC1_COARSE_SEMIS"] = octave1, lack
    e["OSC2_OCTAVE"], e["OSC2_COARSE_SEMIS"] = octave_and_semis(pitch2 - lack)
    e["OSC2_FINE_CENTS"] = max(-50, min(50, rnd((p[DCO2_FINE] - 64) * 50 / 64)))
    # The mixer sums the DCOs, each at its level byte over 127. MIX ENV adds depth x envelope to
    # DCO-2's byte (inverted: takes it away), clamped to 0..127: PG-8X 2.0.13, measured (#102).
    l1, l2 = p[MIX_DCO1], p[MIX_DCO2]
    src, sign = env_mode(p[MIX_ENV_MODE])
    e["OSC1_LEVEL_PERMILLE"], e["OSC2_LEVEL_PERMILLE"] = rnd(1000 * l1 / 127), rnd(1000 * l2 / 127)
    e["OSC_MIX_ENV_PERMILLE"] = sign * rnd(1000 * p[MIX_ENV] / 127)
    e["OSC_MIX_ENV_SOURCE"] = 1 if src == ENV2 and p[VCA_MODE] >= 64 else 0
    xmod = switch(p[DCO_XMOD])
    e["OSC_SYNC"] = 1 if xmod in (1, 2) else 0
    e["XMOD_AM"] = 1 if xmod in (2, 3) else 0
    e["HIGHPASS_STEP"] = hpf_step(p[HPF])
    e["ONSET_THUMP"] = 1                    # PG-8X 2.0.13's thump when a voice first plays (#146)
    e["FILTER_CUTOFF_CENTS"] = cutoff_cents(p[VCF_FREQ])
    e["FILTER_RESONANCE_PERMILLE"] = resonance_permille(p[VCF_RES])
    # PG-8X's key follow pivots at C2 and the engine's at C4: the track's rise from C2 to C4 moves
    # the cutoff, so every note gets PG-8X's cutoff (#95)
    track = key_track_permille(p[VCF_KEY])
    e["FILTER_KEY_TRACK_PERMILLE"] = track
    e["FILTER_CUTOFF_CENTS"] += track * 24 // 10
    gate = 1 if p[VCA_MODE] < 64 else 0
    a, d, s, r = (1, 2, 1000, 5) if gate else env(p, ENV2)
    # ENV KEY (#60): the times shorten above C2, 260 per mille an ENV KEY position (PG-8X 2.0.13)
    e["AMP_KEY_FOLLOW_PERMILLE"] = 0 if gate else KEY_FOLLOW_PERMILLE * switch(p[ENV2_KEY])
    e.update(AMP_ATTACK_MS=a, AMP_DECAY_MS=d, AMP_SUSTAIN_PERMILLE=s, AMP_RELEASE_MS=r, AMP_GATE=gate)
    # the engine's envelope 1 runs ENV-1's own times (#59), and the amp envelope ENV-2's; with the
    # VCA on GATE ENV-2 runs nowhere, so envelope 1 takes it for a VCF that names it
    src, sign = env_mode(p[VCF_ENV_MODE])
    runs = ENV2 if src == ENV2 and p[VCA_MODE] < 64 else ENV1
    a, d, s, r = env(p, runs)
    e["FILTER_ENV_KEY_FOLLOW_PERMILLE"] = KEY_FOLLOW_PERMILLE * switch(p[runs + 4])
    e.update(FILTER_ENV_ATTACK_MS=a, FILTER_ENV_DECAY_MS=d, FILTER_ENV_SUSTAIN_PERMILLE=s,
             FILTER_ENV_RELEASE_MS=r,
             FILTER_ENV_SOURCE=1 if src == ENV2 and p[VCA_MODE] >= 64 else 0,   # as MIX ENV's
             FILTER_ENV_AMOUNT_CENTS=sign * filter_env_cents(p[VCF_ENV]))
    e["LFO_WAVE"] = LFO_WAVE[switch(p[LFO_WF])]
    # PG-8X 2.0.13: 0.537 Hz at 64, x 2.664 every 16 steps (25.4 Hz at 127), from 32 up. Below 32
    # PG-8X flattens near 0.05 Hz (0.050 Hz measured at 16): the curve stops there (at 25)
    e["LFO_RATE_CENTIHZ"] = max(5, rnd(53.7 * 2.664 ** ((p[LFO_RATE] - 64) / 16)))
    e["LFO_DELAY_MS"] = rnd(3000 * (p[LFO_DELAY] / 127) ** 3)
    # a vibrato depth per oscillator (#58). PG-8X 2.0.13: linear, 4.5 cents per step (572 cents
    # at 127), the same law for both
    e["LFO_PITCH_CENTS"] = rnd(4.5 * p[DCO1_LFO])
    e["LFO_PITCH_OSC2_CENTS"] = rnd(4.5 * p[DCO2_LFO])
    e["CHORUS_MODE"] = min(switch(p[CHORUS]), 2)
    e["AMP_VELOCITY_PCT"] = DYNAMICS_PCT[switch(p[VCA_DYNA])]
    # DCO ENV (#58), PG-8X 2.0.13: at full envelope 24.8 cents a step less 13 (0 at 0, 3137 at
    # 127), linear in the envelope, the same on every key; each moves its own oscillator
    src, sign = env_mode(p[DCO_ENV_MODE])
    for n, byte in ((1, DCO1_ENV), (2, DCO2_ENV)):
        e["PITCH_ENV_OSC%d_CENTS" % n] = sign * rnd(24.8 * p[byte] - 13) if p[byte] else 0
    e["PITCH_ENV_SOURCE"] = 1 if src == ENV2 and p[VCA_MODE] >= 64 else 0   # as MIX ENV's
    e["PITCH_ENV_DYNA"] = switch(p[DCO_DYNA])     # the engine's DYNAMICS curve (engine/dyna.c)
    # waiting for velocity on the mix envelope
    e["OSC_MIX_ENV_VELOCITY_PCT"] = DYNAMICS_PCT[switch(p[MIX_DYNA])]
    # waiting for LFO to cutoff and filter velocity (#8), a VCA level
    e["FILTER_LFO_PERMILLE"] = permille(p[VCF_LFO])
    e["FILTER_ENV_DYNA"] = switch(p[VCF_DYNA])    # the engine's DYNAMICS curve (engine/dyna.c)
    e["AMP_LEVEL_PERMILLE"] = permille(p[VCA_LEVEL])
    return e


def generate(syx_path):
    """firmware/patches.c's text from the dump at syx_path."""
    with open(syx_path, "rb") as f:
        data = f.read()
    tones = parse_syx(data, syx_path)
    if len(tones) != 32:
        raise ValueError("%s: %d tones, not 32" % (syx_path, len(tones)))
    return emit_c(tones, "data/" + os.path.basename(syx_path), hashlib.sha256(data).hexdigest())


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    syx = argv[1] if len(argv) > 1 else os.path.join(root, "data", "jx8p-factory-1-32.syx")
    out = argv[2] if len(argv) > 2 else os.path.join(root, "firmware", "patches.c")
    text = generate(syx)                    # first: a refused dump leaves the old table
    with open(out, "w") as f:
        f.write(text)
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv)
