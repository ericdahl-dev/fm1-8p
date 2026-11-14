/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui.h"
#include "ui_save.h"

const ui_slot *ui_page_slot(const ui *u, uint32_t k)
{
    if (k >= 4u)
        return 0;
    return u->state == UI_PLAY ? &UI_SLOTS[UI_PLAY_SLOT[k]] : &UI_SLOTS[4u * u->page + k];
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
    } else if (id == FM_PARAM_FILTER_ENV_AMOUNT_CENTS) {     /* its size (58 MODE: the sign) */
        num = put_u(num, ((uint32_t)(value < 0 ? -value : value) + 50u) / 100u), u = "st";
    } else if (id == FM_PARAM_FILTER_KEY_TRACK) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value + 5) / 10u), u = "%";
    } else if (id == FM_PARAM_OSC1_OCTAVE || id == FM_PARAM_OSC2_OCTAVE) {    /* RANGE in feet */
        static const char *const FEET[5] = {"32", "16", "8", "4", "2"};
        num = put_str(num, FEET[value < -2 ? 0 : value > 2 ? 4 : value + 2]), u = "'";
    } else if (id == FM_PARAM_OSC1_WAVE || id == FM_PARAM_OSC2_WAVE) {
        static const char *const WAVE[4] = {"SAW", "PULSE", "SQUARE", "NOISE"};
        num = put_str(num, WAVE[value < 0 ? 0 : value > 3 ? 3 : value]);
    } else if (id == FM_PARAM_LFO_PITCH_CENTS || id == FM_PARAM_LFO_PITCH_OSC2_CENTS) {
        if (value < 0)                          /* DCO-2's -1: DCO-1's depth */
            num = put_str(num, "D1");
        else
            num = put_u(num, (uint32_t)value), u = "ct";
    } else if (id == FM_PARAM_PITCH_ENV_OSC1_CENTS || id == FM_PARAM_PITCH_ENV_OSC2_CENTS) {
        uint32_t t = ((uint32_t)(value < 0 ? -value : value) + 5u) / 10u;    /* its size (32 MODE: the sign) */
        num = put_u(num, t / 10u);
        *num++ = '.';
        num = put_u(num, t % 10u), u = "st";
    } else if (id == FM_PARAM_OSC1_FINE_CENTS || id == FM_PARAM_OSC2_FINE_CENTS ||
               id == FM_PARAM_OSC2_COARSE_SEMIS) {
        if (value)
            *num++ = value < 0 ? '-' : '+';
        num = put_u(num, (uint32_t)(value < 0 ? -value : value));
        u = id == FM_PARAM_OSC2_COARSE_SEMIS ? "st" : "ct";
    } else if (id == FM_PARAM_OSC1_PULSE_WIDTH_PCT || id == FM_PARAM_OSC2_PULSE_WIDTH_PCT) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value)), u = "%";
    } else if (id == FM_PARAM_PITCH_ENV_DYNA || id == FM_PARAM_OSC_MIX_ENV_DYNA ||
               id == FM_PARAM_FILTER_ENV_DYNA || id == FM_PARAM_AMP_DYNA) {    /* DYNAMICS */
        if (value <= 0)
            num = put_str(num, "OFF");
        else
            num = put_u(num, (uint32_t)value);
    } else if (id == FM_PARAM_OSC1_LEVEL || id == FM_PARAM_OSC2_LEVEL || id == FM_PARAM_OSC_MIX_ENV) {
        num = put_u(num, ((uint32_t)(value < 0 ? -value : value) + 5u) / 10u);    /* 0..100 */
    } else if (id == FM_PARAM_AMP_ATTACK_MS || id == FM_PARAM_AMP_DECAY_MS || id == FM_PARAM_AMP_RELEASE_MS ||
               id == FM_PARAM_FILTER_ENV_ATTACK_MS || id == FM_PARAM_FILTER_ENV_DECAY_MS ||
               id == FM_PARAM_FILTER_ENV_RELEASE_MS || id == FM_PARAM_LFO_DELAY_MS) {
        uint32_t ms = (uint32_t)(value < 0 ? 0 : value);   /* 999 ms, 1.25 s, 12.5 s */
        if (ms < 1000u)
            num = put_u(num, ms), u = "ms";
        else
            num = put_3(num, ms), u = "s";
    } else if (id == FM_PARAM_AMP_SUSTAIN || id == FM_PARAM_FILTER_ENV_SUSTAIN || id == FM_PARAM_AMP_LEVEL) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value + 5) / 10u);     /* 0..100 */
    } else if (id == FM_PARAM_AMP_KEY_FOLLOW || id == FM_PARAM_FILTER_ENV_KEY_FOLLOW) {
        uint32_t k = (uint32_t)(value < 0 ? 0 : value + 130) / 260u;      /* the JX's positions */
        if (k)
            num = put_u(num, k > 3u ? 3u : k);
        else
            num = put_str(num, "OFF");
    } else if (id == FM_PARAM_LFO_WAVE) {
        static const char *const LW[4] = {"TRI", "SAW", "SQUARE", "RANDOM"};
        num = put_str(num, LW[value < 0 ? 0 : value > 3 ? 3 : value]);
    } else if (id == FM_PARAM_LFO_RATE_CENTIHZ) {                       /* 0.01 .. 30.0 Hz */
        num = put_3(num, 10u * (uint32_t)(value < 1 ? 1 : value)), u = "Hz";
    } else if (id == FM_PARAM_LFO_PW_PCT) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value)), u = "%";
    } else if (id == FM_PARAM_SETTING_GLIDE) {
        num = put_str(num, value ? "ON" : "OFF");
    } else if (id == FM_PARAM_VOICE_MODE) {
        num = put_str(num, value ? "UNISON" : "POLY");
    } else if (id == FM_PARAM_BEND_RANGE) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value)), u = "st";
    } else if (id == FM_PARAM_UNISON_DETUNE_CENTS) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value)), u = "ct";
    } else if (id == FM_PARAM_AMP_GATE) {
        num = put_str(num, value ? "GATE" : "ENV2");
    } else if (id == FM_PARAM_CHORUS_MODE) {
        static const char *const CH[4] = {"OFF", "I", "II", "FAST"};
        num = put_str(num, CH[value < 0 ? 0 : value > 3 ? 3 : value]);
    } else if (id == FM_PARAM_AMP_VELOCITY) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value)), u = "%";
    } else if (id == FM_PARAM_OSC_XMOD) {
        num = put_u(num, (uint32_t)(value < 0 ? 0 : value + 5) / 10u), u = "%";
    } else if (id == FM_PARAM_PITCH_ENV_SOURCE || id == FM_PARAM_OSC_MIX_ENV_SOURCE ||
               id == FM_PARAM_FILTER_ENV_SOURCE) {   /* an ENV MODE: source | inverted << 1 */
        static const char *const MODE[4] = {"ENV1", "ENV2", "ENV1 INV", "ENV2 INV"};
        num = put_str(num, MODE[value & 3]);
    } else if (id == FM_PARAM_OSC_SYNC) {       /* 23 X-MOD's sync | ring << 1 (CROSS MOD) */
        static const char *const XMOD[4] = {"OFF", "SYNC 1", "X-MOD", "SYNC 2"};
        num = put_str(num, XMOD[value & 3]);
    } else if (id == FM_PARAM_HIGHPASS_STEP) {  /* the JX positions; step 1 is the engine's own */
        static const char *const HPF[5] = {"FLAT", "100", "240", "560", "BOOST"};
        num = put_str(num, HPF[value < 0 ? 0 : value > 4 ? 4 : value]);
        u = value == 0 || value >= 4 ? "" : "Hz";
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
        if (*unit != '\'')                     /* (feet: 8') */
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
    return c == '-' ? 0x40u : c == '_' ? 0x08u : 0u;
}

void ui_column(uint16_t id, int32_t value, char *num, char *unit)
{
    uint32_t i;
    ui_value(id, value, num, unit);
    if (num[0] == 'E' && num[1] == 'N' && num[4] == ' ') {     /* ENV2 INV: E2 INV (7 characters fit) */
        num[1] = num[3];
        for (i = 2; (num[i] = num[i + 2u]) != 0; i++) {}
    }
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

uint32_t ui_isqrt(uint32_t v)
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
    uint32_t q = 2867u + 6u * (r * ui_isqrt(r) / 64u);
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

void ui_meter_bar(uint16_t id, int32_t value, int32_t w, int32_t *a, int32_t *b)
{
    const fm_param_info *in = fm_param_info_get(id);
    int32_t span, at, x;
    *a = *b = 0;
    if (!in || in->max <= in->min)
        return;
    span = in->max - in->min;
    at = value < in->min ? 0 : value > in->max ? span : value - in->min;
    x = (2 * w * at + span) / (2 * span);       /* rounded (w x span stays well inside 32 bits) */
    if (in->min == -in->max) {                  /* bipolar: from the middle */
        *a = x < w / 2 ? x : w / 2;
        *b = x < w / 2 ? w / 2 : x;
    } else {
        *b = x;
    }
}

void ui_slot_bar(const ui_slot *sl, int32_t value, int32_t w, int32_t *a, int32_t *b)
{
    int32_t max;
    if (sl->how == UI_HOW_TIME || sl->how == UI_HOW_RATE) {    /* its place in its table */
        const uint16_t *t = sl->how == UI_HOW_TIME ? UI_TIMES : UI_RATES;
        int32_t i = 0, n = (int32_t)(sl->how == UI_HOW_TIME ? UI_TIME_COUNT : UI_RATE_COUNT);
        while (i < n - 1 && t[i + 1] <= value)
            i++;
        *a = 0;
        *b = (2 * w * i + (n - 1)) / (2 * (n - 1));
        return;
    }
    if (sl->how != UI_HOW_SIZE) {
        ui_meter_bar(sl->id, value, w, a, b);
        return;
    }
    max = fm_param_info_get(sl->id)->max;
    *a = 0;
    *b = value <= 0 || max <= 0 ? 0 : value >= max ? w : (2 * w * value + max) / (2 * max);
}

uint32_t ui_choice_pos(const ui_slot *sl, int32_t value, uint32_t *n)
{
    uint32_t i, pos = 0, seen = 0;
    for (i = 0; i < sl->n_order; i++) {
        seen += sl->stops >> i & 1u;
        if (sl->order[i] == value && seen)
            pos = seen - 1u;                    /* the last stop at or before the value */
    }
    *n = seen;
    return pos;
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
    u->page = UI_PAGE_5A;                       /* (EDIT's first page) */
    u->inv = 0;
    u->held = 0;
    u->comparing = 0;
    u->now_ms = 0;
    u->shown_edited = 0;
    u->save_waiting = 0;
    u->save_home_armed = 0;
    u->save_draft_prog = UI_SAVE_NO_DRAFT;
    u->jump = -1;
    u->jump_keys = 0;
    u->jump_screen = 0;
    u->readout = 0;
    u->readout_ms = 0;
    u->glo_from = UI_PLAY;
    u->glo_page = 0;
    ui_settings_default(&u->set);
    u->set_dirty = 0;
    u->set_changed_ms = 0;
    u->shown_theme = u->set.theme;
}

static void show_page(ui *u, ui_state st)
{
    uint32_t k;
    u->state = st;
    u->readout = 0;                             /* (PLAY's knob readout is that visit's) */
    u->focus = 1;                               /* knob 2, as visual.md's pages open, */
    if (UI_SLOTS[4u * u->page + 1u].kind != UI_LIVE)   /* or the first live one (4B: 45 alone) */
        for (k = 0; k < 4u; k++)
            if (UI_SLOTS[4u * u->page + k].kind == UI_LIVE) {
                u->focus = k;
                break;
            }
    u->dirty |= UI_D_FULL;
}

/* a page with a live slot (the rest are skipped: interaction.md, page order) */
static int page_live(uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (UI_SLOTS[4u * p + k].kind == UI_LIVE)
            return 1;
    return 0;
}

/* where page p is in ring r, or -1 */
static int32_t ring_at(const uint8_t *r, uint32_t p)
{
    int32_t i;
    for (i = 0; r[i] != UI_RING_END; i++)
        if (r[i] == p)
            return i;
    return -1;
}

/* a section button: its ring's first live page, or on its ring the next live one, around */
static void ring_step(ui *u, const uint8_t *r)
{
    int32_t i = u->state == UI_EDIT ? ring_at(r, u->page) : -1, n, j;
    for (n = 0; r[n] != UI_RING_END; n++) {}
    for (j = 1; j <= n; j++) {
        uint32_t p = r[(i < 0 ? j - 1 : i + j) % n];
        if (page_live(p)) {
            u->page = p;
            show_page(u, UI_EDIT);
            return;
        }
    }
}

int32_t ui_wave_level(uint32_t wave, uint32_t width_pct, uint32_t i, uint32_t n)
{
    uint32_t ph = n ? (2000u * i / n) % 1000u : 0;  /* the place in its cycle, per mille */
    uint32_t r;
    switch (wave) {
    case FM_WAVE_SAW:
        return 2 * (int32_t)ph - 1000;
    case FM_WAVE_PULSE:
        return ph < 10u * width_pct ? 1000 : -1000;
    case FM_WAVE_SQUARE:
        return ph < 500u ? 1000 : -1000;
    default:                                    /* noise: a fixed scatter, the same every draw */
        r = (i + 1u) * 2654435761u;
        return (int32_t)(r >> 16 & 2047u) * 2000 / 2047 - 1000;
    }
}

int ui_slot_active(const ui *u, const ui_slot *sl)
{
    if (sl->id == FM_PARAM_OSC1_PULSE_WIDTH_PCT)
        return play_get_param(u->p, FM_PARAM_OSC1_WAVE) == FM_WAVE_PULSE;
    if (sl->id == FM_PARAM_OSC2_PULSE_WIDTH_PCT)
        return play_get_param(u->p, FM_PARAM_OSC2_WAVE) == FM_WAVE_PULSE;
    if (sl->id == FM_PARAM_AMP_VELOCITY)        /* the curve DYNAMICS OFF uses */
        return play_get_param(u->p, FM_PARAM_AMP_DYNA) == 0;
    if (sl->id == FM_PARAM_UNISON_DETUNE_CENTS) /* the unison spread */
        return play_get_param(u->p, FM_PARAM_VOICE_MODE) == FM_VOICE_MODE_UNISON;
    return 1;
}

/* the ENV MODE a depth's sign belongs to: its bit in ui.inv */
static uint32_t inv_bit(uint16_t depth)
{
    return depth == FM_PARAM_OSC_MIX_ENV ? 2u : depth == FM_PARAM_FILTER_ENV_AMOUNT_CENTS ? 4u : 1u;
}

/* a depth is inverted: negative, or at 0 with its ENV MODE set to INV */
static int inverted(const ui *u, uint16_t depth)
{
    int32_t d = play_get_param(u->p, depth);
    return d < 0 || (d == 0 && u->inv & inv_bit(depth));
}

/* e^(-4 t) for t per mille, per mille (2^(-5.771 t) by exp2_q8) */
static int32_t decay4(uint32_t t)
{
    return (int32_t)(exp2_q8(-(int32_t)(t * 23638u / 1000u)) * 1000u / 256u);
}

int32_t ui_lfo_level(uint32_t wave, uint32_t i, uint32_t n)
{
    uint32_t ph = n ? (2000u * i / n) % 1000u : 0, cyc = n ? 2u * i / n : 0;
    switch (wave) {
    case FM_LFO_TRIANGLE:                       /* low at the cycle's start, high in its middle */
        return ph < 500u ? 4 * (int32_t)ph - 1000 : 3000 - 4 * (int32_t)ph;
    case FM_LFO_SAW:
        return 2 * (int32_t)ph - 1000;
    case FM_LFO_SQUARE:
        return ph < 500u ? 1000 : -1000;
    default: {                                  /* random: a level held through each cycle */
        static const int16_t R[2] = {600, -450};
        return R[cyc & 1u];
    }
    }
}

int32_t ui_adsr_level(uint32_t a, uint32_t d, uint32_t s, uint32_t r, uint32_t i, uint32_t n)
{
    /* stage widths in tenths: attack 6 + 60 a, decay 8 + 70 d, hold 34, release 8 + 70 r */
    uint32_t ta = 60u + 600u * a / 1000u, td = 80u + 700u * d / 1000u, th = 340u, tr = 80u + 700u * r / 1000u;
    uint32_t x = n > 1u ? (ta + td + th + tr) * i / (n - 1u) : 0;
    if (x < ta)
        return (int32_t)(1000u * x / ta);
    x -= ta;
    if (x < td)
        return (int32_t)s + (int32_t)(1000u - s) * decay4(1000u * x / td) / 1000;
    x -= td;
    if (x < th)
        return (int32_t)s;
    x -= th;
    x = x > tr ? tr : x;
    {
        uint32_t t = 1000u * x / tr;
        return (int32_t)s * decay4(t) / 1000 * (int32_t)(1000u - t) / 1000;
    }
}

uint32_t ui_time_count(void) { return UI_TIME_COUNT; }

int32_t ui_time_at(uint32_t i) { return UI_TIMES[i < UI_TIME_COUNT ? i : UI_TIME_COUNT - 1u]; }

/* v clicks entries on in a rising table t of n (between entries: snapped the way of the turn) */
static int32_t table_step(const uint16_t *t, int32_t n, int32_t v, int32_t clicks)
{
    int32_t i = 0;
    while (i < n - 1 && t[i + 1] <= v)          /* the last entry at or below v */
        i++;
    if (clicks < 0 && t[i] < v)                 /* between entries: that one is already a click down */
        clicks++;
    i += clicks;
    return t[i < 0 ? 0 : i >= n ? n - 1 : i];
}

int32_t ui_time_step(int32_t ms, int32_t clicks)
{
    return table_step(UI_TIMES, (int32_t)UI_TIME_COUNT, ms, clicks);
}

uint32_t ui_rate_count(void) { return UI_RATE_COUNT; }

int32_t ui_rate_at(uint32_t i) { return UI_RATES[i < UI_RATE_COUNT ? i : UI_RATE_COUNT - 1u]; }

int32_t ui_slot_value(const ui *u, const ui_slot *sl)
{
    int32_t v = play_get_param(u->p, sl->id);
    switch (sl->how) {
    case UI_HOW_PAIR:
        return v | play_get_param(u->p, sl->id2) << 1;
    case UI_HOW_MODE:
        return v | inverted(u, sl->id2) << 1;
    case UI_HOW_SIZE:
        return v < 0 ? -v : v;
    default:
        return v;
    }
}

static void set_depth(ui *u, uint16_t depth, int neg)
{
    int32_t d = play_get_param(u->p, depth);
    d = d < 0 ? -d : d;
    play_set_param(u->p, depth, neg ? -d : d);
}

static void set_slot(ui *u, const ui_slot *sl, int32_t v)
{
    switch (sl->how) {
    case UI_HOW_PAIR:
        play_set_param(u->p, sl->id, v & 1);
        play_set_param(u->p, sl->id2, v >> 1 & 1);
        return;
    case UI_HOW_MODE:                           /* the source, and the sign of its depths */
        play_set_param(u->p, sl->id, v & 1);
        u->inv = v >> 1 & 1 ? u->inv | inv_bit(sl->id2) : u->inv & ~inv_bit(sl->id2);
        set_depth(u, sl->id2, v >> 1 & 1);
        set_depth(u, sl->id3, v >> 1 & 1);
        return;
    case UI_HOW_SIZE:                           /* the size, kept on its ENV MODE's side */
        v = v < 0 ? 0 : v;
        play_set_param(u->p, sl->id, inverted(u, sl->id) ? -v : v);
        return;
    default:
        play_set_param(u->p, sl->id, v);
    }
}

/* SELECT: clicks pages on in the page order, around, skipping pages with nothing live */
static void walk_pages(ui *u, int32_t clicks)
{
    int32_t p = (int32_t)u->page, way = clicks > 0 ? 1 : -1, guard;
    for (; clicks; clicks -= way)
        for (guard = 0; guard < (int32_t)UI_PAGES; guard++) {
            p = (p + way + (int32_t)UI_PAGES) % (int32_t)UI_PAGES;
            if (page_live((uint32_t)p))
                break;
        }
    u->page = (uint32_t)p;
    show_page(u, UI_EDIT);
}

/* ALGORITHM: clicks sections on (1..9, 0, around), to the section's page A */
static void walk_sections(ui *u, int32_t clicks)
{
    static const uint8_t A[10] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16};   /* 1A, 2A, ... 9A, 0A */
    int32_t s = 0;
    while (s < 9 && A[s + 1] <= u->page)
        s++;
    s = ((s + clicks) % 10 + 10) % 10;
    u->page = A[s];
    show_page(u, UI_EDIT);
}

/* the number jump's keypad: the white keys F3..A4 (key 0 = F3) are the digits 1..9 and 0; -1 not one */
static int32_t key_digit(uint32_t k)
{
    static const int8_t D[17] = {1, -1, 2, -1, 3, -1, 4, 5, -1, 6, -1, 7, 8, -1, 9, -1, 0};
    return k < 17u ? D[k] : -1;
}

/* the page A of section s (0..9: 1x .. 9x, then 0x) */
static uint32_t section_a(int32_t s)
{
    return UI_SECTION_PAGE[s];
}

static void show_banner(ui *u, const char *s, uint32_t ms);

/* number n's page and focus (interaction.md 4: a number with no parameter shows its page with no focus,
 * and with two digits typed the banner NO PARAMETER) */
static void jump_to(ui *u, int32_t n, int typed, uint32_t ms)
{
    uint32_t p = section_a(n / 10), x = (uint32_t)(n % 10), k;
    if (x >= 5u && x <= 8u && p + 1u < UI_PAGES && UI_PAGE_INFO[p + 1u].name[0] == UI_PAGE_INFO[p].name[0])
        p++;                                    /* x5..x8 on the section's page B */
    u->page = p;
    show_page(u, UI_EDIT);
    k = x >= 1u && x <= 8u ? (x - 1u) % 4u : 4u;
    if (k < 4u && (x <= 4u || UI_PAGE_INFO[p].name[1] == 'B') && UI_SLOTS[4u * p + k].kind != UI_RESERVED) {
        u->focus = k;
    } else {
        u->focus = 4u;                          /* none: the focus row is empty */
        if (typed)
            show_banner(u, "NO PARAMETER", ms);
    }
}

static void turn(ui *u, uint32_t k, int32_t clicks, uint32_t ms)
{
    const ui_slot *s = ui_page_slot(u, k);
    if (!s || s->kind != UI_LIVE)
        return;
    if (s->order) {                             /* a choice: each click to the next stop, clamped */
        int32_t v = ui_slot_value(u, s), max = s->how == UI_HOW_PLAIN ? fm_param_info_get(s->id)->max : 3;
        int32_t way = clicks > 0 ? 1 : -1, i = 0, j;
        while (i < (int32_t)s->n_order - 1 && s->order[i] != v)
            i++;
        for (j = i + way; clicks && j >= 0 && j < (int32_t)s->n_order; j += way)
            if (s->stops >> j & 1u && s->order[j] <= max) {    /* (a value the engine lacks: none) */
                i = j;
                clicks -= way;
            }
        set_slot(u, s, s->order[i]);
    } else if (s->how == UI_HOW_TIME || s->how == UI_HOW_RATE) {   /* its table: an entry a click, four fast */
        play_set_param(u->p, s->id, table_step(s->how == UI_HOW_TIME ? UI_TIMES : UI_RATES,
                                               (int32_t)(s->how == UI_HOW_TIME ? UI_TIME_COUNT : UI_RATE_COUNT),
                                               play_get_param(u->p, s->id),
                                               clicks >= 3 || clicks <= -3 ? 4 * clicks : clicks));
    } else if (s->id == FM_PARAM_LFO_PITCH_CENTS || s->id == FM_PARAM_LFO_PITCH_OSC2_CENTS) {
        int32_t v = play_get_param(u->p, s->id), way = clicks > 0 ? 1 : -1, fast = clicks >= 3 || clicks <= -3;
        for (; clicks; clicks -= way) {         /* 1 to 99 ct by 1 (fast 10), from 100 by 10 (fast 50) */
            int32_t big = way > 0 ? v >= 100 : v > 100;
            v += way * (big ? (fast ? 50 : 10) : (fast ? 10 : 1));
        }
        play_set_param(u->p, s->id, v);
    } else {
        set_slot(u, s, ui_slot_value(u, s) + (clicks >= 3 || clicks <= -3 ? s->fast : s->step) * clicks);
    }
    u->dirty |= (u->focus < 4u ? UI_D_COL(u->focus) : 0u) | UI_D_COL(k) | UI_D_FOCUS;
    if (u->state == UI_PLAY) {                  /* the focus row reads the knob for 3 s; the slot is the badge */
        u->readout = 1;
        u->readout_ms = ms;
    } else if (s->id == FM_PARAM_FILTER_CUTOFF_CENTS || s->id == FM_PARAM_FILTER_RESONANCE ||
        s->id == FM_PARAM_OSC1_WAVE || s->id == FM_PARAM_OSC1_PULSE_WIDTH_PCT ||
        s->id == FM_PARAM_OSC2_WAVE || s->id == FM_PARAM_OSC2_PULSE_WIDTH_PCT || s->id == FM_PARAM_LFO_WAVE ||
        (u->page >= 12u && u->page <= 15u))     /* (an ENV page: any of its ADSR) */
        u->dirty |= UI_D_SLOT;                  /* what the slot draws: the curve, the waveform, the ADSR */
    if (s->id == FM_PARAM_VOICE_MODE)           /* 08 DETUNE's activity follows it, on the same page */
        u->dirty |= UI_D_COL(0) | UI_D_COL(1) | UI_D_COL(2) | UI_D_COL(3);
    u->focus = k;
}

/* GLO (interaction.md 4) */
const ui_glo_slot *ui_glo_slot_at(const ui *u, uint32_t k)
{
    return &UI_GLO_SLOTS[4u * u->glo_page + (k & 3u)];
}

static uint8_t *glo_byte(ui *u, const ui_glo_slot *g)
{
    return (uint8_t *)&u->set + UI_SETTINGS_INFO[g->setting].offset;
}

static void glo_read(const ui *u, uint32_t k, char *num, char *unit, int col)
{
    const ui_glo_slot *g = ui_glo_slot_at(u, k);
    const ui_setting_info *in = &UI_SETTINGS_INFO[g->setting];
    uint32_t v = ((const uint8_t *)&u->set)[in->offset];
    *unit = 0;
    if (g->kind != UI_LIVE)
        *put_str(num, "--") = 0;
    else if (g->names)
        *put_str(num, (col && g->cols ? g->cols : g->names)[v - in->min]) = 0;
    else if (g->zero && v == in->min)
        *put_str(num, g->zero) = 0;
    else {
        *put_u(num, v) = 0;
        if (g->setting == 8u)                   /* G31: the velocity curve, in % */
            *put_str(unit, "%") = 0;
    }
}

uint32_t ui_theme_shown(const ui *u) { return u->shown_theme; }

void ui_glo_value(const ui *u, uint32_t k, char *num, char *unit) { glo_read(u, k, num, unit, 0); }
void ui_glo_column(const ui *u, uint32_t k, char *num, char *unit) { glo_read(u, k, num, unit, 1); }

/* a GLO page opens on knob 2, or its first live knob, or none (4) when all four wait */
static void glo_show(ui *u)
{
    uint32_t k;
    u->focus = 4u;
    if (ui_glo_slot_at(u, 1)->kind == UI_LIVE)
        u->focus = 1u;
    else
        for (k = 0; k < 4u; k++)
            if (ui_glo_slot_at(u, k)->kind == UI_LIVE) {
                u->focus = k;
                break;
            }
    u->dirty |= UI_D_FULL;
}

static void glo_turn(ui *u, uint32_t k, int32_t clicks, uint32_t ms)
{
    const ui_glo_slot *g = ui_glo_slot_at(u, k);
    const ui_setting_info *in = &UI_SETTINGS_INFO[g->setting];
    uint8_t *b = glo_byte(u, g);
    int32_t v;
    if (g->kind != UI_LIVE)
        return;
    v = (int32_t)*b + clicks * (clicks >= 3 || clicks <= -3 ? g->fast : g->step);
    v = v < in->min ? in->min : v > in->max ? in->max : v;
    if ((uint8_t)v != *b) {
        *b = (uint8_t)v;
        u->set_dirty = 1;
        u->set_changed_ms = ms;
        if (g->setting == 11u)                  /* G34: the theme cards' frame */
            u->dirty |= UI_D_SLOT;
    }
    u->dirty |= (u->focus < 4u ? UI_D_COL(u->focus) : 0u) | UI_D_COL(k) | UI_D_FOCUS;
    u->focus = k;
}

static void glo_walk(ui *u, int32_t clicks)
{
    u->glo_page = (uint32_t)(((int32_t)u->glo_page + clicks) % (int32_t)UI_GLO_PAGES + (int32_t)UI_GLO_PAGES) %
                  UI_GLO_PAGES;
    glo_show(u);
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
    int32_t way = clicks > 0 ? 1 : -1;
    if (!clicks)                                /* no turn: no load (and no notes let go) */
        return;
    if (play_edited(u->p) && !(u->warned == way && ms - u->warned_ms < UI_DISCARD_MS)) {
        show_banner(u, "EDITED - TURN TO DISCARD", ms);   /* the first event loads nothing */
        u->warned = way;
        u->warned_ms = ms;
        return;
    }
    u->warned = 0;
    u->banner = "";                             /* the warning goes with the edits */
    if (clicks >= 3 || clicks <= -3)          /* a fast turn: 8, a row of slots (11 to 21) */
        clicks = clicks > 0 ? 8 : -8;
    (void)play_load_prog(u->p, play_prog_step(u->p, play_prog(u->p), clicks));
    u->inv = 0;                                 /* the ENV MODEs are the new patch's signs */
    u->dirty |= (UI_D_ALL & ~UI_D_FULL) | UI_D_FOOT;   /* the page and focus stay; its values change */
}

#define JUMP_USED 0x80000000u                  /* ui.jump_keys: a key went down while SEL was held */

int ui_key(ui *u, uint32_t k, int down, uint32_t ms)
{
    int32_t d = key_digit(k);
    if (!down) {                                /* an up goes where its down went */
        if (!(u->jump_keys >> k & 1u))
            return 0;
        u->jump_keys &= ~(1u << k);
        return 1;
    }
    if (u->jump < 0)
        return 0;
    u->jump_keys |= 1u << k | JUMP_USED;
    if (d < 0)                                  /* (a black key: nothing, and no sound) */
        return 1;
    if (u->jump == 0) {
        u->jump = d + 1;
        if (!(u->jump_screen & UI_JS_UP))       /* a digit: the jump's screen at once */
            u->dirty |= UI_D_FULL;
        else
            u->dirty |= UI_D_FOCUS;             /* (its number) */
        u->jump_screen = (u->jump_screen | UI_JS_UP) & ~UI_JS_DONE;
    } else {
        u->jump_screen = UI_JS_DONE;            /* the page now, SEL still down; no timer after */
        jump_to(u, (u->jump - 1) * 10 + d, 1, ms);
        u->jump = 0;
    }
    return 1;
}

uint32_t ui_keypad_led(const ui *u, uint32_t k)
{
    return u->jump >= 0 && key_digit(k) >= 0 ? UI_LED_GLOW : UI_LED_OFF;
}

void ui_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms)
{
    uint32_t id = ev & 0xFFu;
    uint32_t tap = (ev & 0xFF000000u) == UI_EV_BTN_UP && id < UI_BUTTONS &&
                   ms - u->down_ms[id] < UI_TAP_MS;   /* a release: was it a tap? */
    if ((ev & 0xFF000000u) == UI_EV_BTN_DOWN && id < UI_BUTTONS) {
        u->down_ms[id] = ms;
        u->held |= 1u << id;
    }
    if ((ev & 0xFF000000u) == UI_EV_BTN_UP && id < UI_BUTTONS)
        u->held &= ~(1u << id);
    if (u->state == UI_SAVE) {                  /* SAVE takes the controls while it is open */
        ui_save_event(u, ev, arg, ms);
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_HOME) && tap && u->save_waiting) {
        ui_save_home(u, ms);                    /* asks, then cancels the waiting write; nothing else */
        return;
    }
    if (u->state == UI_GLO) {                   /* GLO takes the controls but PRESETS (interaction.md 7) */
        if ((ev == (UI_EV_BTN_UP | UI_BTN_GLO) || ev == (UI_EV_BTN_UP | UI_BTN_HOME)) && tap) {
            u->state = u->glo_from;             /* back, on the page it left */
            u->dirty |= UI_D_FULL;
        } else if (ev == (UI_EV_ENC | UI_ENC_PRESETS)) {
            browse(u, arg, ms);
        } else if ((ev & 0xFF000000u) == UI_EV_ENC && id >= UI_ENC_KNOB1 && id <= UI_ENC_KNOB4) {
            glo_turn(u, id - UI_ENC_KNOB1, arg, ms);
        } else if ((ev == (UI_EV_ENC | UI_ENC_SELECT) || ev == (UI_EV_ENC | UI_ENC_ALGORITHM)) && arg) {
            glo_walk(u, arg);
        }
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_GLO) && tap && (u->state == UI_PLAY || u->state == UI_EDIT) &&
        !u->comparing && !(u->jump_screen & UI_JS_UP)) {
        u->glo_from = u->state;
        u->state = UI_GLO;
        u->readout = 0;
        u->warned = 0;
        glo_show(u);
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_SAVE) && tap && !u->comparing && !u->save_waiting) {
        u->jump = -1;                           /* SEL held: the keypad ends (keys down still get their ups) */
        u->jump_keys &= ~JUMP_USED;
        u->jump_screen = 0;                     /* and its screen: SAVE's is drawn in full (ui_save_open) */
        ui_save_open(u, ms);
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_EDIT) && u->comparing) {   /* the edit buffer back, exactly */
        if (u->comparing == UI_COMPARE_ON) {
            play_compare_end(u->p);
            u->dirty |= (UI_D_ALL & ~UI_D_FULL) | UI_D_HEAD;
        }
        u->comparing = 0;
        return;
    }
    if (u->comparing == UI_COMPARE_ON && (ev & 0xFF000000u) == UI_EV_ENC) {   /* nothing changes */
        if (id >= UI_ENC_KNOB1 && id <= UI_ENC_KNOB4)
            show_banner(u, "COMPARE: RELEASE EDIT", ms);
        return;
    }
    if (ui_screen(u) == UI_SCREEN_JUMP && ev != (UI_EV_BTN_UP | UI_BTN_SEL))
        return;                                 /* the jump's screen: only its keys and SEL's release act */
    if (ev == (UI_EV_BTN_DOWN | UI_BTN_SEL) && (u->state == UI_PLAY || u->state == UI_EDIT))
        u->jump = 0;                            /* the keypad, while SEL is held (in PLAY or EDIT only) */
    if (ev != (UI_EV_ENC | UI_ENC_PRESETS))
        u->warned = 0;                          /* anything else cancels the warning (decision Q4) */
    if (ev == (UI_EV_BTN_DOWN | UI_BTN_ENV))
        ring_step(u, UI_RING_ENV);
    else if (ev == (UI_EV_BTN_UP | UI_BTN_SEL)) {
        if (u->jump > 0)                        /* one digit: that section's page A, no focus */
            jump_to(u, (u->jump - 1) * 10, 0, ms);
        else if (tap && !(u->jump_keys & JUMP_USED))
            ring_step(u, UI_RING_SEL);          /* a tap with no key: SEL's ring */
        u->jump = -1;
        u->jump_keys &= ~JUMP_USED;
        if (u->jump_screen & UI_JS_UP)
            u->dirty |= UI_D_FULL;              /* its screen goes: the page or PLAY again */
        u->jump_screen = 0;
    }
    else if (ev == (UI_EV_BTN_DOWN | UI_BTN_FX))
        ring_step(u, UI_RING_FX);
    else if (ev == (UI_EV_BTN_DOWN | UI_BTN_LFO))
        ring_step(u, UI_RING_LFO);
    else if (ev == (UI_EV_BTN_UP | UI_BTN_EDIT) && tap)
        show_page(u, u->state == UI_EDIT ? UI_PLAY : UI_EDIT);   /* (a hold: compare, not yet) */
    else if (ev == (UI_EV_BTN_UP | UI_BTN_HOME) && tap && u->state != UI_PLAY)
        show_page(u, UI_PLAY);                  /* (a hold: the host's picker, which sends the UP) */
    else if ((ev & 0xFF000000u) == UI_EV_ENC && id >= UI_ENC_KNOB1 && id <= UI_ENC_KNOB4 &&
             (u->state == UI_EDIT || u->state == UI_PLAY))     /* (PLAY: the performance four) */
        turn(u, id - UI_ENC_KNOB1, arg, ms);
    else if (ev == (UI_EV_ENC | UI_ENC_SELECT) && arg)     /* (in PLAY: from the last edit page) */
        walk_pages(u, arg);
    else if (ev == (UI_EV_ENC | UI_ENC_ALGORITHM) && arg)
        walk_sections(u, arg);
    else if (ev == (UI_EV_ENC | UI_ENC_PRESETS))
        browse(u, arg, ms);
}

int ui_play_readout(const ui *u, uint32_t ms)
{
    return u->readout && ms - u->readout_ms < UI_READOUT_MS;
}

uint32_t ui_screen(const ui *u)
{
    if (u->state == UI_SAVE)
        return UI_SCREEN_SAVE;
    if (u->state == UI_GLO)
        return UI_SCREEN_GLO;
    return u->jump_screen & UI_JS_UP ? UI_SCREEN_JUMP : u->state == UI_EDIT ? UI_SCREEN_PAGE : UI_SCREEN_PLAY;
}

void ui_jump_digits(const ui *u, char *s)
{
    s[0] = u->jump > 0 ? (char)('0' + (u->jump - 1)) : '_';
    s[1] = '_';
    s[2] = 0;
}

uint32_t ui_take(ui *u, uint32_t ms)
{
    uint32_t d;
    if (u->jump >= 0 && !u->jump_screen && ms - u->down_ms[UI_BTN_SEL] >= UI_TAP_MS) {
        u->jump_screen = UI_JS_UP;              /* SEL held 400 ms: the jump's screen */
        u->dirty |= UI_D_FULL;
    }
    u->now_ms = ms;
    ui_save_tick(u, ms);
    if (play_edited(u->p) != u->shown_edited) {  /* the footer's `*` */
        u->shown_edited = play_edited(u->p);
        u->dirty |= UI_D_FOOT;
    }
    if (u->held >> UI_BTN_EDIT & 1u && !u->comparing && u->state != UI_GLO &&
        ms - u->down_ms[UI_BTN_EDIT] >= UI_TAP_MS) {
        if (play_edited(u->p)) {                /* an EDIT hold: compare */
            u->comparing = UI_COMPARE_ON;
            play_compare_begin(u->p);
            u->dirty |= (UI_D_ALL & ~UI_D_FULL) | UI_D_HEAD;   /* the page stays: the stored values */
        } else {
            u->comparing = UI_COMPARE_NONE;     /* nothing to compare */
            show_banner(u, "NO EDITS", ms);
        }
    }
    if (u->readout && ms - u->readout_ms >= UI_READOUT_MS) {
        u->readout = 0;                         /* PLAY's knob readout over: the name again, the frame off */
        u->dirty |= UI_D_FOCUS | (u->focus < 4u ? UI_D_COL(u->focus) : 0u);
    }
    if (*u->banner && ms - u->banner_ms >= UI_BANNER_MS) {
        u->banner = "";
        u->dirty |= UI_D_FOOT;
    }
    if (u->set.theme != u->shown_theme && ms - u->set_changed_ms >= UI_THEME_MS) {
        u->shown_theme = u->set.theme;          /* THEME, settled: all of it in the new look */
        u->dirty |= UI_D_FULL;
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
    const uint8_t *r = button == UI_BTN_SEL ? UI_RING_SEL : button == UI_BTN_ENV ? UI_RING_ENV :
                       button == UI_BTN_FX ? UI_RING_FX : UI_RING_LFO;
    switch (button) {
    case UI_BTN_SEL:
    case UI_BTN_ENV:
    case UI_BTN_FX:
    case UI_BTN_LFO:                            /* lit while a page of its ring shows */
        if (u->state == UI_SAVE || u->state == UI_GLO)   /* they do nothing there: off (interaction.md) */
            return UI_LED_OFF;
        return u->state == UI_EDIT && ring_at(r, u->page) >= 0 ? UI_LED_LIT : UI_LED_GLOW;
    case UI_BTN_GLO:                            /* lit in GLO; it glows where a tap opens it */
        return u->state == UI_GLO ? UI_LED_LIT : u->state == UI_SAVE ? UI_LED_OFF : UI_LED_GLOW;
    case UI_BTN_EDIT:                           /* lit while the edit buffer differs from the stored patch */
        if (u->state == UI_GLO)
            return UI_LED_OFF;
        if (u->comparing == UI_COMPARE_ON)      /* blinking during compare */
            return u->now_ms / UI_BLINK_MS & 1u ? UI_LED_OFF : UI_LED_LIT;
        return play_edited(u->p) ? UI_LED_LIT : UI_LED_GLOW;
    default:
        return UI_LED_OFF;
    }
}

uint32_t ui_take_full(ui *u, uint32_t ms)
{
    u->shown_note = play_last_note(u->p);
    u->shown_oct = u->p->oct;
    u->shown_edited = play_edited(u->p);        /* the footer is drawn with it */
    u->dirty = 0;
    u->drawn_ms = ms;
    return UI_D_ALL;
}

uint32_t ui_band(ui *u, uint32_t *pending, uint32_t ms)
{
    uint32_t d;
    if (!*pending || u->dirty & UI_D_FULL) {    /* a frame's bands all go before the next frame's */
        d = ui_take(u, ms);
        if (d & UI_D_FULL)
            *pending = UI_D_ALL;                /* a new page: all of it, none of the old one's */
        else
            *pending |= d;
    }
    return ui_next_band(pending);
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

void ui_prog_number(uint32_t q, char *s)
{
    if (q >= PLAY_PROG_P) {
        *s++ = 'P';
        *put_u(s, q - PLAY_PROG_P + 1u) = 0;
    } else {                                    /* two digits 1..8: 11..18, 21..28, ... 81..88 */
        s[0] = (char)('1' + q / 8u);
        s[1] = (char)('1' + q % 8u);
        s[2] = 0;
    }
}

void ui_patch_number(const patch *t, char *s)
{
    *s++ = 'P';
    *put_u(s, (uint32_t)(t - PATCHES) + 1u) = 0;
}
