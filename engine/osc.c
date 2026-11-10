/*
 * osc.c - band-limited oscillator.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * The saw is a naive ramp with a polyBLEP (polynomial band-limited step)
 * correction on the two samples around each wrap, which removes most of
 * the aliasing a bare ramp produces. See docs/fixed-point.md.
 */
#include "osc.h"

/*
 * Note frequencies for MIDI notes 120..131 in Q16.16 Hz
 * (440 * 2^((n - 69) / 12) * 65536, rounded). Lower octaves are exact
 * right shifts of these. Regenerate with tools/gen_pitch_table.py.
 */
static const uint32_t top_octave_hz_q16[12] = {
    548668578u, 581294109u, 615859655u, 652480576u,
    691279090u, 732384684u, 775934544u, 822074013u,
    870957077u, 922746880u, 977616265u, 1035748353u
};

/* Note frequency in Q16.16 Hz for a whole note 0..131. */
static uint32_t note_hz_q16(uint32_t note)
{
    return top_octave_hz_q16[note % 12] >> (10 - note / 12);
}

uint32_t fm_osc_pitch_inc(int32_t pitch, uint32_t sample_rate)
{
    uint32_t note, frac, lo, hz_q16;
    uint64_t inc;
    if (sample_rate == 0) return 0; /* never divide by zero: no trap on pi32v2 */
    if (pitch < 0) pitch = 0;
    if (pitch > FM_OSC_MAX_PITCH) pitch = FM_OSC_MAX_PITCH;
    note = (uint32_t)pitch >> 16;
    frac = (uint32_t)pitch & 0xFFFFu;
    lo = note_hz_q16(note);
    hz_q16 = lo;
    if (frac) /* straight line to the next semitone (under 0.8 cent off) */
        hz_q16 += (uint32_t)(((uint64_t)(note_hz_q16(note + 1) - lo) * frac) >> 16);
    inc = ((uint64_t)hz_q16 << 16) / sample_rate;
    return inc > FM_OSC_MAX_INC ? FM_OSC_MAX_INC : (uint32_t)inc;
}

void fm_osc_reset(fm_osc *o)
{
    o->phase = 0;
    o->inc = 0;
}

void fm_osc_set_inc(fm_osc *o, uint32_t inc)
{
    o->inc = inc > FM_OSC_MAX_INC ? FM_OSC_MAX_INC : inc;
}

int32_t fm_osc_saw_next(fm_osc *o)
{
    uint32_t t = o->phase, dt = o->inc;
    /* naive saw 2t - 1 as Q0.31: -1.0 at t = 0, rising to +1.0 */
    int64_t v = (int32_t)(t ^ 0x80000000u);

    if (dt) {
        int64_t blep = 0; /* Q16 */
        if (t < dt) { /* just after the wrap: x = t/dt in [0, 1) */
            int64_t x = (int64_t)(((uint64_t)t << 16) / dt);
            blep = 2 * x - ((x * x) >> 16) - 65536;
        } else if (t > 0u - dt) { /* just before: x = (1 - t)/dt */
            int64_t x = (int64_t)(((uint64_t)(0u - t) << 16) / dt);
            blep = ((x * x) >> 16) - 2 * x + 65536;
        }
        v -= blep * 32768; /* Q16 -> Q0.31 (no left shift of negatives) */
    }
    o->phase = t + dt;
    return (int32_t)(v >> 7);
}
