/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui.h"

/* interaction.md section 3, the VCF section: page 4A (41-44) and 4B (45-48) */
static const ui_slot VCF_SLOTS[8] = {
    {41, "CUTOFF", "CUTOFF", FM_PARAM_FILTER_CUTOFF_CENTS, UI_LIVE, 100, 400},
    {42, "RESO", "RESONANCE", FM_PARAM_FILTER_RESONANCE, UI_LIVE, 10, 50},
    {43, "ENV", "ENV AMOUNT", FM_PARAM_FILTER_ENV_AMOUNT_CENTS, UI_LIVE, 100, 600},
    {44, "KEY", "KEY TRACK", FM_PARAM_FILTER_KEY_TRACK, UI_LIVE, 10, 100},
    {45, "HPF", "HIGHPASS", FM_PARAM_HIGHPASS_STEP, UI_LIVE, 1, 1},
    {46, "VEL", "VELOCITY", 0, UI_PENDING, 0, 0},
    {47, "D2 ENV", "DCO-2 ENV", 0, UI_PENDING, 0, 0},
    {48, "ENV SRC", "ENV SOURCE", 0, UI_PENDING, 0, 0},
};

const ui_slot *ui_page_slot(const ui *u, uint32_t k)
{
    (void)u;
    return k < 4u ? &VCF_SLOTS[k] : 0;
}

/* 8.1758 Hz x 2^(s / 12), s = 0..12, in mHz x 16: CUTOFF's semitones in its lowest octave */
static const uint32_t SEMI[13] = {130813, 138591, 146832, 155563, 164814, 174614, 184997,
                                  195998, 207652, 220000, 233082, 246942, 261626};

/* cutoff cents (0..14400) as mHz: the octave a shift, the semitone from SEMI, the cents between
 * two semitones a straight line (under 0.05 % off) */
static uint32_t cutoff_mhz(int32_t cents)
{
    uint32_t c = cents < 0 ? 0u : (uint32_t)cents, o = c / 1200u, r = c % 1200u, k = r / 100u;
    uint32_t v = SEMI[k] + (SEMI[k + 1u] - SEMI[k]) * (r % 100u) / 100u;
    return (v << o) >> 4;
}

static char *put_u(char *s, uint32_t v)        /* v in decimal */
{
    char d[10];
    uint32_t n = 0;
    do d[n++] = (char)('0' + v % 10u); while ((v /= 10u) != 0);
    while (n)
        *s++ = d[--n];
    return s;
}

static char *put_str(char *s, const char *t)
{
    while (*t)
        *s++ = *t++;
    return s;
}

/* milli (1000 = 1.00 of the unit, under 1 000 000) to three significant digits: 440, 55.0, 8.18 */
static char *put_3(char *s, uint32_t milli)
{
    uint32_t d;
    if (milli >= 99500u)
        return put_u(s, (milli + 500u) / 1000u);
    if (milli >= 9950u) {
        d = (milli + 50u) / 100u;
        s = put_u(s, d / 10u);
        *s++ = '.';
        return put_u(s, d % 10u);
    }
    d = (milli + 5u) / 10u;
    s = put_u(s, d / 100u);
    *s++ = '.';
    *s++ = (char)('0' + d / 10u % 10u);
    return put_u(s, d % 10u);
}

void ui_format(uint16_t id, int32_t value, char *s)
{
    if (id == FM_PARAM_FILTER_CUTOFF_CENTS) {
        uint32_t mhz = cutoff_mhz(value);
        if (value >= FM_FILTER_OPEN_CENTS)
            s = put_str(s, "OPEN");
        else if (mhz >= 999500u)
            s = put_str(put_3(s, (mhz + 500u) / 1000u), " kHz");
        else
            s = put_str(put_3(s, mhz), " Hz");
    } else if (id == FM_PARAM_FILTER_RESONANCE) {
        s = put_u(s, (uint32_t)(value < 0 ? 0 : value + 5) / 10u);
    } else if (id == FM_PARAM_FILTER_ENV_AMOUNT_CENTS) {
        uint32_t st = (uint32_t)(value < 0 ? -value : value) + 50u;
        if (st >= 100u)                         /* (0 has no sign) */
            *s++ = value < 0 ? '-' : '+';
        s = put_str(put_u(s, st / 100u), " st");
    } else if (id == FM_PARAM_FILTER_KEY_TRACK) {
        s = put_str(put_u(s, (uint32_t)(value < 0 ? 0 : value + 5) / 10u), " %");
    }
    *s = 0;
}

uint32_t ui_meter(uint16_t id, int32_t value)
{
    const fm_param_info *in = fm_param_info_get(id);
    uint32_t k, lit = 0, span, at;
    if (!in || in->max <= in->min)
        return 0;
    if (in->min == -in->max) {                  /* bipolar: each segment's middle against the value */
        int32_t v = (int32_t)UI_METER_SEGS * value;
        for (k = 0; k < UI_METER_SEGS; k++) {
            int32_t c = (2 * (int32_t)k + 1 - (int32_t)UI_METER_SEGS) * in->max;
            if ((c >= 0 && c <= v) || (c <= 0 && c >= v))
                lit |= 1u << k;
        }
        return lit;
    }
    span = (uint32_t)(in->max - in->min);
    at = value < in->min ? 0u : value > in->max ? span : (uint32_t)(value - in->min);
    return (1u << ((2u * UI_METER_SEGS * at + span) / (2u * span))) - 1u;   /* rounded */
}

void ui_init(ui *u, play *p)
{
    u->p = p;
    u->state = UI_PLAY;
    u->focus = 1;
    u->dirty = UI_D_FULL;
    u->drawn_ms = 0;
}

static void show_page(ui *u, ui_state st)
{
    u->state = st;
    u->focus = 1;                               /* knob 2, as visual.md's pages open */
    u->dirty |= UI_D_FULL;
}

static void turn(ui *u, uint32_t k, int32_t clicks)
{
    const ui_slot *s = ui_page_slot(u, k);
    if (!s || s->kind != UI_LIVE)
        return;
    play_set_param(u->p, s->id, play_get_param(u->p, s->id) +
                   (clicks >= 3 || clicks <= -3 ? s->fast : s->step) * clicks);
    u->dirty |= UI_D_COL(u->focus) | UI_D_COL(k) | UI_D_FOCUS;
    if (s->id == FM_PARAM_FILTER_CUTOFF_CENTS || s->id == FM_PARAM_FILTER_RESONANCE)
        u->dirty |= UI_D_SLOT;                  /* the curve shows only those two */
    u->focus = k;
}

void ui_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms)
{
    uint32_t id = ev & 0xFFu;
    (void)ms;
    if (ev == (UI_EV_BTN_DOWN | UI_BTN_ENV))
        show_page(u, UI_EDIT);
    else if (ev == (UI_EV_BTN_UP | UI_BTN_EDIT))
        show_page(u, u->state == UI_EDIT ? UI_PLAY : UI_EDIT);
    else if ((ev & 0xFF000000u) == UI_EV_ENC && id >= UI_ENC_KNOB1 && id <= UI_ENC_KNOB4 &&
             u->state == UI_EDIT)
        turn(u, id - UI_ENC_KNOB1, arg);
}

uint32_t ui_take(ui *u, uint32_t ms)
{
    uint32_t d = u->dirty;
    if (!d || (!(d & UI_D_FULL) && ms - u->drawn_ms < UI_FRAME_MS))
        return 0;
    u->dirty = 0;
    u->drawn_ms = ms;
    return d;
}
