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
    { FM_PARAM_OSC1_WAVE,      "osc1_wave",           0, FM_WAVE_COUNT - 1, FM_WAVE_SAW },
};

const fm_param_info *fm_param_info_get(uint16_t id)
{
    return id < FM_PARAM_COUNT ? &table[id] : 0;
}
