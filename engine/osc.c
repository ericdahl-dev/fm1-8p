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

/* Keeps a rare path out of line so the per-sample path around it stays small. */
#if defined(__GNUC__) || defined(__clang__)
#define FM_NOINLINE __attribute__((noinline))
#else
#define FM_NOINLINE
#endif

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
 * `dt0` is the first saw's polyBLEP increment (0 right after a sync
 * restart, which brings its own correction), `dt` the second's. Kept out of
 * line: inlined at -Os, it makes wave_next save four more registers on
 * every call, the saw's included (+1.7 % at 8 voices in fm1-emu).
 */
static FM_NOINLINE int32_t pulse_at(uint32_t t, uint32_t dt0, uint32_t dt, uint32_t width)
{
    return saw_at(t, dt0) - saw_at(t + width, dt);
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
    p->level[0] = FM_OSC_LEVEL_FULL;
    p->level[1] = 0;
    p->sync = 0;
    p->sync_after = 0;
    p->sync_pend = 0;
    p->xmod = 0;
    p->special = 0;
    p->only1 = 1;
    p->xmod_inc = 0;
    p->recip0 = 0;
    p->recip0_sh = 0;
    p->noise = seed ? seed : 0x2545F491u;
}

/* osc1 at full alone with nothing special: fm_osc_pair_next's fast path */
static void update_only1(fm_osc_pair *p)
{
    p->only1 = (uint8_t)(p->level[0] == FM_OSC_LEVEL_FULL && p->level[1] == 0 &&
                         !p->special);
}

void fm_osc_pair_set_level(fm_osc_pair *p, uint8_t which, int32_t level)
{
    if (which > 1) return;
    if (level < 0) level = 0;
    if (level > FM_OSC_LEVEL_FULL) level = FM_OSC_LEVEL_FULL;
    p->level[which] = level;
    update_only1(p);
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
 * pulse_gain without the divide, for widths the LFO moves every control
 * tick: 1 / (2 max(w, 1 - w)) from the seed 48/17 - 32/17 m and two
 * Newton steps, x = x (2 - m x), good to about 1e-5 (0.0001 dB).
 */
/* Toward 1 / m for m in 0.5..1 (Q30): the seed, then Newton steps (Q30). */
static int64_t recip_seed(int64_t m)
{
    return 3031741621ll - ((2021161081ll * m) >> 30);
}

static int64_t recip_step(int64_t m, int64_t x)
{
    return (x * ((1ll << 31) - ((m * x) >> 30))) >> 30;
}

static int32_t pulse_gain_fast(uint32_t w)
{
    int64_t m = (w >= 0x80000000u ? w : 0u - w) >> 2; /* Q30, 0.5..1 */
    return (int32_t)(recip_step(m, recip_step(m, recip_seed(m))) >> 1);
}

/* 1 % of a cycle, Q0.32: the narrowest pulse the LFO can make. */
#define PW_MIN 42949673u

/*
 * The duty oscillator `i` plays: its width, kept at least two samples
 * (two increments) from either edge of the cycle. A narrower pulse falls
 * inside the polyBLEP's two-sample span, so it is mostly aliasing; at C7
 * a 1% pulse is 0.2 samples wide. Above a quarter of the sample rate
 * there is room only for a square.
 */
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

/*
 * What sync and cross-mod need from the increments, on every change of
 * them while either is on (events and the LFO's control ticks; no
 * division). recip0 / 2^recip0_sh is 1 / osc1's increment: osc1's increment
 * shifted up to 2^31..2^32 is m, and 2^62 / m (2^30..2^31) comes from three
 * Newton steps, good to the Q30 rounding. xmod_inc is osc2's increment
 * times the cross-mod depth.
 */
static void update_derived(fm_osc_pair *p)
{
    uint32_t m = p->osc[0].inc;
    uint8_t s = 0;
    if (!p->special) return;
    p->xmod_inc = (uint32_t)(((uint64_t)p->osc[1].inc * (uint32_t)p->xmod) >> 30);
    if (m == 0) {
        p->recip0 = 0;
        p->recip0_sh = 0;
        return;
    }
    while (!(m & 0x80000000u)) {
        m <<= 1;
        s++;
    }
    p->recip0 = (uint32_t)recip_step(m >> 2, recip_step(m >> 2, recip_step(m >> 2, recip_seed(m >> 2))));
    p->recip0_sh = (uint8_t)(46 - s); /* t / inc0 in Q16 = t * recip0 >> recip0_sh */
}

void fm_osc_pair_set_pw_mod(fm_osc_pair *p, int32_t offset)
{
    uint8_t i;
    if (offset == p->pw_mod) return;
    p->pw_mod = offset;
    for (i = 0; i < 2; i++)
        if (p->wave[i] == FM_WAVE_PULSE) update_pw(p, i);
}

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

/* Sets oscillator `i`'s increment for `pitch` plus its tuning, and the LFO
 * offset on top. */
static void tune_osc(fm_osc_pair *p, uint8_t i, int32_t pitch,
                     uint32_t sample_rate)
{
    p->base[i] = fm_osc_pitch_inc(pitch + p->tune[i], sample_rate);
    fm_osc_set_inc(&p->osc[i], mod_inc(p->base[i], p->mod_oct));
    update_pw(p, i);
}

/* sync or cross-mod on: fm_osc_pair_next takes the slower path */
static void update_special(fm_osc_pair *p)
{
    p->special = (uint8_t)(p->sync || p->xmod);
    p->sync_pend = 0;
    update_only1(p);
    update_derived(p);
}

void fm_osc_pair_set_sync(fm_osc_pair *p, uint8_t on)
{
    p->sync = on ? 1 : 0;
    update_special(p);
}

void fm_osc_pair_set_xmod(fm_osc_pair *p, int32_t depth)
{
    if (depth < 0) depth = 0;
    if (depth > FM_OSC_LEVEL_FULL) depth = FM_OSC_LEVEL_FULL;
    p->xmod = depth;
    update_special(p);
}

void fm_osc_pair_set_pitch_mod(fm_osc_pair *p, int32_t oct)
{
    if (oct == p->mod_oct) return;
    p->mod_oct = oct;
    fm_osc_set_inc(&p->osc[0], mod_inc(p->base[0], oct));
    fm_osc_set_inc(&p->osc[1], mod_inc(p->base[1], oct));
    update_derived(p);
}

void fm_osc_pair_set_tune(fm_osc_pair *p, uint8_t which, int32_t cents,
                          int32_t pitch, uint32_t sample_rate)
{
    if (which >= 2) return;
    if (cents < -12800) cents = -12800; /* keeps the sums from overflowing */
    if (cents > 12800) cents = 12800;
    p->tune[which] = cents * 65536 / 100; /* Q16.16 semitones */
    tune_osc(p, which, pitch, sample_rate);
    update_derived(p);
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
    update_derived(p);
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

/* Oscillator `which`'s wave at phase t without band-limiting, Q8.24 (noise: 0). */
static int32_t naive_at(const fm_osc_pair *p, uint8_t which, uint32_t t)
{
    int32_t saw = (int32_t)(t ^ 0x80000000u) >> 7;
    uint32_t w = p->wave[which] == FM_WAVE_PULSE ? p->pw[which] : 0x80000000u;
    int32_t pulse = saw - ((int32_t)((t + w) ^ 0x80000000u) >> 7);
    switch (p->wave[which]) {
    case FM_WAVE_PULSE:
        return (int32_t)(((int64_t)pulse * p->pw_gain[which]) >> 30);
    case FM_WAVE_SQUARE:
        return pulse;
    case FM_WAVE_NOISE:
        return 0;
    default:
        return saw;
    }
}

static int32_t wave_at(fm_osc_pair *p, uint8_t which);
static int32_t wave_at_dt(fm_osc_pair *p, uint8_t which, uint32_t dt0);

/*
 * osc2's sample, with the polyBLEP of a sync reset in the last sample.
 * After a reset osc2's phase is under one increment, so its own polyBLEP
 * already treats the reset as a full wrap (a step of -2.0); sync_after
 * replaces that with the real jump.
 */
static int32_t osc2_at(fm_osc_pair *p)
{
    int32_t v;
    if (!p->sync_pend)
        return wave_at(p, 1);
    /* the first sample after a restart: no polyBLEP of osc2's own for the
     * restart (dt 0 for the saw that starts there), the restart's instead */
    v = wave_at_dt(p, 1, 0);
    p->sync_pend = 0;
    return v + p->sync_after;
}

/*
 * Oscillator `which`'s sample at its current phase, Q8.24. `dt0` is the
 * increment the polyBLEP of the saw that starts at the phase uses (0: none);
 * a pulse's second saw always uses the oscillator's increment.
 */
static int32_t wave_at_dt(fm_osc_pair *p, uint8_t which, uint32_t dt0)
{
    fm_osc *o = &p->osc[which];
    uint32_t t = o->phase;
    switch (p->wave[which]) {
    case FM_WAVE_PULSE:
        return (int32_t)(((int64_t)pulse_at(t, dt0, o->inc, p->pw[which]) *
                          p->pw_gain[which]) >> 30);
    case FM_WAVE_SQUARE:
        return pulse_at(t, dt0, o->inc, 0x80000000u);
    case FM_WAVE_NOISE:
        return noise_next(p);
    default:
        return saw_at(t, dt0);
    }
}

/* Oscillator `which`'s sample at its current phase, Q8.24. */
static int32_t wave_at(fm_osc_pair *p, uint8_t which)
{
    return wave_at_dt(p, which, p->osc[which].inc);
}

/*
 * How far the next sample is past an osc1 wrap that left osc1 at phase t:
 * t / osc1's increment in Q16 (0..1), from the reciprocal (no division).
 */
static uint64_t restart_x(const fm_osc_pair *p, uint32_t t)
{
    return ((uint64_t)t * p->recip0) >> p->recip0_sh;
}

/* A polyBLEP term of osc2's first saw (Q8.24 at unit gain) at osc2's gain. */
static int32_t osc2_gain(const fm_osc_pair *p, int64_t v)
{
    if (p->wave[1] == FM_WAVE_NOISE) return 0;
    if (p->wave[1] == FM_WAVE_PULSE) v = (v * p->pw_gain[1]) >> 30;
    return (int32_t)v;
}

/*
 * The polyBLEP osc2's first saw put on the sample just computed at phase
 * `t` for a wrap due later in that sample, negated: a restart came first,
 * so that wrap never happens. The same sum saw_at made (one division, as
 * there), times the wave's gain.
 */
static int32_t unwrap_blep(const fm_osc_pair *p, uint32_t t, uint32_t dt)
{
    int64_t x = (int64_t)(((uint64_t)(0u - t) << 16) / dt);
    return osc2_gain(p, (((x * x) >> 16) - 2 * x + 65536) * 256); /* Q16 -> Q8.24 */
}

/*
 * The after side of the polyBLEP of osc2's own wrap, `t` (its phase now)
 * past it: what saw_at would have added on the next sample, had a restart
 * not followed the wrap within the sample (one division, as there).
 */
static int32_t wrap_after_blep(const fm_osc_pair *p, uint32_t t, uint32_t dt)
{
    int64_t x = (int64_t)(((uint64_t)t << 16) / dt);
    return osc2_gain(p, -(2 * x - ((x * x) >> 16) - 65536) * 256);
}

/*
 * osc1 wrapped with sync on: restart osc2 at the same point of its own
 * cycle (osc1 is `t`, its new phase, into the sample, so osc2 is
 * t * inc2 / inc1 into its cycle) and set up the polyBLEP of osc2's jump.
 * Returns the correction for the sample just computed. Once per osc1
 * cycle, so kept out of the per-sample path.
 */
static FM_NOINLINE int32_t sync_reset(fm_osc_pair *p, uint32_t t)
{
    /* osc2's increment this sample (bent, with cross-mod) for both ends */
    uint32_t inc2 = p->osc[1].inc, cont = p->osc[1].phase + inc2, now;
    int64_t x = (int64_t)restart_x(p, t);
    int32_t half, before;
    now = (uint32_t)(((uint64_t)x * inc2) >> 16);
    /* osc2 was at cont - now when osc1 wrapped, and jumps to 0 there */
    half = (naive_at(p, 1, 0) - naive_at(p, 1, cont - now)) / 2;
    /* the sample just computed, (1 - x) before the step: x^2 of the polyBLEP;
     * the next one, x after it: 2x - x^2 - 1 */
    before = (int32_t)((half * ((x * x) >> 16)) >> 16);
    p->sync_after = (int32_t)((half * (2 * x - ((x * x) >> 16) - 65536)) >> 16);
    p->sync_pend = 1;
    if (cont < p->osc[1].phase) { /* osc2's own wrap was due in this sample */
        if (cont < now) /* the restart came first: that wrap never happens */
            before += unwrap_blep(p, p->osc[1].phase, inc2);
        else /* it came first: its polyBLEP's after side, `cont` (where osc2
              * would be now without the restart) past it */
            p->sync_after += wrap_after_blep(p, cont, inc2);
    }
    p->osc[1].phase = now;
    p->osc[0].phase = t;
    return before;
}

/* Moves both oscillators on one sample; returns sync's correction for it (0 without). */
static int32_t advance(fm_osc_pair *p)
{
    uint32_t t = p->osc[0].phase + p->osc[0].inc;
    if (p->sync && t < p->osc[0].phase)
        return sync_reset(p, t);
    p->osc[1].phase += p->osc[1].inc;
    p->osc[0].phase = t;
    return 0;
}

/* x times a level (Q1.30); full passes x through untouched */
static inline int32_t gain(int32_t x, int32_t level)
{
    return level == FM_OSC_LEVEL_FULL ? x : (int32_t)(((int64_t)x * level) >> 30);
}

/*
 * One sample with sync or cross-mod on. Cross-mod runs osc2 this sample
 * at its increment times (1 + depth * osc1), 0..2x and capped under
 * Nyquist, so its polyBLEPs follow the bent pitch; the set increment is
 * put back after. With osc2 at level 0 neither changes the sound, but
 * osc2 still moves with sync so its phase is right when its level changes.
 */
static FM_NOINLINE int32_t next_special(fm_osc_pair *p)
{
    uint32_t inc = p->osc[1].inc;
    int32_t a = 0, b;
    if (p->level[0] || p->xmod)
        a = wave_at(p, 0);
    if (p->level[1] == 0) { /* osc2 unheard: with sync, only its phase restarts */
        uint32_t t = p->osc[0].phase + p->osc[0].inc;
        if (p->sync && t < p->osc[0].phase)
            p->osc[1].phase = (uint32_t)((restart_x(p, t) * p->osc[1].inc) >> 16);
        else
            p->osc[1].phase += p->osc[1].inc;
        p->osc[0].phase = t;
        p->sync_pend = 0;
        return gain(a, p->level[0]);
    }
    if (p->xmod) {
        int64_t now = (int64_t)inc + (((int64_t)p->xmod_inc * a) >> 24);
        p->osc[1].inc = now < 0 ? 0u : now > FM_OSC_MAX_INC ? FM_OSC_MAX_INC : (uint32_t)now;
    }
    b = osc2_at(p);
    b += advance(p);
    p->osc[1].inc = inc;
    return gain(a, p->level[0]) + gain(b, p->level[1]);
}

/* Oscillator `which`'s sample, then its phase moves on (no sync). */
static int32_t wave_next(fm_osc_pair *p, uint8_t which)
{
    fm_osc *o = &p->osc[which];
    uint32_t t = o->phase;
    o->phase = t + o->inc;
    switch (p->wave[which]) {
    case FM_WAVE_PULSE:
        return (int32_t)(((int64_t)pulse_at(t, o->inc, o->inc, p->pw[which]) *
                          p->pw_gain[which]) >> 30);
    case FM_WAVE_SQUARE:
        return pulse_at(t, o->inc, o->inc, 0x80000000u);
    case FM_WAVE_NOISE:
        return noise_next(p);
    default:
        return saw_at(t, o->inc);
    }
}

/*
 * a * level 0 + b * level 1. An oscillator at level 0 only advances its
 * phase, so it costs one add. osc1 alone
 * without sync or cross-mod, the most common setting, is the first test,
 * so it costs no more than before those existed.
 */
int32_t fm_osc_pair_next(fm_osc_pair *p)
{
    int32_t a, b;
    if (p->only1) {
        p->osc[1].phase += p->osc[1].inc;
        return wave_next(p, 0);
    }
    if (p->special)
        return next_special(p);
    if (p->level[0] == 0) {
        p->osc[0].phase += p->osc[0].inc;
        return gain(wave_next(p, 1), p->level[1]);
    }
    if (p->level[1] == 0) {
        p->osc[1].phase += p->osc[1].inc;
        return gain(wave_next(p, 0), p->level[0]);
    }
    a = wave_next(p, 0);
    b = wave_next(p, 1);
    return gain(a, p->level[0]) + gain(b, p->level[1]);
}

/*
 * Oscillator `which`'s next `n` samples (no sync) times `level`, its wave
 * chosen once: into out[], or with `add` added to what out[] holds (osc1),
 * as fm_osc_pair_next sums them. Inlined into fm_osc_pair_render with
 * `add` known and `level` often known to be full, so each loop is
 * one wave's own, the pulse's two saws inline (pulse_at stays out of
 * line for the per-sample paths, where inlining it costs 8 voices).
 */
static inline void wave_run(fm_osc_pair *p, uint8_t which, int32_t *out,
                            uint32_t n, int32_t level, int add)
{
    fm_osc *o = &p->osc[which];
    uint32_t t = o->phase, dt = o->inc, i;
    int32_t b;
    switch (p->wave[which]) {
    case FM_WAVE_PULSE: {
        uint32_t w = p->pw[which];
        int32_t g = p->pw_gain[which];
        for (i = 0; i < n; i++, t += dt) {
            b = (int32_t)(((int64_t)(saw_at(t, dt) - saw_at(t + w, dt)) * g) >> 30);
            out[i] = add ? out[i] + gain(b, level) : gain(b, level);
        }
        break;
    }
    case FM_WAVE_SQUARE:
        for (i = 0; i < n; i++, t += dt) {
            b = saw_at(t, dt) - saw_at(t + 0x80000000u, dt);
            out[i] = add ? out[i] + gain(b, level) : gain(b, level);
        }
        break;
    case FM_WAVE_NOISE:
        for (i = 0; i < n; i++, t += dt) {
            b = noise_next(p);
            out[i] = add ? out[i] + gain(b, level) : gain(b, level);
        }
        break;
    default:
        for (i = 0; i < n; i++, t += dt) {
            b = saw_at(t, dt);
            out[i] = add ? out[i] + gain(b, level) : gain(b, level);
        }
        break;
    }
    o->phase = t;
}

/*
 * The path is chosen once for the chunk: osc1 alone, osc2 alone, both
 * summed (osc1 into out[], then osc2 added), or sync / cross-mod a
 * sample at a time. An unheard oscillator only moves its phase, by n
 * increments at once. Two noises blend a sample at a time, as they share
 * one generator and fm_osc_pair_next draws them in turn.
 */
void fm_osc_pair_render(fm_osc_pair *p, int32_t *out, uint32_t n)
{
    uint32_t i;
    if (p->special) {
        for (i = 0; i < n; i++) out[i] = next_special(p);
        return;
    }
    if (p->only1) {
        p->osc[1].phase += n * p->osc[1].inc;
        wave_run(p, 0, out, n, FM_OSC_LEVEL_FULL, 0);
        return;
    }
    if (p->level[0] == 0) {
        p->osc[0].phase += n * p->osc[0].inc;
        wave_run(p, 1, out, n, p->level[1], 0);
        return;
    }
    if (p->level[1] == 0) {
        p->osc[1].phase += n * p->osc[1].inc;
        wave_run(p, 0, out, n, p->level[0], 0);
        return;
    }
    if (p->wave[0] == FM_WAVE_NOISE && p->wave[1] == FM_WAVE_NOISE) {
        for (i = 0; i < n; i++) out[i] = fm_osc_pair_next(p);
        return;
    }
    wave_run(p, 0, out, n, p->level[0], 0);
    wave_run(p, 1, out, n, p->level[1], 1);
}
