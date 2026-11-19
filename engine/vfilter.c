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

void fm_vfilter_set_highpass(fm_vfilter *vf, uint8_t step)
{
    fm_filter_set_highpass(&vf->f, step, vf->rate_cents);
}

int fm_vfilter_at_rest(const fm_vfilter *vf)
{
    return fm_filter_voice_cents(vf->base, vf->key_track, vf->key, 0) == vf->cutoff;
}

int fm_vfilter_update(fm_vfilter *vf, int32_t mod_cents, int32_t add_cents)
{
    int32_t cents = fm_filter_voice_cents(vf->base, vf->key_track, vf->key, mod_cents);
    if (add_cents) {
        cents += add_cents;
        if (cents > FM_FILTER_OPEN_CENTS) cents = FM_FILTER_OPEN_CENTS;
    }
    if (cents == vf->cutoff) return 0; /* the coefficients stand */
    vf->cutoff = cents;
    fm_filter_set_cutoff(&vf->f, cents, vf->rate_cents);
    return 1;
}
