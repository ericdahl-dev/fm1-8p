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
    Field("OSC_MIX_PERMILLE", "OSC_MIX", None, None, None),
    Field("OSC_SYNC", "OSC_SYNC", None, None, None),
    Field("XMOD_AM", None, 0, 1, "cross mod as the JX-8P does it (no issue yet)"),
    Field("HIGHPASS_STEP", "HIGHPASS_STEP", None, None, None),
    Field("FILTER_CUTOFF_CENTS", "FILTER_CUTOFF_CENTS", None, None, None),
    Field("FILTER_RESONANCE_PERMILLE", "FILTER_RESONANCE", None, None, None),
    Field("FILTER_KEY_TRACK_PERMILLE", "FILTER_KEY_TRACK", None, None, None),
    Field("FILTER_ENV_AMOUNT_CENTS", "FILTER_ENV_AMOUNT_CENTS", None, None, None),
    Field("FILTER_ENV_ATTACK_MS", "FILTER_ENV_ATTACK_MS", None, None, None),
    Field("FILTER_ENV_DECAY_MS", "FILTER_ENV_DECAY_MS", None, None, None),
    Field("FILTER_ENV_SUSTAIN_PERMILLE", "FILTER_ENV_SUSTAIN", None, None, None),
    Field("FILTER_ENV_RELEASE_MS", "FILTER_ENV_RELEASE_MS", None, None, None),
    Field("FILTER_ENV_SOURCE", None, 0, 3, "#59"),
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
    Field("LFO_PITCH_OSC2_CENTS", None, 0, 1200, "a vibrato depth per oscillator (no issue yet)"),
    Field("PITCH_ENV_OSC1_CENTS", None, -2400, 2400, "#58"),
    Field("PITCH_ENV_OSC2_CENTS", None, -2400, 2400, "#58"),
    Field("PITCH_ENV_SOURCE", None, 0, 3, "#58"),
    Field("PITCH_ENV_VELOCITY_PCT", None, 0, 100, "#58"),
    Field("OSC_MIX_ENV_PERMILLE", "OSC_MIX_ENV", None, None, None),
    Field("OSC_MIX_ENV_SOURCE", "OSC_MIX_ENV_SOURCE", None, None, None),
    Field("OSC_MIX_ENV_VELOCITY_PCT", None, 0, 100, "velocity on the mix envelope (no issue yet)"),
    Field("FILTER_LFO_PERMILLE", None, 0, 1000, "#8"),
    Field("FILTER_VELOCITY_PCT", None, 0, 100, "#8"),
    Field("ENV1_KEY_FOLLOW", None, 0, 3, "#60"),
    Field("ENV2_KEY_FOLLOW", None, 0, 3, "#60"),
    Field("AMP_LEVEL_PERMILLE", None, 0, 1000, "a VCA level (no issue yet)"),
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


def env(p, base):
    """The envelope at base (ENV1 or ENV2) as engine attack ms, decay ms, sustain per mille and
    release ms. Times are cubic in the knob: attack 1..3000 ms, decay and release 2..10000 ms (an
    estimate: docs/patches.md)."""
    def t(v, lo, hi):
        return rnd(lo + (hi - lo) * (v / 127) ** 3)
    a, d, s, r = p[base:base + 4]
    return t(a, 1, 3000), t(d, 2, 10000), permille(s), t(r, 2, 10000)


def env_mode(v):
    """An ENV MODE switch (0 ENV-2 inverted, 32 ENV-2, 64 ENV-1 inverted, 96 ENV-1) as the
    envelope's offset and the sign it is applied with."""
    pos = switch(v)
    return (ENV1 if pos >= 2 else ENV2), (1 if pos & 1 else -1)


def mix_permille(l1, l2):
    """DCO levels as the engine's osc mix: DCO-2's share, per mille. DCO-1 off is osc2 only, whatever
    DCO-2's level (the engine's mix has no silence)."""
    return rnd(1000 * l2 / (l1 + l2)) if l1 else 1000


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
    # MIX ENV scales DCO-2's level (est.): from 1 - depth to 1 as the envelope rises (inverted: the
    # other way). osc_mix is the mix with the envelope at rest, osc_mix_env the change at its peak.
    l1, l2 = p[MIX_DCO1], p[MIX_DCO2]
    src, sign = env_mode(p[MIX_ENV_MODE])
    depth = p[MIX_ENV] / 127
    low = mix_permille(l1, l2 * (1 - depth))
    full = mix_permille(l1, l2)
    rest, peak = (low, full) if sign > 0 else (full, low)
    e["OSC_MIX_PERMILLE"] = rest
    e["OSC_MIX_ENV_PERMILLE"] = peak - rest
    e["OSC_MIX_ENV_SOURCE"] = 1 if src == ENV2 and p[VCA_MODE] >= 64 else 0
    xmod = switch(p[DCO_XMOD])
    e["OSC_SYNC"] = 1 if xmod in (1, 2) else 0
    e["XMOD_AM"] = 1 if xmod in (2, 3) else 0
    e["HIGHPASS_STEP"] = switch(p[HPF])
    e["FILTER_CUTOFF_CENTS"] = 1200 + 100 * p[VCF_FREQ]
    e["FILTER_RESONANCE_PERMILLE"] = permille(p[VCF_RES])
    e["FILTER_KEY_TRACK_PERMILLE"] = permille(p[VCF_KEY])
    gate = 1 if p[VCA_MODE] < 64 else 0
    a, d, s, r = (1, 2, 1000, 5) if gate else env(p, ENV2)
    e.update(AMP_ATTACK_MS=a, AMP_DECAY_MS=d, AMP_SUSTAIN_PERMILLE=s, AMP_RELEASE_MS=r, AMP_GATE=gate)
    src, sign = env_mode(p[VCF_ENV_MODE])
    a, d, s, r = env(p, src)
    e.update(FILTER_ENV_ATTACK_MS=a, FILTER_ENV_DECAY_MS=d, FILTER_ENV_SUSTAIN_PERMILLE=s,
             FILTER_ENV_RELEASE_MS=r, FILTER_ENV_SOURCE=switch(p[VCF_ENV_MODE]),
             FILTER_ENV_AMOUNT_CENTS=sign * 75 * p[VCF_ENV])
    e["LFO_WAVE"] = LFO_WAVE[switch(p[LFO_WF])]
    # PG-8X 2.0.13: 0.537 Hz at 64, x 2.664 every 16 steps (25.4 Hz at 127), from 32 up. Below 32
    # PG-8X flattens near 0.05 Hz (0.050 Hz measured at 16): the curve stops there (at 25)
    e["LFO_RATE_CENTIHZ"] = max(5, rnd(53.7 * 2.664 ** ((p[LFO_RATE] - 64) / 16)))
    e["LFO_DELAY_MS"] = rnd(3000 * (p[LFO_DELAY] / 127) ** 3)
    # the engine's one vibrato depth moves both oscillators: DCO-1's sets it, DCO-2's waits.
    # PG-8X 2.0.13: linear, 4.5 cents per step (572 cents at 127)
    e["LFO_PITCH_CENTS"] = rnd(4.5 * p[DCO1_LFO])
    e["LFO_PITCH_OSC2_CENTS"] = rnd(1200 * (p[DCO2_LFO] / 127) ** 2)
    e["CHORUS_MODE"] = min(switch(p[CHORUS]), 2)
    e["AMP_VELOCITY_PCT"] = DYNAMICS_PCT[switch(p[VCA_DYNA])]
    # waiting for the pitch envelope (#58)
    _, sign = env_mode(p[DCO_ENV_MODE])
    e["PITCH_ENV_OSC1_CENTS"] = sign * rnd(2400 * (p[DCO1_ENV] / 127) ** 2)
    e["PITCH_ENV_OSC2_CENTS"] = sign * rnd(2400 * (p[DCO2_ENV] / 127) ** 2)
    e["PITCH_ENV_SOURCE"] = switch(p[DCO_ENV_MODE])
    e["PITCH_ENV_VELOCITY_PCT"] = DYNAMICS_PCT[switch(p[DCO_DYNA])]
    # waiting for velocity on the mix envelope
    e["OSC_MIX_ENV_VELOCITY_PCT"] = DYNAMICS_PCT[switch(p[MIX_DYNA])]
    # waiting for LFO to cutoff and filter velocity (#8), envelope key follow (#60), a VCA level
    e["FILTER_LFO_PERMILLE"] = permille(p[VCF_LFO])
    e["FILTER_VELOCITY_PCT"] = DYNAMICS_PCT[switch(p[VCF_DYNA])]
    e["ENV1_KEY_FOLLOW"] = switch(p[ENV1 + 4])
    e["ENV2_KEY_FOLLOW"] = switch(p[ENV2 + 4])
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
