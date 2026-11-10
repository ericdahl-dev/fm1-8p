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
    { FM_PARAM_FILTER_CUTOFF_CENTS, "filter_cutoff_cents", 0,
      FM_FILTER_OPEN_CENTS, FM_FILTER_OPEN_CENTS },
    { FM_PARAM_FILTER_RESONANCE, "filter_resonance_permille", 0, 1000, 0 },
    { FM_PARAM_FILTER_KEY_TRACK, "filter_key_track_permille", 0, 1000, 0 },
};

const fm_param_info *fm_param_info_get(uint16_t id)
{
    return id < FM_PARAM_COUNT ? &table[id] : 0;
}
