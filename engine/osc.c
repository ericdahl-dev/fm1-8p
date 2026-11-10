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

/*
 * 2^(f / 12) in Q2.30 for a fraction of a semitone f (Q16, 0..1), by the
 * series e^y = 1 + y + y^2/2 + y^3/6 with y = f * ln 2 / 12 (y < 0.058,
 * so the error is under 0.001 cent).
 */
static uint32_t semitone_ratio_q30(uint32_t f)
{
    int64_t y = (int64_t)(((uint64_t)f * 62021760u) >> 16); /* ln 2 / 12, Q30 */
    int64_t y2 = (y * y) >> 30, y3 = (y2 * y) >> 30;
    return (uint32_t)((1 << 30) + y + y2 / 2 + (int32_t)y3 / 6);
}

uint32_t fm_osc_pitch_inc(int32_t pitch, uint32_t sample_rate)
{
    const int32_t octave = 12 << 16;
    int32_t up = 0, down = 0;
    uint32_t note, frac;
    uint64_t hz_q16, inc;
    if (sample_rate == 0) return 0; /* never divide by zero: no trap on pi32v2 */
    /* beyond +-256 semitones the result is pinned anyway; keeps sums small */
    if (pitch > (256 << 16)) pitch = 256 << 16;
    if (pitch < -(256 << 16)) pitch = -(256 << 16);
    /* fold into the table by whole octaves (exact factors of two) */
    if (pitch > FM_OSC_MAX_PITCH) {
        up = (pitch - FM_OSC_MAX_PITCH + octave - 1) / octave;
        pitch -= up * octave;
    } else if (pitch < 0) {
        down = (octave - 1 - pitch) / octave;
        pitch += down * octave;
    }
    note = (uint32_t)pitch >> 16;
    frac = (uint32_t)pitch & 0xFFFFu;
    hz_q16 = note_hz_q16(note);
    if (frac) hz_q16 = (hz_q16 * semitone_ratio_q30(frac)) >> 30;
    inc = (hz_q16 << 16) / sample_rate;
    if (up) inc = up > 16 ? FM_OSC_MAX_INC : inc << up;
    inc >>= down;
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

/* polyBLEP saw at phase t with increment dt, Q8.24. */
static int32_t saw_at(uint32_t t, uint32_t dt)
{
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
    return (int32_t)(v >> 7);
}

/*
 * Pulse of duty `width` (Q0.32 of a cycle) as the difference of two
 * polyBLEP saws offset by the width: saw(t) - saw(t + width). That is
 * +2(1 - width) for the last `width` of the cycle and -2 width for the
 * rest: peak-to-peak 2.0 and no DC, like an AC-coupled analog pulse. At
 * 50% that is +-1.0, the square; other widths are scaled by pw_gain.
 */
static int32_t pulse_at(uint32_t t, uint32_t dt, uint32_t width)
{
    return saw_at(t, dt) - saw_at(t + width, dt);
}

void fm_osc_pair_reset(fm_osc_pair *p, uint32_t seed)
{
    uint8_t i;
    for (i = 0; i < 2; i++) {
        fm_osc_reset(&p->osc[i]);
        p->wave[i] = FM_WAVE_SAW;
        p->width[i] = 0x80000000u;
        p->pw[i] = 0x80000000u;
        p->pw_gain[i] = 1 << 30;
        p->tune[i] = 0;
        p->base[i] = 0;
    }
    p->mod_oct = 0;
    p->pw_mod = 0;
    p->mix = 0;
    p->noise = seed ? seed : 0x2545F491u;
}

void fm_osc_pair_set_mix(fm_osc_pair *p, int32_t mix)
{
    if (mix < 0) mix = 0;
    if (mix > FM_OSC_MIX_FULL) mix = FM_OSC_MIX_FULL;
    p->mix = mix;
}

/*
 * Gain (Q1.30) that brings a pulse of duty `w` to a peak of 1.0, like the
 * saw: the pulse peaks at 2 max(w, 1 - w), so the gain is 1 / that. One
 * division, on events only.
 */
static int32_t pulse_gain(uint32_t w)
{
    uint64_t m = w >= 0x80000000u ? w : 0u - w; /* max(w, 1 - w), Q0.32 */
    return (int32_t)((1ull << 61) / m);
}

/*
 * The duty oscillator `i` plays: its width, kept at least two samples
 * (two increments) from either edge of the cycle. A narrower pulse falls
 * inside the polyBLEP's two-sample span, so it is mostly aliasing; at C7
 * a 1% pulse is 0.2 samples wide. Above a quarter of the sample rate
 * there is room only for a square.
 */
/*
 * pulse_gain without the divide, for widths the LFO moves every control
 * tick: 1 / (2 max(w, 1 - w)) from the seed 48/17 - 32/17 m and two
 * Newton steps, x = x (2 - m x), good to about 1e-5 (0.0001 dB).
 */
static int32_t pulse_gain_fast(uint32_t w)
{
    int64_t m = (w >= 0x80000000u ? w : 0u - w) >> 2; /* Q30, 0.5..1 */
    int64_t x = 3031741621ll - ((2021161081ll * m) >> 30); /* Q30 */
    x = (x * ((1ll << 31) - ((m * x) >> 30))) >> 30;
    x = (x * ((1ll << 31) - ((m * x) >> 30))) >> 30; /* 1 / m, Q30 */
    return (int32_t)(x >> 1);
}

/* 1 % of a cycle, Q0.32: the narrowest pulse the LFO can make. */
#define PW_MIN 42949673u

static void update_pw(fm_osc_pair *p, uint8_t i)
{
    /* limits from the unmodulated pitch: the LFO's phase never sets the
     * timbre (docs/modulation.md#pitch) */
    uint32_t inc = p->base[i], w = p->width[i], lo;
    if (p->pw_mod) {
        int64_t m = (int64_t)w + p->pw_mod;
        if (m < PW_MIN) m = PW_MIN;
        if (m > 0x100000000ll - PW_MIN) m = 0x100000000ll - PW_MIN;
        w = (uint32_t)m;
    }
    if (inc >= 0x40000000u) {
        w = 0x80000000u;
    } else if (inc) { /* no pitch yet: nothing to limit (0 - 0 would be 0) */
        lo = inc << 1;
        if (w < lo) w = lo;
        if (w > 0u - lo) w = 0u - lo;
    }
    p->pw[i] = w;
    p->pw_gain[i] = p->pw_mod ? pulse_gain_fast(w) : pulse_gain(w);
}

void fm_osc_pair_set_pw_mod(fm_osc_pair *p, int32_t offset)
{
    uint8_t i;
    if (offset == p->pw_mod) return;
    p->pw_mod = offset;
    for (i = 0; i < 2; i++)
        if (p->wave[i] == FM_WAVE_PULSE) update_pw(p, i);
}

/* Sets oscillator `i`'s increment for `pitch` plus its tuning. */
/*
 * 2^x for x in Q16.16 octaves, times `inc`, capped just under Nyquist.
 * The fraction uses a quartic fit of 2^f on [0, 1) (Q30 coefficients,
 * under 0.011 cent off); whole octaves are shifts. No division.
 */
static uint32_t mod_inc(uint32_t inc, int32_t x)
{
    static const int64_t c[4] = { 744149770, 259040010, 56213420, 14325366 };
    int32_t k;
    int64_t f, acc;
    uint64_t out;
    if (x == 0) return inc;
    k = x >> 16; /* floor: arithmetic shift */
    f = x & 0xFFFF;
    acc = c[3];
    acc = ((acc * f) >> 16) + c[2];
    acc = ((acc * f) >> 16) + c[1];
    acc = ((acc * f) >> 16) + c[0];
    acc = (1 << 30) + ((acc * f) >> 16); /* 2^f, Q2.30, 1.0..2.0 */
    out = ((uint64_t)inc * (uint64_t)acc) >> 30;
    if (k > 0) out = k > 8 ? FM_OSC_MAX_INC : out << k;
    else if (k < 0) out = k < -31 ? 0 : out >> -k;
    return out > FM_OSC_MAX_INC ? FM_OSC_MAX_INC : (uint32_t)out;
}

static void tune_osc(fm_osc_pair *p, uint8_t i, int32_t pitch,
                     uint32_t sample_rate)
{
    p->base[i] = fm_osc_pitch_inc(pitch + p->tune[i], sample_rate);
    fm_osc_set_inc(&p->osc[i], mod_inc(p->base[i], p->mod_oct));
    update_pw(p, i);
}

void fm_osc_pair_set_pitch_mod(fm_osc_pair *p, int32_t oct)
{
    if (oct == p->mod_oct) return;
    p->mod_oct = oct;
    fm_osc_set_inc(&p->osc[0], mod_inc(p->base[0], oct));
    fm_osc_set_inc(&p->osc[1], mod_inc(p->base[1], oct));
}

void fm_osc_pair_set_tune(fm_osc_pair *p, uint8_t which, int32_t cents,
                          int32_t pitch, uint32_t sample_rate)
{
    if (which >= 2) return;
    if (cents < -12800) cents = -12800; /* keeps the sums from overflowing */
    if (cents > 12800) cents = 12800;
    p->tune[which] = cents * 65536 / 100; /* Q16.16 semitones */
    tune_osc(p, which, pitch, sample_rate);
}

void fm_osc_pair_set_width(fm_osc_pair *p, uint8_t which, int32_t pct)
{
    if (which >= 2) return;
    if (pct < 1) pct = 1; /* 100% would not fit Q0.32 (it wraps to 0) */
    if (pct > 99) pct = 99;
    p->width[which] = (uint32_t)pct * 42949673u; /* 2^32 / 100, rounded */
    update_pw(p, which);
}

void fm_osc_pair_set_wave(fm_osc_pair *p, uint8_t which, uint8_t wave)
{
    if (which >= 2 || wave >= FM_WAVE_COUNT) return;
    p->wave[which] = wave;
    if (wave == FM_WAVE_PULSE) update_pw(p, which); /* any LFO offset */
}

void fm_osc_pair_set_pitch(fm_osc_pair *p, int32_t pitch, uint32_t sample_rate)
{
    tune_osc(p, 0, pitch, sample_rate);
    tune_osc(p, 1, pitch, sample_rate);
}

/*
 * White noise from a xorshift32 generator (period 2^32 - 1, three shifts
 * and three XORs a sample), as Q8.24 in -1.0..+1.0. Pitch has no effect.
 */
static int32_t noise_next(fm_osc_pair *p)
{
    uint32_t x = p->noise;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    p->noise = x;
    return (int32_t)x >> 7;
}

/* Next sample of oscillator `which`, Q8.24. */
static int32_t wave_next(fm_osc_pair *p, uint8_t which)
{
    fm_osc *o = &p->osc[which];
    uint32_t t = o->phase;
    o->phase = t + o->inc;
    switch (p->wave[which]) {
    case FM_WAVE_PULSE:
        return (int32_t)(((int64_t)pulse_at(t, o->inc, p->pw[which]) *
                          p->pw_gain[which]) >> 30);
    case FM_WAVE_SQUARE:
        return pulse_at(t, o->inc, 0x80000000u);
    case FM_WAVE_NOISE:
        return noise_next(p);
    default:
        return saw_at(t, o->inc);
    }
}

/*
 * a + (b - a) * mix: exactly a at 0 and b at full. An oscillator the mix
 * silences only advances its phase, so it costs one add.
 */
int32_t fm_osc_pair_next(fm_osc_pair *p)
{
    int32_t a, b;
    if (p->mix == 0) {
        p->osc[1].phase += p->osc[1].inc;
        return wave_next(p, 0);
    }
    if (p->mix == FM_OSC_MIX_FULL) {
        p->osc[0].phase += p->osc[0].inc;
        return wave_next(p, 1);
    }
    a = wave_next(p, 0);
    b = wave_next(p, 1);
    return a + (int32_t)(((int64_t)(b - a) * p->mix) >> 30);
}
