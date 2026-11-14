/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM1-8P's screen: the pages firmware/ui.c describes, drawn on fm1-dock's canvases in a
 * docs/ui/visual.md theme (firmware/ui_map.c), only what changed (ui_take), a band part per call.
 * Apart from the module (fm1_8p.c) so the CPU test can draw the same pages while it times (#47). */
#include "draw.h"
#define DFONT_NO_D20                            /* (only D24 is drawn here) */
#include "display_font.h"

static const hb_host_t *H;
static play *P;
static ui *U;

#ifdef FM1_DEBUG
/* The trail (debug builds): every string handed to the host's text calls is checked first. One that
 * isn't in RAM or the flash window is logged with its line here, and drawing stops for good (no
 * fault, so the log keeps it). Look for "fm1-8p TRAIL". */
static uint32_t trail_dead;
static int trail_ok(const char *s, uint32_t line)
{
    uint32_t a = (uint32_t)(uintptr_t)s;
    if ((a >= 0x01C00000u && a < 0x01C80000u) || (a >= 0x02000000u && a < 0x02400000u))
        return 1;
    H->log_u32("fm1-8p TRAIL bad text pointer", a);
    H->log_u32("fm1-8p TRAIL at draw.c line", line);
    trail_dead = 1;
    return 0;
}
#define CV_TEXT(x, y, b, s, c) (trail_ok(s, __LINE__) ? (void)H->cv_text(x, y, b, s, c) : (void)0)
#define TEXT_W(b, s) (trail_ok(s, __LINE__) ? H->text_w(b, s) : 0)
#else
#define CV_TEXT(x, y, b, s, c) H->cv_text(x, y, b, s, c)
#define TEXT_W(b, s) H->text_w(b, s)
#endif

/* the theme (visual.md's style table, firmware/ui_map.c), RGB565 */
static const ui_theme *T = &UI_MEMBRANE;
#define C_BG     (T->bg)
#define C_SURF   (T->surface)
#define C_SURFHI (T->surface_hi)
#define C_LINE   (T->line)
#define C_TEXT   (T->text)
#define C_DIM    (T->dim)
#define C_WINDOW (T->window)
#define C_VCF    (T->section[UI_PAGE_INFO[U->page].color])    /* the page's section color */
#define C_PLAY   (T->section[UI_SEC_PLAY])
#define M 5                                     /* the margin */
#define COL_W 56

/* a toward b by m / 256, per channel */
static uint16_t mix(uint16_t a, uint16_t b, uint32_t m)
{
    uint32_t r = ((a >> 11) * (256u - m) + (b >> 11) * m) >> 8;
    uint32_t g = ((a >> 5 & 63u) * (256u - m) + (b >> 5 & 63u) * m) >> 8;
    uint32_t bl = ((a & 31u) * (256u - m) + (b & 31u) * m) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* ---------------------------------------------------------------- drawing on a canvas */
static void frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    H->cv_rect(x, y, w, 1, c);
    H->cv_rect(x, y + h - 1, w, 1, c);
    H->cv_rect(x, y, 1, h, c);
    H->cv_rect(x + w - 1, y, 1, h, c);
}

static uint32_t glyph(char c)                   /* its index in DFONT_CHARS: lower case as upper */
{
    static const char CH[] = DFONT_CHARS;
    uint32_t i;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (i = 0; i < DFONT_N; i++)
        if (CH[i] == c)
            return i;
    return 0;
}

static int32_t d24_w(const char *s)
{
    int32_t w = 0;
    for (; *s; s++)
        w += D24_W[glyph(*s)] + D24_GAP;
    return w ? w - D24_GAP : 0;
}

/* the display font's D24 at x, y: each row's runs as one rect; returns the x after it */
static int32_t d24(int32_t x, int32_t y, const char *s, uint16_t c)
{
    for (; *s; s++) {
        uint32_t g = glyph(*s), w = D24_W[g], bpr = (w + 7u) / 8u, r, b;
        const uint8_t *row = &D24_DATA[D24_OFF[g]];
        for (r = 0; r < D24_H; r++, row += bpr)
            for (b = 0; b < w;) {
                uint32_t e;
                if (!(row[b >> 3] >> (7u - (b & 7u)) & 1u)) { b++; continue; }
                for (e = b; e < w && row[e >> 3] >> (7u - (e & 7u)) & 1u; e++) {}
                H->cv_rect(x + (int32_t)b, y + (int32_t)r, (int32_t)(e - b), 1, c);
                b = e;
            }
        x += (int32_t)w + D24_GAP;
    }
    return x - D24_GAP;
}

/* text in size big (0: 16 px, 1: 32 px), centered in w */
static void text_c(int32_t x, int32_t w, int32_t y, uint32_t big, const char *s, uint16_t c)
{
    CV_TEXT(x + (w - TEXT_W(big, s)) / 2, y, big, s, c);
}

/* two digits on 7-segment LEDs, 16 x 36, 4 px strokes, the unlit ones as a ghost */
static void seg7(int32_t x, int32_t y, const char *s, uint16_t on, uint16_t ghost)
{
    const int32_t dw = 16, dh = 36, t = 4, hh = (dh - 3 * t) / 2, j = 1;
    for (; *s; s++, x += dw + t + 2) {
        uint32_t lit = ui_seg7(*s), k;
        const int32_t R[7][4] = {
            {x + t + j, y, dw - 2 * t - 2 * j, t},                          /* a */
            {x + dw - t, y + t + j, t, hh - 2 * j},                          /* b */
            {x + dw - t, y + 2 * t + hh + j, t, hh - 2 * j},                 /* c */
            {x + t + j, y + 2 * t + 2 * hh, dw - 2 * t - 2 * j, t},          /* d */
            {x, y + 2 * t + hh + j, t, hh - 2 * j},                          /* e */
            {x, y + t + j, t, hh - 2 * j},                                   /* f */
            {x + t + j, y + t + hh, dw - 2 * t - 2 * j, t},                  /* g */
        };
        for (k = 0; k < 7u; k++)
            H->cv_rect(R[k][0], R[k][1], R[k][2], R[k][3], lit >> k & 1u ? on : ghost);
    }
}

/* ---------------------------------------------------------------- the bands */
/* Each band is one canvas or a few (the parts), gaps around it included, so nothing is drawn with
 * fill() (which waits for the panel). The panel takes a canvas's pixels while the main loop goes on;
 * blit() notes when they will be out, and draw() starts no part before then (cv_begin would wait). */
#define TICKS_PER_PX 32u                        /* 16 bits a pixel on SPI1 at ~12 MHz, 24 MHz ticks */
static uint32_t blit_t, blit_wait;

static void blit(int32_t x, int32_t y, int32_t w, int32_t h)
{
    H->cv_blit(x, y);
    blit_t = H->ticks();
    blit_wait = (uint32_t)(w * h) * TICKS_PER_PX;
}

/* rows 0..27: the header and the gap under it: the section name on a chip of its color, or under a
 * 3 px band in it (the theme's), then lead and tail dim around lit ("A/" "B" "": page B) */
static void header(const char *chip, uint16_t col, const char *lead, const char *lit, const char *tail,
                   int32_t page)
{
    char o[UI_VALUE_LEN];
    int32_t x, w;
    uint32_t i;
    H->cv_begin(240, 28, C_BG);
    if (T->chip) {
        x = M + TEXT_W(0, chip) + M;
        H->cv_rect(0, 0, x, 23, col);
        CV_TEXT(M, 4, 0, chip, T->ink);
        x += 6;
    } else {
        H->cv_rect(0, 0, 240, 3, col);
        CV_TEXT(M, 4, 0, chip, col);
        x = M + TEXT_W(0, chip) + 8;
    }
    CV_TEXT(x, 4, 0, lead, C_DIM);
    x += TEXT_W(0, lead);
    CV_TEXT(x, 4, 0, lit, C_TEXT);
    CV_TEXT(x + TEXT_W(0, lit), 4, 0, tail, C_DIM);
    ui_octave_label(P->oct, o);                  /* (HB_F_SCREEN hides the host's line, which had it) */
    CV_TEXT(240 - M - TEXT_W(0, o), 4, 0, o, P->oct ? C_TEXT : C_DIM);
    H->cv_rect(0, 23, 240, 1, C_LINE);
    if (page >= 0)
        for (i = 0; i < UI_PAGES; i++) {
            ui_strip(i, &x, &w);
            H->cv_rect(x, 22, w, 2, i == (uint32_t)page ? col : C_LINE);
        }
    blit(0, 0, 240, 28);
}

/* rows 222..239: the gap, the line and the footer: the patch's number and name and the voice
 * count, or for 1.5 s the banner (ui_banner) */
static void footer(void)
{
    char v[] = "N VOICES", n[UI_VALUE_LEN];
    const char *b = ui_banner(U);
    v[0] = (char)('0' + PLAY_VOICES % 10u);
    H->cv_begin(240, 18, C_SURF);
    H->cv_rect(0, 0, 240, 1, C_BG);
    H->cv_rect(0, 1, 240, 1, C_LINE);
    if (*b) {
        text_c(0, 240, 2, 0, b, C_PLAY);
    } else {
        ui_patch_number(play_patch(P), n);
        CV_TEXT(M, 2, 0, n, C_DIM);
        CV_TEXT(M + TEXT_W(0, n) + 6, 2, 0, play_patch(P)->name, C_TEXT);
        CV_TEXT(240 - M - TEXT_W(0, v), 2, 0, v, C_DIM);
    }
    blit(0, 222, 240, 18);
}

/* rows 28..77 */
static void focus_row(void)
{
    const ui_slot *s = ui_page_slot(U, U->focus);
    char num[3], val[UI_VALUE_LEN], unit[UI_VALUE_LEN];
    int32_t x;
    if (!s || s->kind == UI_RESERVED) {         /* (no focus: an empty row, never an empty slot's text) */
        H->cv_begin(240, 50, C_BG);
        blit(0, 28, 240, 50);
        return;
    }
    ui_slot_number(s, num);
    ui_value(s->id, ui_slot_value(U, s), val, unit);
    H->cv_begin(240, 50, C_BG);
    H->cv_rect(M, 0, 64, 46, C_WINDOW);
    frame(M, 0, 64, 46, C_LINE);
    seg7(M + (64 - 38) / 2, 5, num, T->led, mix(C_WINDOW, T->led, T->ghost));
    CV_TEXT(M + 72, 0, 0, UI_PAGE_INFO[U->page].section, C_VCF);
    CV_TEXT(M + 72 + TEXT_W(0, UI_PAGE_INFO[U->page].section) + 8, 0, 0, s->full, C_TEXT);
    x = d24(M + 72, 20, val, C_TEXT);
    if (*unit)
        CV_TEXT(x + 3, 28, 0, unit, C_DIM);
    blit(0, 28, 240, 50);
}

/* rows 78..137, in SLOT_PARTS strips across: the filter curve, +12 dB at the top, -36 at the
 * bottom, hatched under */
#define SLOT_PARTS 3u
static ui_curve curve;                          /* the curve being drawn: one cutoff and resonance */
static uint32_t wave, width;                    /* or the waveform: its wave and pulse width */
static uint32_t adsr[4];                        /* or the ADSR: a, d, r positions, s, per mille */

/* a time's place in the time table, per mille (the ADSR picture's stage widths) */
static uint32_t time_pos(int32_t ms)
{
    uint32_t i = 0, n = ui_time_count();
    while (i < n - 1u && ui_time_at(i + 1u) <= ms)
        i++;
    return 1000u * i / (n - 1u);
}
static void slot(uint32_t part)
{
    const int32_t w = 240 - 2 * M, h = 56, n = w - 4, pw = 240 / (int32_t)SLOT_PARTS;
    const int32_t x0 = (int32_t)part * pw;      /* the part's screen x */
    int32_t i, i0, i1, y, y0 = 0;
    const uint8_t col = UI_PAGE_INFO[U->page].color;
    uint16_t hatch = mix(C_WINDOW, C_VCF, T->hatch);
    if (part == 0u) {                           /* the values as they are now, for all three parts */
        ui_curve_begin(&curve, play_get_param(P, FM_PARAM_FILTER_CUTOFF_CENTS),
                       play_get_param(P, FM_PARAM_FILTER_RESONANCE), (uint32_t)n);
        wave = (uint32_t)play_get_param(P, U->page == 11u ? FM_PARAM_LFO_WAVE
                                           : col == UI_SEC_DCO2 ? FM_PARAM_OSC2_WAVE : FM_PARAM_OSC1_WAVE);
        width = (uint32_t)play_get_param(P, col == UI_SEC_DCO2 ? FM_PARAM_OSC2_PULSE_WIDTH_PCT
                                                               : FM_PARAM_OSC1_PULSE_WIDTH_PCT);
    }
    H->cv_begin(pw, 60, C_BG);
    H->cv_rect(M - x0, 0, w, h, C_WINDOW);      /* (the canvas clips what isn't this part's) */
    frame(M - x0, 0, w, h, C_LINE);
    if (U->page >= 12u && U->page <= 15u) {     /* 8A-9B: the envelope's ADSR (render.py env_points) */
        static const uint16_t ENV1[4] = {FM_PARAM_FILTER_ENV_ATTACK_MS, FM_PARAM_FILTER_ENV_DECAY_MS,
                                         FM_PARAM_FILTER_ENV_SUSTAIN, FM_PARAM_FILTER_ENV_RELEASE_MS};
        static const uint16_t ENV2[4] = {FM_PARAM_AMP_ATTACK_MS, FM_PARAM_AMP_DECAY_MS,
                                         FM_PARAM_AMP_SUSTAIN, FM_PARAM_AMP_RELEASE_MS};
        const uint16_t *e = U->page < 14u ? ENV1 : ENV2;
        if (part == 0u) {                       /* (the values once, for all three parts) */
            adsr[0] = time_pos(play_get_param(P, e[0]));
            adsr[1] = time_pos(play_get_param(P, e[1]));
            adsr[2] = (uint32_t)play_get_param(P, e[2]);
            adsr[3] = time_pos(play_get_param(P, e[3]));
        }
        i = x0 - M - 3;
        i0 = i = i < 0 ? 0 : i;
        i1 = x0 + pw - M - 2;
        i1 = i1 > n ? n : i1;
        for (; i < i1; i++) {
            int32_t xi = M + 2 + i - x0;
            y = h - 6 - ui_adsr_level(adsr[0], adsr[1], adsr[2], adsr[3], (uint32_t)i, (uint32_t)n) * (h - 12) / 1000;
            if (i > i0) {
                int32_t lo = y < y0 ? y : y0, hi = y < y0 ? y0 : y;
                H->cv_rect(xi - 1, lo, 2, hi - lo + 2, C_VCF);
            }
            y0 = y;
        }
        blit(x0, 78, pw, 60);
        return;
    }
    if (U->page < 4u || U->page == 11u) {       /* 1A-2B: the oscillator's waveform; 7A: the LFO's */
        H->cv_rect(M + 2 - x0, h / 2, w - 4, 1, C_LINE);
        i = x0 - M - 3;
        i0 = i = i < 0 ? 0 : i;
        i1 = x0 + pw - M - 2;
        i1 = i1 > n ? n : i1;
        for (; i < i1; i++) {
            int32_t xi = M + 2 + i - x0;
            y = h / 2 - (U->page == 11u ? ui_lfo_level(wave, (uint32_t)i, (uint32_t)n)
                                         : ui_wave_level(wave, width, (uint32_t)i, (uint32_t)n)) * (h / 2 - 8) / 1000;
            if (i > i0) {
                int32_t lo = y < y0 ? y : y0, hi = y < y0 ? y0 : y;
                H->cv_rect(xi - 1, lo, 2, hi - lo + 2, C_VCF);
            }
            y0 = y;
        }
        blit(x0, 78, pw, 60);
        return;
    }
    if (col != UI_SEC_VCF) {                    /* (a page without a picture yet: the window) */
        blit(x0, 78, pw, 60);
        return;
    }
    H->cv_rect(M + 2 - x0, 5 + 120 * (h - 8) / 480, w - 4, 1, C_LINE);
    i = x0 - M - 3;                             /* from the column before the part: the line joins */
    i0 = i = i < 0 ? 0 : i;
    i1 = x0 + pw - M - 2;
    i1 = i1 > n ? n : i1;
    for (; i < i1; i++) {
        int32_t d = ui_curve_db(&curve, (uint32_t)i), xi = M + 2 + i - x0;
        d = d > 120 ? 120 : d < -360 ? -360 : d;
        y = 5 + (120 - d) * (h - 8) / 480;
        if (T->hatch && !(i & 1))
            H->cv_rect(xi, y + 2, 1, h - 4 - y, hatch);
        if (i > i0) {
            int32_t lo = y < y0 ? y : y0, hi = y < y0 ? y0 : y;
            H->cv_rect(xi - 1, lo, 2, hi - lo + 2, C_VCF);
        }
        y0 = y;
    }
    blit(x0, 78, pw, 60);
}

/* rows 138..221: knob column k, with the gap on its left (and column 3 the margin on its right) */
static void column(uint32_t k)
{
    const ui_slot *s = ui_page_slot(U, k);
    const int32_t cx = k ? M - 2 + (int32_t)k * (COL_W + 2) : 0;    /* the canvas's screen x */
    const int32_t cw = (k == 3u ? 240 : M - 2 + (int32_t)(k + 1u) * (COL_W + 2)) - cx;
    const int32_t x = M + (int32_t)k * (COL_W + 2) - cx;            /* the column in the canvas */
    uint32_t act = k == U->focus, lit, i;
    uint16_t bg = act ? C_SURFHI : C_SURF, on;
    char val[UI_VALUE_LEN], unit[UI_VALUE_LEN], num[3];
    int32_t value, tw, x0;
    H->cv_begin(cw, 84, C_BG);
    H->cv_rect(x, 0, COL_W, 84, bg);
    if (s && s->kind != UI_RESERVED) {
        ui_slot_number(s, num);
        text_c(x, COL_W, 3, 0, num, C_VCF);
        text_c(x, COL_W, 19, 0, s->label, act ? C_TEXT : C_DIM);
        if (s->kind == UI_PENDING) {            /* (interaction.md: a pending parameter shows --) */
            d24(x + (COL_W - d24_w("--")) / 2, 52, "--", C_DIM);
        } else {
            uint32_t live = ui_slot_active(U, s);   /* inactive: dim, no meter (visual.md) */
            value = ui_slot_value(U, s);
            lit = ui_meter(s->id, value);
            on = act ? C_VCF : mix(bg, C_VCF, 154);
            if (!live) {
                /* no meter */
            } else if (s->order) {                     /* a choice: a segment per position, both themes */
                uint32_t n, at = ui_choice_pos(s, value, &n);
                for (i = 0; i < n; i++)
                    H->cv_rect(x + 6 + (int32_t)(i * 46u / n), 40, (int32_t)(46u / n) - 2, 6,
                               i == at ? on : C_LINE);
            } else if (T->meter_segments) {
                if (s->how == UI_HOW_SIZE || s->how == UI_HOW_TIME || s->how == UI_HOW_RATE) {   /* (from the left, as the bar) */
                    int32_t a, b;
                    ui_slot_bar(s, value, (int32_t)UI_METER_SEGS, &a, &b);
                    lit = (1u << b) - 1u;
                }
                for (i = 0; i < UI_METER_SEGS; i++)  /* 44 px: 8 segments of about 4, 2 px gaps */
                    H->cv_rect(x + 6 + ((int32_t)i * 23 + 2) / 4, 40, 4, 6, lit >> i & 1u ? on : C_LINE);
            } else {                            /* one bar in a 44 px track */
                int32_t a, b;
                ui_slot_bar(s, value, 44, &a, &b);
                H->cv_rect(x + 6, 40, 44, 6, C_LINE);
                H->cv_rect(x + 6 + a, 40, b - a, 6, on);
            }
            ui_column(s->id, value, val, unit);
            tw = d24_w(val) + (*unit ? 3 + TEXT_W(0, unit) : 0);
            if (tw > COL_W - 4) {               /* too wide: the unit goes first (the focus row has it) */
                *unit = 0;
                tw = d24_w(val);
            }
            if (tw > COL_W - 4) {               /* a word too wide for D24 (BOOST): Terminus */
                text_c(x, COL_W, 58, 0, val, act && live ? C_TEXT : C_DIM);
            } else {
                x0 = d24(x + (COL_W - tw) / 2, 52, val, act && live ? C_TEXT : C_DIM);
                if (*unit)
                    CV_TEXT(x0 + 1, 60, 0, unit, mix(C_SURF, act && live ? C_TEXT : C_DIM, 179));
            }
        }
        if (act)                                /* the focus: the theme's frames inward */
            for (i = 0; i < 3u && T->focus[i]; i++)
                frame(x + (int32_t)i, (int32_t)i, COL_W - 2 * (int32_t)i, 84 - 2 * (int32_t)i,
                      mix(C_BG, C_VCF, T->focus[i]));
    }
    blit(cx, 138, cw, 84);
}

/* ---------------------------------------------------------------- the screens, a part per call */
/* PLAY: the header and footer, then three strips (as the VCF page's bands, so the same bits) */
static void play_part(uint32_t b, uint32_t part)
{
    char s[PLAY_NOTE_NAME > UI_VALUE_LEN ? PLAY_NOTE_NAME : UI_VALUE_LEN];
    if ((b == UI_D_FULL && part == 1u) || b == UI_D_FOOT) {
        footer();
    } else if (b == UI_D_FULL || b == UI_D_HEAD) {
        header("PLAY", C_PLAY, "", "FM1-8P", "", -1);
    } else if (b == UI_D_FOCUS) {               /* rows 28..77: the patch's number, large */
        ui_patch_number(play_patch(P), s);
        H->cv_begin(240, 50, C_BG);
        text_c(0, 240, 12, 1, s, HB_RGB(255, 176, 64));
        blit(0, 28, 240, 50);
    } else if (b == UI_D_SLOT) {                /* rows 78..137: its name, large */
        H->cv_begin(240, 60, C_BG);
        text_c(0, 240, 0, 1, play_patch(P)->name, C_TEXT);
        text_c(0, 240, 40, 0, "ENV: VCF PAGE", C_DIM);
        blit(0, 78, 240, 60);
    } else if (b == UI_D_NOTE) {                /* rows 138..221: the last note */
        play_note_name(play_last_note(P), s);
        H->cv_begin(240, 84, C_BG);
        text_c(0, 240, 2, 0, "LAST NOTE", C_DIM);
        text_c(0, 240, 26, 1, s, 0xFFFFu);
        blit(0, 138, 240, 84);
    }
}

/* an edit page (UI_PAGE_INFO): "5 VCF" on the chip, then A/B when the section has two pages */
static void page_part(uint32_t b, uint32_t part)
{
    const ui_page_info *in = &UI_PAGE_INFO[U->page];
    uint32_t k;
    if ((b == UI_D_FULL && part == 1u) || b == UI_D_FOOT) {
        footer();
    } else if (b == UI_D_FULL || b == UI_D_HEAD) {
        char chip[16];
        int b_page = in->name[1] == 'B';
        int two = b_page || (U->page + 1u < UI_PAGES && UI_PAGE_INFO[U->page + 1u].name[0] == in->name[0]);
        for (k = 0; in->section[k] && k < sizeof chip - 3u; k++)
            chip[k + 2u] = in->section[k];
        chip[0] = in->name[0], chip[1] = ' ', chip[k + 2u] = 0;
        header(chip, C_VCF, b_page ? "A/" : "", two ? (b_page ? "B" : "A") : "", two && !b_page ? "/B" : "",
               (int32_t)U->page);
    } else if (b == UI_D_FOCUS) {
        focus_row();
    } else if (b == UI_D_SLOT) {
        slot(part);
    } else {
        for (k = 0; k < 4u; k++)
            if (b == UI_D_COL(k))
                column(k);
    }
}

static uint32_t parts(uint32_t b)
{
    return b == UI_D_FULL ? 2u : b == UI_D_SLOT && U->state == UI_EDIT ? SLOT_PARTS : 1u;
}

/* The host calls draw() from its main loop, between its key scans: one part a call, so a key never
 * waits behind more than one part (ui_draw_frame). */
static uint32_t pending;                        /* UI_D_* bands still to draw */
static uint32_t band, part;                     /* the band being drawn (0: none) and its next part */
#ifdef FM1_DEBUG
static uint32_t worst_call;                     /* the longest draw() call, in 24 MHz ticks */
#endif
void ui_draw_frame(uint32_t full)
{
#ifdef FM1_DEBUG
    uint32_t t0 = H->ticks();
    static uint32_t shown = ~0u;
    uint32_t now = (uint32_t)U->state << 16 | U->page << 8 | U->focus;
    if (trail_dead)
        return;                                 /* (a bad pointer was logged: drawing stops) */
    if (now != shown) {                         /* each change of state, page or focus, with the focus's label */
        shown = now;
        H->log_u32("fm1-8p TRAIL state/page/focus", now);
        H->log_u32("fm1-8p TRAIL focus label", (uint32_t)(uintptr_t)ui_page_slot(U, U->focus)->full);
    }
#endif
    if (full) {                                 /* the host's: all of it, now */
        pending = ui_take_full(U, H->ms());
        band = 0;
    }
    if (H->ticks() - blit_t < blit_wait)
        return;                                 /* the panel is still taking the last part */
    if (!band) {                                /* the next band: a frame's all go first (ui_band) */
        band = ui_band(U, &pending, H->ms());
        part = 0;
        if (!band)
            return;
    }
    if (U->state == UI_EDIT)
        page_part(band, part);
    else
        play_part(band, part);
    if (++part >= parts(band))
        band = 0;
#ifdef FM1_DEBUG
    if (H->ticks() - t0 > worst_call) {
        worst_call = H->ticks() - t0;
        H->log_u32("fm1-8p: worst draw call, ticks", worst_call);
    }
#endif
}

void ui_draw_init(const hb_host_t *h, play *p, ui *u)
{
    H = h;
    P = p;
    U = u;
    pending = band = part = 0;
    blit_t = blit_wait = 0;
}
