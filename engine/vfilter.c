/*
 * vfilter.c - a voice's filter: cutoff composition and cadence (#72).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "vfilter.h"

void fm_vfilter_reset(fm_vfilter *vf, int32_t rate_cents)
{
    fm_filter_reset(&vf->f);
    vf->rate_cents = rate_cents;
    vf->base = FM_FILTER_OPEN_CENTS;
    vf->key_track = 0;
    vf->cutoff = INT32_MIN;
    vf->key = 0;
    vf->phase = 0;
}

void fm_vfilter_set_base(fm_vfilter *vf, int32_t linear_cents)
{
    vf->base = linear_cents;
}

void fm_vfilter_set_key_track(fm_vfilter *vf, int32_t permille)
{
    vf->key_track = (int16_t)permille; /* 0..1300 (params.c) */
}

void fm_vfilter_set_key(fm_vfilter *vf, uint8_t note)
{
    vf->key = note;
}

void fm_vfilter_clear(fm_vfilter *vf)
{
    fm_filter_clear(&vf->f);
}

void fm_vfilter_set_resonance(fm_vfilter *vf, int32_t permille)
{
    fm_filter_set_resonance(&vf->f, permille);
}

void fm_vfilter_turn_resonance(fm_vfilter *vf, int32_t permille)
{
    fm_filter_turn_resonance(&vf->f, permille);
}

int fm_vfilter_handover_snap(fm_vfilter *vf)
{
    if (!fm_filter_handover_moving(&vf->f)) return 0;
    fm_filter_set_resonance(&vf->f, vf->f.res);
    return 1;
}

void fm_vfilter_set_highpass(fm_vfilter *vf, uint8_t step)
{
    fm_filter_set_highpass(&vf->f, step, vf->rate_cents);
}

int fm_vfilter_at_rest(const fm_vfilter *vf)
{
    return fm_filter_voice_cents(vf->base, vf->key_track, vf->key, 0) == vf->cutoff &&
           !fm_filter_handover_moving(&vf->f);
}

int fm_vfilter_update(fm_vfilter *vf, int32_t mod_cents)
{
    int32_t cents = fm_filter_voice_cents(vf->base, vf->key_track, vf->key, mod_cents);
    /* the resonant top's handover, a step on while it is on its way (#168): here, inside the one call the
     * voice's loop already makes at a control point, and with the one setting of the coefficients a moved
     * cutoff makes anyway (a note that starts as its resonance glides in has both moving) */
    int moved = fm_filter_handover_advance(&vf->f);
    if (cents == vf->cutoff && !moved) return 0; /* the coefficients stand */
    vf->cutoff = cents;
    fm_filter_set_cutoff(&vf->f, cents, vf->rate_cents);
    return moved | 1;
}
