/*
 * vfilter.h - a voice's filter: the filter with the cutoff it composes from
 * its settings, the voice's note and modulation, and the control cadence it
 * updates at (internal to the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * The voice gives it the note and the cutoff's modulation (cents, summed in
 * engine/mod.h); the voice filter decides the cutoff and when the filter is
 * a wire (#72).
 */
#ifndef FM_VFILTER_H
#define FM_VFILTER_H

#include <stdint.h>

#include "filter.h"

/* Samples between control points, where the cutoff is updated (control
 * rate: 2756 Hz at 44.1 kHz). */
#define FM_VFILTER_CTRL_SAMPLES 16

/* the fast path counts the cadence down modulo this */
typedef char fm_require_ctrl_samples_power_of_two[
    (FM_VFILTER_CTRL_SAMPLES & (FM_VFILTER_CTRL_SAMPLES - 1)) == 0 ? 1 : -1];

typedef struct {
    fm_filter f;
    int32_t rate_cents; /* fm_filter_rate_cents(sample rate) */
    int32_t base;       /* the cutoff setting, linear cents (fm_filter_linear_cents) */
    int32_t cutoff;     /* cents the filter is set to (INT32_MIN: none yet) */
    int16_t key_track;  /* per mille (0..1300, params.c); 16 bits so the two
                         * bytes below pack beside it, no padding */
    uint8_t key;        /* the note the cutoff tracks */
    uint8_t phase;      /* samples until the next control point (0: this one) */
} fm_vfilter;

/* A wire (open, no resonance, the highpass off) at `rate_cents`
 * (fm_filter_rate_cents of the sample rate). */
void fm_vfilter_reset(fm_vfilter *vf, int32_t rate_cents);

/* The cutoff setting, in linear cents (fm_filter_linear_cents of the
 * setting's cents): taken at the next update. */
void fm_vfilter_set_base(fm_vfilter *vf, int32_t linear_cents);

/* Key tracking, per mille (fm_filter_voice_cents): taken at the next
 * update. */
void fm_vfilter_set_key_track(fm_vfilter *vf, int32_t permille);

/* The note the cutoff tracks (a note start, or a legato change): taken at
 * the next update. */
void fm_vfilter_set_key(fm_vfilter *vf, uint8_t note);

/* Drops the filter's state (all sound off: nothing rings into the next
 * note), keeping the settings and the cutoff. */
void fm_vfilter_clear(fm_vfilter *vf);

/* Resonance, per mille: at once (fm_filter_set_resonance), for a voice that isn't sounding. */
void fm_vfilter_set_resonance(fm_vfilter *vf, int32_t permille);

/* Resonance on a sounding voice (fm_filter_turn_resonance): at once but for the resonant top's handover
 * (#168), which fm_vfilter_handover_step then moves, a step a control point. */
void fm_vfilter_turn_resonance(fm_vfilter *vf, int32_t permille);

/* The handover where its resonance says, at once (a note starts on a voice that wasn't sounding). Returns 1 if
 * the filter was set. */
int fm_vfilter_handover_snap(fm_vfilter *vf);

/* The highpass position 0..4 (fm_filter_set_highpass): at once. */
void fm_vfilter_set_highpass(fm_vfilter *vf, uint8_t step);

/*
 * Composes the cutoff from the settings, the key and `mod_cents` (the
 * voice's modulation of the cutoff, engine/mod.h) and sets the filter to
 * it if it moved. The one place the cutoff is composed. It also moves the
 * resonant top's handover a step on while that is on its way (#168,
 * fm_filter_handover_step). Returns 0 if the filter's coefficients stand,
 * else 1 (the filter was set), plus 2 while the handover has further to
 * go: the voice then keeps its control points coming.
 */
int fm_vfilter_update(fm_vfilter *vf, int32_t mod_cents);

/* Set to what its settings and key compose with no modulation: nothing
 * left for an update to do once the voice's modulation is at rest. For
 * checks: it composes the cutoff (fm_voice_busy_derived). */
int fm_vfilter_at_rest(const fm_vfilter *vf);

/* The cutoff the filter is set to, in cents (as composed, before the
 * filter clamps it). */
static inline int32_t fm_vfilter_cutoff(const fm_vfilter *vf)
{
    return vf->cutoff;
}

/* Called once a sample, before the sample runs: 1 if it is a control
 * point (the voice updates the cutoff and its other control-rate work). */
static inline int fm_vfilter_due(fm_vfilter *vf)
{
    if (vf->phase == 0) {
        vf->phase = FM_VFILTER_CTRL_SAMPLES - 1;
        return 1;
    }
    vf->phase--;
    return 0;
}

/* The next sample is a control point (a note starts: its cutoff from its
 * first sample). */
static inline void fm_vfilter_due_next(fm_vfilter *vf)
{
    vf->phase = 0;
}

/* `n` samples passed a wire without running it (the voice's fast path),
 * `last` the last of them: the wire keeps it, as fm_filter_run would (a
 * filter that closes carries on from it), and the cadence moves on as n
 * calls of fm_vfilter_due would. */
static inline void fm_vfilter_pass(fm_vfilter *vf, int32_t last, uint32_t n)
{
    if (n) fm_filter_pass_wire(&vf->f, last);
    vf->phase = (uint8_t)((vf->phase - n) & (FM_VFILTER_CTRL_SAMPLES - 1));
}

/* The note the cutoff tracks. */
static inline uint8_t fm_vfilter_key(const fm_vfilter *vf)
{
    return vf->key;
}

/* Samples until the next control point (0: the next sample is one). */
static inline uint8_t fm_vfilter_phase(const fm_vfilter *vf)
{
    return vf->phase;
}

/* The filter itself, to read (its coefficients and state: filter.h). */
static inline const fm_filter *fm_vfilter_filter(const fm_vfilter *vf)
{
    return &vf->f;
}

/* Nonzero while the resonant top's handover is on its way (fm_filter_handover_moving). */
static inline int fm_vfilter_handover_moving(const fm_vfilter *vf)
{
    return fm_filter_handover_moving(&vf->f);
}

/* Nonzero while the filter is a wire. */
static inline int fm_vfilter_is_wire(const fm_vfilter *vf)
{
    return fm_filter_is_wire(&vf->f);
}

/* One sample through the filter (Q8.24). */
static inline int32_t fm_vfilter_run(fm_vfilter *vf, int32_t in)
{
    return fm_filter_run(&vf->f, in);
}

#endif /* FM_VFILTER_H */
