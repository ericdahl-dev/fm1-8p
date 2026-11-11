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

/* ui_value; returns the end of num (the loop to find it would be strlen, which the firmware lacks) */
static char *value_parts(uint16_t id, int32_t value, char *num, char *unit)
{
    const char *u = "";
    if (id == FM_PARAM_FILTER_CUTOFF_CENTS) {
        uint32_t mhz = cutoff_mhz(value);
        if (value >= FM_FILTER_OPEN_CENTS)
            num = put_str(num, "OPEN");
        else if (mhz >= 999500u)
            num = put_3(num, (mhz + 500u) / 1000u), u = "kHz";
        else
            num = put_3(num, mhz), u = "Hz";
    } else if (id == FM_PARAM_FILTER_RESONANCE) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value + 5) / 10u);
    } else if (id == FM_PARAM_FILTER_ENV_AMOUNT_CENTS) {
        uint32_t st = (uint32_t)(value < 0 ? -value : value) + 50u;
        if (st >= 100u)                         /* (0 has no sign) */
            *num++ = value < 0 ? '-' : '+';
        num = put_u(num, st / 100u), u = "st";
    } else if (id == FM_PARAM_FILTER_KEY_TRACK) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value + 5) / 10u), u = "%";
    }
    *num = 0;
    *put_str(unit, u) = 0;
    return num;
}

void ui_value(uint16_t id, int32_t value, char *num, char *unit)
{
    (void)value_parts(id, value, num, unit);
}

void ui_format(uint16_t id, int32_t value, char *s)
{
    char unit[UI_VALUE_LEN];
    s = value_parts(id, value, s, unit);
    if (*unit) {
        *s++ = ' ';
        *put_str(s, unit) = 0;
    }
}

void ui_strip(uint32_t i, int32_t *x, int32_t *w)
{
    const uint32_t span = 230u - (UI_PAGES - 1u) * 2u, sw = span / UI_PAGES, extra = span % UI_PAGES;
    *x = (int32_t)(5u + i * (sw + 2u) + (i < extra ? i : extra));
    *w = (int32_t)(sw + (i < extra ? 1u : 0u));
}

uint32_t ui_seg7(char c)
{
    static const uint8_t DIGIT[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    if (c >= '0' && c <= '9')
        return DIGIT[c - '0'];
    return c == '-' ? 0x40u : 0u;
}

void ui_column(uint16_t id, int32_t value, char *num, char *unit)
{
    ui_value(id, value, num, unit);
    if (unit[0] == 'k')                         /* kHz: k */
        unit[1] = 0;
    else if (unit[0] == 'H')                    /* Hz: no room after three digits */
        unit[0] = 0;
}

/* 2^(k / 16) in Q16 and log2(1 + k / 16) in Q12, k = 0..16: the curve's exp2 and log2, a straight
 * line between entries */
static const uint32_t EXP2[17] = {65536, 68438, 71468, 74632, 77936, 81386, 84990, 88752, 92682,
                                  96785, 101070, 105545, 110218, 115098, 120194, 125515, 131072};
static const uint16_t LOG2[17] = {0, 358, 696, 1016, 1319, 1607, 1882, 2145, 2396, 2637, 2869,
                                  3092, 3307, 3514, 3715, 3908, 4096};

/* 2^(e / 4096) in Q8 (0 when below 1/256) */
static uint32_t exp2_q8(int32_t e)
{
    int32_t ip = e >> 12;                       /* (floor) */
    uint32_t f = (uint32_t)e & 4095u, k = f >> 8, t;
    t = EXP2[k] + (EXP2[k + 1u] - EXP2[k]) * (f & 255u) / 256u;     /* Q16 */
    ip -= 8;                                    /* to Q8 */
    if (ip <= -24)
        return 0;
    return ip < 0 ? t >> -ip : t << ip;
}

/* log2(v) in Q12, v > 0 */
static int32_t log2_q12(uint32_t v)
{
    int32_t p = 31;
    uint32_t m, k;
    while (!(v >> p))
        p--;
    m = p >= 12 ? v >> (p - 12) : v << (12 - p);   /* 4096..8191: 1.m */
    m -= 4096u;
    k = m >> 8;
    return p * 4096 + (int32_t)(LOG2[k] + (LOG2[k + 1u] - LOG2[k]) * (m & 255u) / 256u);
}

static uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* render.py: fc = 10^(-2 + 2.4 cut), x = 10^(-2 + 2.6 i / (n - 1)) / fc, q = 0.7 + 6 res^1.5,
 * |H|^2 = 1 / ((1 - x^2)^2 + (x / q)^2); what doesn't depend on the column, once per curve */
void ui_curve_begin(ui_curve *c, int32_t cutoff_cents, int32_t reso_permille, uint32_t n)
{
    uint32_t r = (uint32_t)(reso_permille < 0 ? 0 : reso_permille) * 4096u / 1000u;
    uint32_t q = 2867u + 6u * (r * isqrt(r) / 64u);
    c->q2 = q * q >> 16;                        /* q^2, Q8 */
    c->cut = cutoff_cents * 9830 / 14400;       /* 2.4 cut decades, Q12 */
    c->n1 = n > 1u ? n - 1u : 1u;
}

int32_t ui_curve_db(const ui_curve *c, uint32_t i)
{
    int32_t dec = (int32_t)(10650u * i / c->n1) - c->cut;     /* decades, Q12 */
    int32_t lx = dec * 13607 / 4096, a;                       /* log2(x), Q12 */
    uint32_t x2, d;
    if (lx > 3 * 4096)                          /* x > 8: x^4 is all of it, -40 log10 x */
        return -(lx * 1204 / 40960);
    x2 = exp2_q8(2 * lx);                       /* x^2, Q8 */
    a = 256 - (int32_t)x2;
    d = (uint32_t)(a * a) + (x2 << 16) / c->q2; /* Q16 */
    return -((log2_q12(d) - 16 * 4096) * 301 / 40960);   /* -100 log10 d, in tenths of a dB */
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
    uint32_t i;
    u->p = p;
    u->state = UI_PLAY;
    u->focus = 1;
    u->dirty = UI_D_FULL;
    u->drawn_ms = 0;
    for (i = 0; i < UI_BUTTONS; i++)
        u->down_ms[i] = 0;
    u->shown_note = play_last_note(p);
    u->shown_oct = p->oct;
    u->banner = "";
    u->banner_ms = 0;
    u->warned = 0;
    u->warned_ms = 0;
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

static void show_banner(ui *u, const char *s, uint32_t ms)
{
    u->banner = s;
    u->banner_ms = ms;
    u->dirty |= UI_D_FOOT;
}

/* PRESETS: the patch clicks away, wrapping */
static void browse(ui *u, int32_t clicks, uint32_t ms)
{
    int32_t i, way = clicks > 0 ? 1 : -1;
    if (play_edited(u->p) && !(u->warned == way && ms - u->warned_ms < UI_DISCARD_MS)) {
        show_banner(u, "EDITED: TURN AGAIN TO DISCARD", ms);   /* the first event loads nothing */
        u->warned = way;
        u->warned_ms = ms;
        return;
    }
    u->warned = 0;
    if (clicks >= 3 || clicks <= -3)          /* a fast turn: 4 */
        clicks = clicks > 0 ? 4 : -4;
    i = (int32_t)(play_patch(u->p) - PATCHES) + clicks % (int32_t)PATCH_COUNT;
    play_load_patch(u->p, (uint32_t)(i + (int32_t)PATCH_COUNT) % PATCH_COUNT);
    u->dirty |= (UI_D_ALL & ~UI_D_FULL) | UI_D_FOOT;   /* the page and focus stay; its values change */
}

void ui_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms)
{
    uint32_t id = ev & 0xFFu;
    uint32_t tap = (ev & 0xFF000000u) == UI_EV_BTN_UP && id < UI_BUTTONS &&
                   ms - u->down_ms[id] < UI_TAP_MS;   /* a release: was it a tap? */
    if ((ev & 0xFF000000u) == UI_EV_BTN_DOWN && id < UI_BUTTONS)
        u->down_ms[id] = ms;
    if (ev != (UI_EV_ENC | UI_ENC_PRESETS))
        u->warned = 0;                          /* anything else cancels the warning (decision Q4) */
    if (ev == (UI_EV_BTN_DOWN | UI_BTN_ENV) && u->state != UI_EDIT)   /* (on 4A: 4B, with #47) */
        show_page(u, UI_EDIT);
    else if (ev == (UI_EV_BTN_UP | UI_BTN_EDIT) && tap)
        show_page(u, u->state == UI_EDIT ? UI_PLAY : UI_EDIT);   /* (a hold: compare, not yet) */
    else if (ev == (UI_EV_BTN_UP | UI_BTN_HOME) && tap && u->state != UI_PLAY)
        show_page(u, UI_PLAY);                  /* (a hold: the host's picker, which sends the UP) */
    else if ((ev & 0xFF000000u) == UI_EV_ENC && id >= UI_ENC_KNOB1 && id <= UI_ENC_KNOB4 &&
             u->state == UI_EDIT)
        turn(u, id - UI_ENC_KNOB1, arg);
    else if (ev == (UI_EV_ENC | UI_ENC_PRESETS))
        browse(u, arg, ms);
}

uint32_t ui_take(ui *u, uint32_t ms)
{
    uint32_t d;
    if (u->state == UI_PLAY && play_last_note(u->p) != u->shown_note) {
        u->shown_note = play_last_note(u->p);
        u->dirty |= UI_D_NOTE;
    }
    if (*u->banner && ms - u->banner_ms >= UI_BANNER_MS) {
        u->banner = "";
        u->dirty |= UI_D_FOOT;
    }
    if (u->p->oct != u->shown_oct) {
        u->shown_oct = u->p->oct;
        u->dirty |= UI_D_HEAD;
    }
    d = u->dirty;
    if (!d || (!(d & UI_D_FULL) && ms - u->drawn_ms < UI_FRAME_MS))
        return 0;
    u->dirty = 0;
    u->drawn_ms = ms;
    return d;
}

uint32_t ui_led(const ui *u, uint32_t button)
{
    switch (button) {
    case UI_BTN_ENV:                            /* its ring starts at 4A, the one page here */
        return u->state == UI_EDIT ? UI_LED_LIT : UI_LED_GLOW;
    case UI_BTN_SEL:
    case UI_BTN_LFO:
    case UI_BTN_FX:                             /* none of their pages shows */
        return UI_LED_GLOW;
    case UI_BTN_EDIT:                           /* lit while the sound differs from the stored patch */
        return play_edited(u->p) ? UI_LED_LIT : UI_LED_GLOW;
    default:
        return UI_LED_OFF;
    }
}

uint32_t ui_take_full(ui *u, uint32_t ms)
{
    u->shown_note = play_last_note(u->p);
    u->shown_oct = u->p->oct;
    u->dirty = 0;
    u->drawn_ms = ms;
    return UI_D_ALL;
}

uint32_t ui_next_band(uint32_t *pending)
{
    uint32_t b = *pending & (0u - *pending);    /* the lowest bit: FULL, FOCUS, SLOT, then the columns */
    *pending &= ~b;
    return b;
}

void ui_octave_label(int32_t oct, char *s)
{
    static const char OCT[] = "OCT ";
    uint32_t i;
    for (i = 0; OCT[i]; i++)
        s[i] = OCT[i];
    s[i++] = oct < 0 ? '-' : '+';
    s[i++] = (char)('0' + (oct < 0 ? -oct : oct));
    s[i] = 0;
}

void ui_slot_number(const ui_slot *sl, char *s)
{
    s[0] = (char)('0' + sl->num / 10u);
    s[1] = (char)('0' + sl->num % 10u);
    s[2] = 0;
}

const char *ui_banner(const ui *u) { return u->banner; }

void ui_patch_number(const patch *t, char *s)
{
    *s++ = 'P';
    *put_u(s, (uint32_t)(t - PATCHES) + 1u) = 0;
}
