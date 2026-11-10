/*
 * params.c - the parameter table.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * One row per parameter: the host, tests and (later) the FM-1 UI and the
 * patch format all read ranges and defaults from here. Values are plain
 * integers in the units the name gives.
 */
#include "fm1_8p.h"

static const fm_param_info table[FM_PARAM_COUNT] = {
    /* id                       name                  min  max    default */
    { FM_PARAM_AMP_ATTACK_MS,  "amp_attack_ms",       0, 10000,    5 },
    { FM_PARAM_AMP_DECAY_MS,   "amp_decay_ms",        0, 10000,  300 },
    { FM_PARAM_AMP_SUSTAIN,    "amp_sustain_permille", 0, 1000, 700 },
    { FM_PARAM_AMP_RELEASE_MS, "amp_release_ms",      0, 10000,  300 },
    { FM_PARAM_BEND_RANGE,     "bend_range_semitones", 0,   24,    2 },
    { FM_PARAM_VOICE_MODE,     "voice_mode",          0,     1,    0 },
    { FM_PARAM_UNISON_DETUNE_CENTS, "unison_detune_cents", 0, 100, 20 },
    { FM_PARAM_OSC1_WAVE,      "osc1_wave",           0, FM_WAVE_COUNT - 1, FM_WAVE_SAW },
    { FM_PARAM_OSC1_PULSE_WIDTH_PCT, "osc1_pulse_width_pct", 1, 99, 50 },
    { FM_PARAM_OSC1_OCTAVE,    "osc1_octave",        -2,    2,    0 },
    { FM_PARAM_OSC1_FINE_CENTS, "osc1_fine_cents",  -100,  100,   0 },
    { FM_PARAM_OSC2_WAVE,      "osc2_wave",           0, FM_WAVE_COUNT - 1, FM_WAVE_SAW },
    { FM_PARAM_OSC2_PULSE_WIDTH_PCT, "osc2_pulse_width_pct", 1, 99, 50 },
    { FM_PARAM_OSC2_OCTAVE,    "osc2_octave",        -2,    2,    0 },
    { FM_PARAM_OSC2_FINE_CENTS, "osc2_fine_cents",  -100,  100,   0 },
    { FM_PARAM_OSC_MIX,        "osc_mix_permille",    0, 1000,    0 },
    { FM_PARAM_LFO_WAVE,       "lfo_wave",            0,     3,    0 },
    { FM_PARAM_LFO_RATE_CENTIHZ, "lfo_rate_centihz",  1,  3000,  500 },
    { FM_PARAM_LFO_DELAY_MS,   "lfo_delay_ms",        0,  5000,    0 },
    { FM_PARAM_LFO_PITCH_CENTS, "lfo_pitch_cents",    0,  1200,    0 },
    { FM_PARAM_AMP_VELOCITY,   "amp_velocity_pct",    0,   100,    0 },
    { FM_PARAM_LFO_PW_PCT,     "lfo_pw_pct",          0,    49,    0 },
};

const fm_param_info *fm_param_info_get(uint16_t id)
{
    return id < FM_PARAM_COUNT ? &table[id] : 0;
}
