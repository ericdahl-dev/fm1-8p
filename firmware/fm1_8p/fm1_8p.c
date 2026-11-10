/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM1-8P: fm1-8p's engine as a playable synth under fm1-dock, on its default patch (firmware/play.c).
 *   keys   play notes with their velocity; the module owns the panel (HB_F_PANEL), so it keeps the
 *          octave (OCT- / OCT+, their LEDs lit off center as fm1-dock's own modules do), lights each
 *          key while it is held and sends the keys' notes out on MIDI channel 1
 *   MIDI   whatever the host takes in (USB, Bluetooth or the jack) comes through midi(), all alike:
 *          note on and off, sustain (CC 64), all sound / notes off (CC 120 / 123), pitch bend; any
 *          channel. With LOCAL OFF the host plays the keys through note(), on its own octave.
 *   pages  ENV opens page 4A VCF (docs/ui/interaction.md): KNOB1-4 set CUTOFF, RESO, ENV and KEY;
 *          EDIT goes back to PLAY. The keys play on every page.
 * The screen is the module's (HB_F_SCREEN), drawn in docs/ui/visual.md's Night Panel theme, and
 * only what changed is redrawn (firmware/ui.c says what), never once per audio block. */
#include "hb_module.h"
#include "play.h"
#include "ui.h"
#include "display_font.h"

static const hb_host_t *H;
static play P;                                  /* static: the engine's 5 KiB must not sit on a stack */
static ui U;
static int32_t drawn_note;
#define NOT_DRAWN (-2)                          /* drawn_note: the screen shows no note yet */

/* Night Panel (visual.md's style table), RGB565 */
#define C_BG     0x0863u
#define C_SURF   0x18C5u
#define C_SURFHI 0x2107u
#define C_LINE   0x2969u
#define C_TEXT   0xEF7Fu
#define C_DIM    0x9CB7u
#define C_WINDOW 0x0021u
#define C_VCF    0x2EDFu
#define C_PLAY   0xF9FBu
#define M 5                                     /* the margin */
#define COL_W 56

static void render(int32_t *out, uint32_t n) { play_render(&P, out, n); }

static void note(uint32_t ch, uint32_t key, uint32_t vel)
{
    (void)ch;
    play_note(&P, key, vel);
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2) { play_midi(&P, st, d1, d2); }

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

static uint32_t glyph(char c)                   /* its index in DFONT_CHARS */
{
    static const char CH[] = DFONT_CHARS;
    uint32_t i;
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

static void text_c(int32_t x, int32_t w, int32_t y, const char *s, uint16_t c)   /* centered in w */
{
    H->cv_text(x + (w - H->text_w(0, s)) / 2, y, 0, s, c);
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
static void header(const char *chip, uint16_t col, const char *a, const char *b, int32_t page)
{
    int32_t x, w;
    uint32_t i;
    H->cv_begin(240, 24, C_BG);
    H->cv_rect(0, 0, 240, 3, col);
    H->cv_text(M, 4, 0, chip, col);
    x = M + H->text_w(0, chip) + 8;
    H->cv_text(x, 4, 0, a, C_TEXT);
    H->cv_text(x + H->text_w(0, a), 4, 0, b, C_DIM);
    H->cv_rect(0, 23, 240, 1, C_LINE);
    if (page >= 0)
        for (i = 0; i < UI_PAGES; i++) {
            ui_strip(i, &x, &w);
            H->cv_rect(x, 22, w, 2, i == (uint32_t)page ? col : C_LINE);
        }
    H->cv_blit(0, 0);
}

static void footer(void)
{
    char v[] = "N VOICES";
    v[0] = (char)('0' + PLAY_VOICES % 10u);
    H->fill(0, 223, 240, 1, C_LINE);
    H->cv_begin(240, 16, C_SURF);
    H->cv_text(M, 0, 0, "FM1-8P", C_TEXT);
    H->cv_text(240 - M - H->text_w(0, v), 0, 0, v, C_DIM);
    H->cv_blit(0, 224);
}

static void focus_row(void)
{
    const ui_slot *s = ui_page_slot(&U, U.focus);
    char num[3], val[UI_VALUE_LEN], *unit = val;
    int32_t x;
    num[0] = (char)('0' + s->num / 10u), num[1] = (char)('0' + s->num % 10u), num[2] = 0;
    ui_format(s->id, play_get_param(&P, s->id), val);
    while (*unit && *unit != ' ')
        unit++;
    if (*unit)
        *unit++ = 0;
    H->cv_begin(240, 46, C_BG);
    H->cv_rect(M, 0, 64, 46, C_WINDOW);
    frame(M, 0, 64, 46, C_LINE);
    seg7(M + (64 - 38) / 2, 5, num, C_VCF, mix(C_WINDOW, C_VCF, 20));
    H->cv_text(M + 72, 0, 0, "VCF", C_VCF);
    H->cv_text(M + 72 + H->text_w(0, "VCF") + 8, 0, 0, s->full, C_TEXT);
    x = d24(M + 72, 20, val, C_TEXT);
    if (*unit)
        H->cv_text(x + 3, 28, 0, unit, C_DIM);
    H->cv_blit(0, 28);
}

/* the filter curve: +12 dB at the top, -36 at the bottom, hatched under */
static void slot(void)
{
    const int32_t w = 240 - 2 * M, h = 56, n = w - 4;
    int32_t cut = play_get_param(&P, FM_PARAM_FILTER_CUTOFF_CENTS);
    ui_curve c;
    int32_t res = play_get_param(&P, FM_PARAM_FILTER_RESONANCE), i, y0 = 0;
    uint16_t hatch = mix(C_WINDOW, C_VCF, 41);
    ui_curve_begin(&c, cut, res, (uint32_t)n);
    H->cv_begin(w, h, C_WINDOW);
    frame(0, 0, w, h, C_LINE);
    H->cv_rect(2, 5 + 120 * (h - 8) / 480, w - 4, 1, C_LINE);
    for (i = 0; i < n; i++) {
        int32_t d = ui_curve_db(&c, (uint32_t)i), y;
        d = d > 120 ? 120 : d < -360 ? -360 : d;
        y = 5 + (120 - d) * (h - 8) / 480;
        if (!(i & 1))
            H->cv_rect(2 + i, y + 2, 1, h - 4 - y, hatch);
        if (i) {
            int32_t lo = y < y0 ? y : y0, hi = y < y0 ? y0 : y;
            H->cv_rect(2 + i - 1, lo, 2, hi - lo + 2, C_VCF);
        }
        y0 = y;
    }
    H->cv_blit(M, 78);
}

static void column(uint32_t k)
{
    const ui_slot *s = ui_page_slot(&U, k);
    const int32_t x = M + (int32_t)k * (COL_W + 2);
    uint32_t act = k == U.focus, lit, i;
    uint16_t bg = act ? C_SURFHI : C_SURF, on;
    char num[3], val[UI_VALUE_LEN], unit[UI_VALUE_LEN];
    int32_t value, tw, x0;
    H->cv_begin(COL_W, 84, bg);
    if (!s || s->kind == UI_RESERVED) {
        H->cv_blit(x, 138);
        return;
    }
    num[0] = (char)('0' + s->num / 10u), num[1] = (char)('0' + s->num % 10u), num[2] = 0;
    text_c(0, COL_W, 3, num, C_VCF);
    text_c(0, COL_W, 19, s->label, act ? C_TEXT : C_DIM);
    if (s->kind == UI_PENDING) {                /* (interaction.md: a pending parameter shows --) */
        x0 = (COL_W - d24_w("--")) / 2;
        d24(x0, 52, "--", C_DIM);
    } else {
        value = play_get_param(&P, s->id);
        lit = ui_meter(s->id, value);
        on = act ? C_VCF : mix(bg, C_VCF, 154);
        for (i = 0; i < UI_METER_SEGS; i++)     /* 44 px: 8 segments of about 4, 2 px gaps */
            H->cv_rect(6 + ((int32_t)i * 23 + 2) / 4, 40, 4, 6, lit >> i & 1u ? on : C_LINE);
        ui_column(s->id, value, val, unit);
        tw = d24_w(val) + (*unit ? 3 + H->text_w(0, unit) : 0);
        x0 = d24((COL_W - tw) / 2, 52, val, act ? C_TEXT : C_DIM);
        if (*unit)
            H->cv_text(x0 + 1, 60, 0, unit, mix(C_SURF, act ? C_TEXT : C_DIM, 179));
    }
    if (act) {                                  /* the glow: three frames inward */
        frame(0, 0, COL_W, 84, C_VCF);
        frame(1, 1, COL_W - 2, 82, mix(C_BG, C_VCF, 115));
        frame(2, 2, COL_W - 4, 80, mix(C_BG, C_VCF, 51));
    }
    H->cv_blit(x, 138);
}

/* ---------------------------------------------------------------- the screens */
static void play_screen(uint32_t full)
{
    char s[PLAY_NOTE_NAME];
    int32_t last = play_last_note(&P);
    if (full) {
        H->fill(0, 24, 240, 199, 0);
        header("PLAY", C_PLAY, "FM1-8P", "", -1);
        H->text(0, 48, 240, 1, "FM1-8P", HB_RGB(255, 176, 64), 1);
        H->text(0, 92, 240, 0, "ENV: VCF PAGE", C_DIM, 1);
        H->text(0, 140, 240, 0, "LAST NOTE", C_DIM, 1);
        footer();
        drawn_note = NOT_DRAWN;
    }
    if (last != drawn_note) {                   /* only when it changed: never per audio block */
        drawn_note = last;
        play_note_name(last, s);
        H->fill(0, 164, 240, 36, 0);
        H->text(0, 164, 240, 1, s, 0xFFFFu, 1);
    }
}

static void vcf_screen(uint32_t d)
{
    uint32_t k;
    if (d & UI_D_FULL) {
        uint32_t g;
        /* only the gaps between the bands: a fill of the whole screen cost ~75 ms in fm1-emu */
        static const uint8_t GAP[][4] = {
            {0, 24, 240, 4}, {0, 74, 240, 4}, {0, 134, 240, 4}, {0, 222, 240, 1},   /* between rows */
            {0, 78, 5, 56}, {235, 78, 5, 56},                                        /* beside the slot */
            {0, 138, 5, 84}, {61, 138, 2, 84}, {119, 138, 2, 84}, {177, 138, 2, 84}, /* the columns' */
            {235, 138, 5, 84},
        };
        for (g = 0; g < sizeof GAP / sizeof GAP[0]; g++)
            H->fill(GAP[g][0], GAP[g][1], GAP[g][2], GAP[g][3], C_BG);
        header("4 VCF", C_VCF, "A", "/B", (int32_t)UI_PAGE_4A);
        footer();
        d = ~0u;
    }
    if (d & UI_D_FOCUS)
        focus_row();
    if (d & UI_D_SLOT)
        slot();
    for (k = 0; k < 4u; k++)
        if (d & UI_D_COL(k))
            column(k);
}

/* ---------------------------------------------------------------- panel */
/* OCT- / OCT+: lit off center; an SMK-37's outer keys go on from key 0's note */
static void octave_shown(void)
{
    H->led(HB_BTN_OCT_DOWN, play_oct_led(&P, PLAY_OCT_DOWN));
    H->led(HB_BTN_OCT_UP, play_oct_led(&P, PLAY_OCT_UP));
    if (HB_HOST_HAS(H, key_base))
        H->key_base((uint32_t)((int32_t)PLAY_KEY0 + 12 * P.oct));
}

static void page_leds(void)
{
    H->led(HB_BTN_ENV, U.state == UI_EDIT);
    H->led(HB_BTN_EDIT, U.state == UI_EDIT);
}

static void ui_ev(uint32_t ev, int32_t arg)
{
    uint32_t id = HB_EV_ID(ev), kind = HB_EV_KIND(ev);
    ui_state was = U.state;
    if ((kind == HB_EV_KEY_DOWN || kind == HB_EV_KEY_UP) && id >= PLAY_KEYS)
        return;
    switch (kind) {
    case HB_EV_KEY_DOWN:                        /* arg: the velocity, 0 an unsensed key */
        play_key_down(&P, id, (uint32_t)arg);
        H->led(HB_LED_KEY(id), 1);
        H->midi_send(0x90u, P.held[id], arg > 0 ? (uint32_t)arg : 100u);
        return;
    case HB_EV_KEY_UP:
        play_key_up(&P, id);
        H->led(HB_LED_KEY(id), 0);
        H->midi_send(0x80u, P.held[id], 0);
        return;
    case HB_EV_BTN_DOWN:
        if (id == HB_BTN_OCT_DOWN || id == HB_BTN_OCT_UP) {
            play_octave(&P, id == HB_BTN_OCT_UP ? 1 : -1);
            octave_shown();
            return;
        }
        break;
    default: break;
    }
    ui_event(&U, ev, arg, H->ms());             /* pages and knobs */
    if (U.state != was)
        page_leds();
}

static void draw(uint32_t full)
{
    uint32_t d = ui_take(&U, H->ms());
    if (full) {
        d |= UI_D_FULL;
        octave_shown();                         /* (back from the picker, which had the LEDs) */
        page_leds();
    }
    if (U.state == UI_EDIT) {
        uint32_t t0 = H->ticks();
        vcf_screen(d);
        if (d & UI_D_FULL)                      /* (a page's cost, in 24 MHz ticks: once per page) */
            H->log_u32("fm1-8p: VCF page drawn, ticks", H->ticks() - t0);
    } else
        play_screen((d & UI_D_FULL) != 0);
}

static int init(const hb_host_t *h)
{
    H = h;
    drawn_note = NOT_DRAWN;
    if (play_init(&P) != 0) {
        H->log("fm1-8p: engine init failed");
        return 1;                               /* the host falls back to the sine */
    }
    ui_init(&U, &P);
    octave_shown();
    page_leds();
    H->log_u32("fm1-8p: ready, voices", PLAY_VOICES);
    return 0;
}

/* no release(): init() starts afresh; no cc(): midi() takes every channel message */
HB_DESC = {HB_ABI, "FM1-8P", init, 0, note, 0, render, ui_ev, draw, 0, 0, HB_F_SCREEN | HB_F_PANEL, midi};
