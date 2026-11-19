"""The engine's parameters, each written once (#196).

tools/jx8p.py generates from this list (`make generate`):
- engine/fm1_8p_params.h: the fm_param_id enum
- engine/params.c: the range and default table, fm_param_info_get

`make check` fails when a generated file differs from what the list makes now. The Python tools
(jx8p.py, gate_cases.py, scorecard.py) read the list instead of parsing params.c.

Rules:
- id: stable, the list's order from 0. Never reorder, renumber or remove a param: append.
- enum: FM_PARAM_<enum>. name: the host's and tests' name (fm1-8p-render -s name=value).
- min, max, default: C integer expressions over engine/*.h's constants, in the units the name gives.
- tag: its store tag (firmware/patch_store.c): explicit and frozen. A new param takes the next free
  tag; a tag is never reused or moved (docs/patches.md, Storage).
- doc: the enum member's comment ("" for none).
"""
from collections import namedtuple

Param = namedtuple("Param", "id enum name min max default tag doc")


def P(*a):
    return Param(*a)


PARAMS = [
    P(0, 'AMP_ATTACK_MS', 'amp_attack_ms', '0', '32000', '5', 0,
      ''),
    P(1, 'AMP_DECAY_MS', 'amp_decay_ms', '0', '32000', '300', 1,
      ''),
    P(2, 'AMP_SUSTAIN', 'amp_sustain_permille', '0', '1000', '700', 2,
      'per mille of full level, 0..1000'),
    P(3, 'AMP_RELEASE_MS', 'amp_release_ms', '0', '32000', '300', 3,
      ''),
    P(4, 'BEND_RANGE', 'bend_range_semitones', '0', '24', '2', 4,
      'semitones each way at full bend, 0..24'),
    P(5, 'VOICE_MODE', 'voice_mode', '0', '1', '0', 5,
      'FM_VOICE_MODE_POLY or FM_VOICE_MODE_UNISON'),
    P(6, 'UNISON_DETUNE_CENTS', 'unison_detune_cents', '0', '100', '20', 6,
      'unison spread, lowest to highest voice'),
    P(7, 'OSC1_WAVE', 'osc1_wave', '0', 'FM_WAVE_COUNT - 1', 'FM_WAVE_SAW', 7,
      'fm_wave: 0 saw, 1 pulse, 2 square, 3 noise, 4 spike (PG-8X pulse, #150)'),
    P(8, 'OSC1_PULSE_WIDTH_PCT', 'osc1_pulse_width_pct', '1', '99', '50', 8,
      'pulse duty, percent of a cycle'),
    P(9, 'OSC1_OCTAVE', 'osc1_octave', '-2', '2', '0', 9,
      '-2..+2 octaves'),
    P(10, 'OSC1_FINE_CENTS', 'osc1_fine_cents', '-100', '100', '0', 10,
      '-100..+100 cents'),
    P(11, 'OSC2_WAVE', 'osc2_wave', '0', 'FM_WAVE_COUNT - 1', 'FM_WAVE_SAW', 11,
      'the same five for osc2'),
    P(12, 'OSC2_PULSE_WIDTH_PCT', 'osc2_pulse_width_pct', '1', '99', '50', 12,
      ''),
    P(13, 'OSC2_OCTAVE', 'osc2_octave', '-2', '2', '0', 13,
      ''),
    P(14, 'OSC2_FINE_CENTS', 'osc2_fine_cents', '-100', '100', '0', 14,
      ''),
    P(15, 'OSC2_LEVEL', 'osc2_level_permille', '0', '1000', '0', 15,
      "per mille: osc2's level into the sum, 0 off"),
    P(16, 'CHORUS_MODE', 'chorus_mode', '0', 'FM_CHORUS_MODE_COUNT - 1', '0', 16,
      '0 = off, then the fm_chorus_mode modes'),
    P(17, 'LFO_WAVE', 'lfo_wave', '0', 'FM_LFO_WAVE_COUNT - 1', 'FM_LFO_TRIANGLE', 17,
      'fm_lfo_wave (docs/modulation.md)'),
    P(18, 'LFO_RATE_CENTIHZ', 'lfo_rate_centihz', '1', '3000', '500', 18,
      'hundredths of a hertz'),
    P(19, 'LFO_DELAY_MS', 'lfo_delay_ms', '0', '5000', '0', 19,
      'zero for this long, then fades in as long'),
    P(20, 'LFO_PITCH_CENTS', 'lfo_pitch_cents', '0', '1200', '0', 20,
      'vibrato depth at full LFO, each way'),
    P(21, 'AMP_VELOCITY', 'amp_velocity_pct', '0', '100', '0', 21,
      'percent: 0 ignores velocity, 100 full curve'),
    P(22, 'LFO_PW_PCT', 'lfo_pw_pct', '0', '49', '0', 22,
      'pulse width depth, percent of a cycle each way'),
    P(23, 'OSC_SYNC', 'osc_sync', '0', '1', '0', 23,
      '1: osc2 restarts with every cycle of osc1'),
    P(24, 'OSC_XMOD', 'osc_xmod_permille', '0', '1000', '0', 24,
      "per mille: osc1 bends osc2's frequency by up to +-100 %"),
    P(25, 'FILTER_CUTOFF_CENTS', 'filter_cutoff_cents', '0', 'FM_FILTER_OPEN_CENTS', 'FM_FILTER_OPEN_CENTS', 25,
      'lowpass cutoff at note 60 with no modulation, cents above MIDI note 0 (6900 = 440 Hz); FM_FILTER_OPEN_CENTS (the maximum) is fully open'),
    P(26, 'FILTER_RESONANCE', 'filter_resonance_permille', '0', '1000', '0', 26,
      'per mille; 1000 self-oscillates'),
    P(27, 'FILTER_KEY_TRACK', 'filter_key_track_permille', '0', '1300', '0', 27,
      'per mille of linear cents an octave (#253): 1000 adds 1200 linear cents an octave from note 60, before the cutoff curve'),
    P(28, 'FILTER_ENV_ATTACK_MS', 'filter_env_attack_ms', '0', '32000', '5', 28,
      ''),
    P(29, 'FILTER_ENV_DECAY_MS', 'filter_env_decay_ms', '0', '32000', '300', 29,
      ''),
    P(30, 'FILTER_ENV_SUSTAIN', 'filter_env_sustain_permille', '0', '1000', '500', 30,
      'per mille of full level, 0..1000'),
    P(31, 'FILTER_ENV_RELEASE_MS', 'filter_env_release_ms', '0', '32000', '300', 31,
      ''),
    P(32, 'FILTER_ENV_AMOUNT_CENTS', 'filter_env_amount_cents', '-16400', '16400', '0', 32,
      'linear cents the filter envelope adds to the cutoff at full level, before the cutoff curve (#253); negative sweeps the cutoff down'),
    P(33, 'HIGHPASS_STEP', 'highpass_step', '0', 'FM_FILTER_HIGHPASS_STEPS - 1', '0', 33,
      '0 (off) .. 3 (most low cut); 4 is the bass boost (JX-8P HPF 0, #93)'),
    P(34, 'OSC2_COARSE_SEMIS', 'osc2_coarse_semis', '-12', '12', '0', 34,
      "-12..+12 semitones, on top of osc2's octave and fine"),
    P(35, 'OSC_MIX_ENV', 'osc_mix_env_permille', '-1000', '1000', '0', 35,
      "per mille, -1000..+1000: the envelope's share added to osc2_level"),
    P(36, 'OSC_MIX_ENV_SOURCE', 'osc_mix_env_source', '0', '1', '0', 36,
      'the envelope it follows: 0 filter, 1 amp'),
    P(37, 'FILTER_ENV_SOURCE', 'filter_env_source', '0', '1', '0', 37,
      'the envelope the cutoff follows at filter_env_amount_cents: 0 envelope 1 (filter), 1 envelope 2 (amp)'),
    P(38, 'LFO_PITCH_OSC2_CENTS', 'lfo_pitch_osc2_cents', '-1', '1200', '-1', 38,
      "osc 2's vibrato depth; -1 (default): lfo_pitch_cents, which is then both's"),
    P(39, 'PITCH_ENV_OSC1_CENTS', 'pitch_env_osc1_cents', '-3200', '3200', '0', 39,
      "osc 1's pitch at full envelope, signed"),
    P(40, 'PITCH_ENV_OSC2_CENTS', 'pitch_env_osc2_cents', '-3200', '3200', '0', 40,
      "osc 2's"),
    P(41, 'PITCH_ENV_SOURCE', 'pitch_env_source', '0', '1', '0', 41,
      'the envelope it follows: 0 filter, 1 amp'),
    P(42, 'PITCH_ENV_DYNA', 'pitch_env_dyna', '0', '3', '0', 42,
      'DYNAMICS 0 (off) .. 3: velocity scales it'),
    P(43, 'OSC1_LEVEL', 'osc1_level_permille', '0', '1000', '1000', 43,
      "per mille: osc1's level into the sum, 0 off"),
    P(44, 'OSC_RING', 'osc_ring', '0', '1', '0', 44,
      "1: osc2 times osc1, the JX-8P's SYNC 2 and X-MOD"),
    P(45, 'FILTER_ENV_DYNA', 'filter_env_dyna', '0', '3', '0', 45,
      "DYNAMICS 0 (off) .. 3: velocity scales the filter envelope's cutoff depth"),
    P(46, 'AMP_KEY_FOLLOW', 'amp_key_follow_permille', '0', '1000', '0', 46,
      "per mille, 0..1000: the amp envelope's times x 2^(-this x octaves above C2) (ENV-2 KEY)"),
    P(47, 'FILTER_ENV_KEY_FOLLOW', 'filter_env_key_follow_permille', '0', '1000', '0', 47,
      'the same for the filter envelope (ENV-1 KEY)'),
    P(48, 'AMP_DYNA', 'amp_dyna', '0', '3', '0', 48,
      'VCA DYNAMICS 0 (off: amp_velocity) .. 3: the level is the DYNAMICS share at the velocity (engine/dyna.c)'),
    P(49, 'OSC_MIX_ENV_DYNA', 'osc_mix_env_dyna', '0', '3', '0', 49,
      "MIX DYNAMICS 0 (off) .. 3: osc_mix_env's depth times the DYNAMICS share at the velocity"),
    P(50, 'AMP_LEVEL', 'amp_level_permille', '0', '1000', '1000', 50,
      "per mille: the output level (VCA LEVEL), live; glides with setting_glide (#169)"),
    P(51, 'SETTING_GLIDE', 'setting_glide', '0', '1', '0', 51,
      "1: levels, filter and LFO depths glide as PG-8X's (#146)"),
    P(52, 'AMP_GATE', 'amp_gate', '0', '1', '0', 52,
      'VCA MODE GATE (#63): 1, the amp envelope is a gate (on with the key, a fixed short ramp), and the amp_* settings run envelope 2 for the cutoff, pitch and mix instead'),
    P(53, 'WHEEL_PITCH_CENTS', 'wheel_pitch_cents', '0', '1200', '50', 53,
      'the mod wheel (CC1) at full adds this to both oscillators\' vibrato depth (#41; 50 ct, Eric)'),
    P(54, 'WHEEL_PW_PCT', 'wheel_pw_pct', '0', '49', '0', 54,
      'the mod wheel (CC1) at full adds this to the LFO\'s pulse-width depth, at most 49 % in all (#41)'),
    P(55, 'OSC1_COARSE_SEMIS', 'osc1_coarse_semis', '-12', '12', '0', 55,
      '-12..+12 semitones, on top of osc1\'s octave and fine (DCO-1 TUNE, #108)'),
    P(56, 'PORTAMENTO_MS', 'portamento_ms', '0', '10000', '0', 56,
      'portamento (#62): each note glides from the last one played, the distance left falling by 1/e '
      'every this many ms (PG-8X\'s exponential glide); 0 off'),
]


# A factory patch's fields (firmware/patches.h's patch_field, generated from this list), in the
# order the patch table stores them. Each sets the engine param `param` (an enum above), or, with
# param None, waits for the feature `waits` names (lo, hi: the field's own range then). jx: the JX-8P
# SysEx / PG-800 parameter numbers (byte offsets in an APR's data, tools/jx8p.py) the field is
# converted from, every one of them (#195); () for none. doc: the enum member's comment.
Field = namedtuple("Field", "name param jx doc lo hi waits")


def F(name, param, jx, doc, lo=None, hi=None, waits=None):
    return Field(name, param, jx, doc, lo, hi, waits)


PATCH_FIELDS = [
    F('OSC1_WAVE', 'OSC1_WAVE', (12,), 'fm_wave'),
    F('OSC1_PULSE_WIDTH_PCT', 'OSC1_PULSE_WIDTH_PCT', (12,), ''),
    F('OSC1_OCTAVE', 'OSC1_OCTAVE', (11, 13, 16, 19), ''),
    F('OSC1_COARSE_SEMIS', 'OSC1_COARSE_SEMIS', (11, 13, 16, 19), 'DCO-1 tune: -12..+12 semitones over its octave (#108)'),
    F('OSC2_WAVE', 'OSC2_WAVE', (17,), ''),
    F('OSC2_PULSE_WIDTH_PCT', 'OSC2_PULSE_WIDTH_PCT', (17,), ''),
    F('OSC2_OCTAVE', 'OSC2_OCTAVE', (11, 13, 16, 19), ''),
    F('OSC2_COARSE_SEMIS', 'OSC2_COARSE_SEMIS', (11, 13, 16, 19), ''),
    F('OSC2_FINE_CENTS', 'OSC2_FINE_CENTS', (20,), ''),
    F('OSC1_LEVEL_PERMILLE', 'OSC1_LEVEL', (28,), 'DCO-1 level over 127, summed with DCO-2 (#102)'),
    F('AMP_LEVEL_PERMILLE', 'AMP_LEVEL', (41,), 'VCA LEVEL, 0.30 dB a step below 127'),
    F('OSC2_LEVEL_PERMILLE', 'OSC2_LEVEL', (29,), 'DCO-2 level over 127, before MIX ENV'),
    F('OSC_SYNC', 'OSC_SYNC', (18,), ''),
    F('XMOD_AM', 'OSC_RING', (18,), "osc_ring: the JX-8P's ring, SYNC 2 and X-MOD (#105)"),
    F('HIGHPASS_STEP', 'HIGHPASS_STEP', (33,), ''),
    F('FILTER_CUTOFF_CENTS', 'FILTER_CUTOFF_CENTS', (34, 38), ''),
    F('FILTER_RESONANCE_PERMILLE', 'FILTER_RESONANCE', (35,), ''),
    F('FILTER_KEY_TRACK_PERMILLE', 'FILTER_KEY_TRACK', (38,), ''),
    F('FILTER_ENV_AMOUNT_CENTS', 'FILTER_ENV_AMOUNT_CENTS', (37, 40), ''),
    F('FILTER_ENV_ATTACK_MS', 'FILTER_ENV_ATTACK_MS', (47,), ''),
    F('FILTER_ENV_DECAY_MS', 'FILTER_ENV_DECAY_MS', (48,), ''),
    F('FILTER_ENV_SUSTAIN_PERMILLE', 'FILTER_ENV_SUSTAIN', (49,), ''),
    F('FILTER_ENV_RELEASE_MS', 'FILTER_ENV_RELEASE_MS', (50,), ''),
    F('FILTER_ENV_SOURCE', 'FILTER_ENV_SOURCE', (40,), '0 the filter envelope, 1 the amp envelope (#59)'),
    F('AMP_ATTACK_MS', 'AMP_ATTACK_MS', (52,), ''),
    F('AMP_DECAY_MS', 'AMP_DECAY_MS', (53,), ''),
    F('AMP_SUSTAIN_PERMILLE', 'AMP_SUSTAIN', (54,), ''),
    F('AMP_RELEASE_MS', 'AMP_RELEASE_MS', (55,), ''),
    F('AMP_DYNA', 'AMP_DYNA', (42,), 'VCA DYNAMICS: 0 off .. 3 (engine/dyna.c)'),
    F('AMP_GATE', 'AMP_GATE', (58,), "VCA MODE GATE: 1 the VCA is the engine's gate (#63)"),
    F('CHORUS_MODE', 'CHORUS_MODE', (43,), ''),
    F('LFO_WAVE', 'LFO_WAVE', (44,), ''),
    F('LFO_RATE_CENTIHZ', 'LFO_RATE_CENTIHZ', (46,), ''),
    F('LFO_DELAY_MS', 'LFO_DELAY_MS', (45,), ''),
    F('LFO_PITCH_CENTS', 'LFO_PITCH_CENTS', (14,), "DCO-1's LFO depth: the engine's one moves both oscillators"),
    F('LFO_PITCH_OSC2_CENTS', 'LFO_PITCH_OSC2_CENTS', (21,), "DCO-2's LFO depth (LFO_PITCH_CENTS is DCO-1's)"),
    F('PITCH_ENV_OSC1_CENTS', 'PITCH_ENV_OSC1_CENTS', (15, 27), 'signed, at full envelope (#58)'),
    F('PITCH_ENV_OSC2_CENTS', 'PITCH_ENV_OSC2_CENTS', (22, 27), ''),
    F('PITCH_ENV_SOURCE', 'PITCH_ENV_SOURCE', (27,), '0 the filter envelope, 1 the amp envelope'),
    F('PITCH_ENV_DYNA', 'PITCH_ENV_DYNA', (26,), 'DCO DYNAMICS: 0 off .. 3'),
    F('OSC_MIX_ENV_PERMILLE', 'OSC_MIX_ENV', (30, 32), "MIX ENV: the mix's change at the envelope's peak"),
    F('OSC_MIX_ENV_SOURCE', 'OSC_MIX_ENV_SOURCE', (32,), '0 filter envelope, 1 amp envelope'),
    F('OSC_MIX_ENV_DYNA', 'OSC_MIX_ENV_DYNA', (31,), 'MIX DYNAMICS: 0 off .. 3'),
    F('FILTER_LFO_PERMILLE', None, (36,), 'waits #8: LFO to cutoff', lo=0, hi=1000, waits='#8'),
    F('FILTER_ENV_DYNA', 'FILTER_ENV_DYNA', (39,), 'VCF DYNAMICS: 0 off .. 3'),
    F('AMP_KEY_FOLLOW_PERMILLE', 'AMP_KEY_FOLLOW', (56,), 'ENV-2 KEY: 260 per mille a position (#60)'),
    F('FILTER_ENV_KEY_FOLLOW_PERMILLE', 'FILTER_ENV_KEY_FOLLOW', (51,), "the filter envelope's (ENV-1 KEY, or ENV-2's)"),
    F('SETTING_GLIDE', 'SETTING_GLIDE', (), "1: levels, filter and LFO depths glide as PG-8X's (#146)"),
]


# How the screen shows each param (#196): firmware/ui.c formats a value by its kind, with the unit and
# names given here. tools/jx8p.py writes firmware/ui_params.h from it (make generate). Every param has
# one; a new param fails test_params.py until it does.
#   kind   one of DISPLAY_KINDS (what each does: firmware/ui.c, value_parts)
#   unit   the unit after the number ("" none)
#   names  the names a choice shows, from its min (NAMES, NAMES_INNER_UNIT: one a value; NONZERO: 0 and
#          not 0; MASK3: by value & 3, 4 of them); NEG_NAME, OFF_OR_N, KEY_POS: the name for below
#          or at zero
DISPLAY_KINDS = (
    'NONE',             # nothing: the param shows through another (osc_ring through 23 X-MOD's pair)
    'CUTOFF',           # Hz, kHz, OPEN at the top
    'TENTHS',           # value / 10, rounded; below 0 is 0
    'ABS_TENTHS',       # |value| / 10, rounded (a signed depth's size)
    'ABS_HUNDREDTHS',   # |value| / 100, rounded (cents to semitones, the sign shown elsewhere)
    'TENTH_SEMIS',      # |value| in cents as semitones to a tenth: 1.5
    'SIGNED',           # +5, -5, 0
    'PLAIN',            # value; below 0 is 0
    'NEG_NAME',         # below 0: names[0]; else the value
    'OFF_OR_N',         # 0 or below: names[0]; else the value
    'KEY_POS',          # the JX's key follow positions, 260 per mille each: names[0] for none, 1..3
    'MS_S',             # milliseconds: 999 ms, then 1.25 s, 12.5 s; with a name, that name at 0
    'RATE',             # hundredths of a hertz as Hz to three digits; below 1 is 1
    'NAMES',            # names[value - min], held to the ends; the unit always
    'NAMES_INNER_UNIT', # as NAMES, with the unit only for values strictly between min and max
    'NONZERO',          # names[1] for any value but 0, else names[0]
    'MASK3',            # names[value & 3]: a source and its sign, or a pair (sync | ring << 1)
)

Display = namedtuple("Display", "kind unit names")


def D(kind, unit='', names=()):
    return Display(kind, unit, tuple(names))


DCO_FEET = ('32', '16', '8', '4', '2')
WAVES = ('SAW', 'PULSE', 'SQUARE', 'NOISE', 'SPIKE')
ENV_MODES = ('ENV1', 'ENV2', 'ENV1 INV', 'ENV2 INV')

DISPLAY = {
    'AMP_ATTACK_MS': D('MS_S'),
    'AMP_DECAY_MS': D('MS_S'),
    'AMP_SUSTAIN': D('TENTHS'),
    'AMP_RELEASE_MS': D('MS_S'),
    'BEND_RANGE': D('PLAIN', 'st'),
    'VOICE_MODE': D('NONZERO', '', ('POLY', 'UNISON')),
    'UNISON_DETUNE_CENTS': D('PLAIN', 'ct'),
    'OSC1_WAVE': D('NAMES', '', WAVES),
    'OSC1_PULSE_WIDTH_PCT': D('PLAIN', '%'),
    'OSC1_OCTAVE': D('NAMES', "'", DCO_FEET),
    'OSC1_FINE_CENTS': D('SIGNED', 'ct'),
    'OSC2_WAVE': D('NAMES', '', WAVES),
    'OSC2_PULSE_WIDTH_PCT': D('PLAIN', '%'),
    'OSC2_OCTAVE': D('NAMES', "'", DCO_FEET),
    'OSC2_FINE_CENTS': D('SIGNED', 'ct'),
    'OSC2_LEVEL': D('ABS_TENTHS'),
    'CHORUS_MODE': D('NAMES', '', ('OFF', 'I', 'II', 'FAST')),
    'LFO_WAVE': D('NAMES', '', ('TRI', 'SAW', 'SQUARE', 'RANDOM')),
    'LFO_RATE_CENTIHZ': D('RATE', 'Hz'),
    'LFO_DELAY_MS': D('MS_S'),
    'LFO_PITCH_CENTS': D('NEG_NAME', 'ct', ('D1',)),
    'AMP_VELOCITY': D('PLAIN', '%'),
    'LFO_PW_PCT': D('PLAIN', '%'),
    'OSC_SYNC': D('MASK3', '', ('OFF', 'SYNC 1', 'X-MOD', 'SYNC 2')),
    'OSC_XMOD': D('TENTHS', '%'),
    'FILTER_CUTOFF_CENTS': D('CUTOFF'),
    'FILTER_RESONANCE': D('TENTHS'),
    'FILTER_ENV_AMOUNT_CENTS': D('ABS_HUNDREDTHS', 'st'),
    'FILTER_KEY_TRACK': D('TENTHS', '%'),
    'FILTER_ENV_ATTACK_MS': D('MS_S'),
    'FILTER_ENV_DECAY_MS': D('MS_S'),
    'FILTER_ENV_SUSTAIN': D('TENTHS'),
    'FILTER_ENV_RELEASE_MS': D('MS_S'),
    'HIGHPASS_STEP': D('NAMES_INNER_UNIT', 'Hz', ('FLAT', '100', '240', '560', 'BOOST')),
    'OSC2_COARSE_SEMIS': D('SIGNED', 'st'),
    'OSC_MIX_ENV': D('ABS_TENTHS'),
    'OSC_MIX_ENV_SOURCE': D('MASK3', '', ENV_MODES),
    'FILTER_ENV_SOURCE': D('MASK3', '', ENV_MODES),
    'LFO_PITCH_OSC2_CENTS': D('NEG_NAME', 'ct', ('D1',)),
    'PITCH_ENV_OSC1_CENTS': D('TENTH_SEMIS', 'st'),
    'PITCH_ENV_OSC2_CENTS': D('TENTH_SEMIS', 'st'),
    'PITCH_ENV_SOURCE': D('MASK3', '', ENV_MODES),
    'PITCH_ENV_DYNA': D('OFF_OR_N', '', ('OFF',)),
    'OSC1_LEVEL': D('ABS_TENTHS'),
    'OSC_RING': D('NONE'),
    'FILTER_ENV_DYNA': D('OFF_OR_N', '', ('OFF',)),
    'AMP_KEY_FOLLOW': D('KEY_POS', '', ('OFF',)),
    'FILTER_ENV_KEY_FOLLOW': D('KEY_POS', '', ('OFF',)),
    'AMP_DYNA': D('OFF_OR_N', '', ('OFF',)),
    'OSC_MIX_ENV_DYNA': D('OFF_OR_N', '', ('OFF',)),
    'AMP_LEVEL': D('TENTHS'),
    'SETTING_GLIDE': D('NONZERO', '', ('OFF', 'ON')),
    'AMP_GATE': D('NONZERO', '', ('ENV2', 'GATE')),
    'WHEEL_PITCH_CENTS': D('PLAIN', 'ct'),
    'WHEEL_PW_PCT': D('PLAIN', '%'),
    'OSC1_COARSE_SEMIS': D('SIGNED', 'st'),
    'PORTAMENTO_MS': D('MS_S', '', ('OFF',)),
}


def jx(p):
    """Param p's JX-8P SysEx numbers: its patch field's, or () when no JX-8P byte sets it."""
    for f in PATCH_FIELDS:
        if f.param == p.enum:
            return f.jx
    return ()


def values(p, root):
    """p's (min, max, default) as integers, its C expressions evaluated over root's engine/*.h."""
    import jx8p
    names = jx8p.c_constants(root)
    return tuple(jx8p.c_int(e, names) for e in (p.min, p.max, p.default))
