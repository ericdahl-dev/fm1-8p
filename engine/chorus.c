/*
 * chorus.c - stereo chorus.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "chorus.h"

void fm_chorus_init(fm_chorus *c, uint32_t sample_rate)
{
    c->sample_rate = sample_rate;
    c->mode = FM_CHORUS_OFF;
}

void fm_chorus_set_mode(fm_chorus *c, uint8_t mode)
{
    c->mode = mode < FM_CHORUS_MODE_COUNT ? mode : FM_CHORUS_OFF;
}

void fm_chorus_process(fm_chorus *c, int32_t in, int32_t *left,
                       int32_t *right)
{
    (void)c;
    *left = in;
    *right = in;
}
