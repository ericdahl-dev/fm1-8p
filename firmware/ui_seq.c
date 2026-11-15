/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_seq.h"

static void banner(ui *u, const char *s, uint32_t ms)
{
    u->banner = s;
    u->banner_ms = ms;
    u->dirty |= UI_D_FOOT;
}

void ui_chord_note(ui *u, uint32_t note, uint32_t vel, int on)
{
    uint32_t i, j;
    for (i = 0; i < u->chord_n && u->chord[i] != note; i++)
        ;
    if (on && i == u->chord_n && u->chord_n < UI_CHORD_MAX) {
        u->chord[u->chord_n] = (uint8_t)note;
        u->chord_vel[u->chord_n++] = (uint8_t)vel;
    } else if (!on && i < u->chord_n) {        /* (let go: the later ones move up) */
        for (j = i; j + 1u < u->chord_n; j++)
            u->chord[j] = u->chord[j + 1u], u->chord_vel[j] = u->chord_vel[j + 1u];
        u->chord_n--;
    }
    if (u->state == UI_SEQ)
        u->dirty |= UI_D_FOCUS;                 /* (the chord held, on the screen) */
}

void ui_seq_step_key(ui *u, uint32_t k, int down, uint32_t ms)
{
    seq *s = play_seq(u->p);
    uint32_t i, n = u->chord_n < PLAY_VOICES ? u->chord_n : PLAY_VOICES, vel = 0;
    if (!down)
        return;
    if (n) {                                    /* a chord held: written there (the first notes pressed) */
        for (i = 0; i < n; i++)
            vel = u->chord_vel[i] > vel ? u->chord_vel[i] : vel;
        seq_set_step(s, k, u->chord, n, vel);
        if (u->chord_n > PLAY_VOICES) {
            static const char MAX[] = "MAX 0 NOTES";    /* (the voice count: one digit) */
            for (i = 0; i < sizeof MAX; i++)
                u->seq_msg[i] = MAX[i];
            u->seq_msg[4] = (char)('0' + PLAY_VOICES % 10u);
            banner(u, u->seq_msg, ms);
        }
    }
    u->seq_cursor = k;
    u->dirty |= UI_D_SLOT | UI_D_FOCUS;
}

void ui_seq_open(ui *u)
{
    u->seq_from = u->state;
    u->state = UI_SEQ;
    u->readout = 0;
    u->warned = 0;
    u->focus = 4u;
    u->dirty |= UI_D_FULL;
}

void ui_seq_transport(ui *u)
{
    seq *s = play_seq(u->p);
    if (s->want)
        seq_stop(s);
    else
        seq_play(s);
    u->dirty |= UI_D_HEAD;
}

const char *const UI_SEQ_PAGE_NAME[UI_SEQ_PAGES] = {"S1 PATTERN", "S2 STEP", "S3 PLAY"};
const ui_seq_slot UI_SEQ_SLOTS[UI_SEQ_PAGES][4] = {
    {{"TEMPO", "TEMPO", 1}, {"LENGTH", "LENGTH", 1}, {"SWING", "SWING", 1}, {"GATE", "GATE", 1}},
    {{"VEL", "VELOCITY", 1}, {"BEND", "BEND", 0}, {"MOD", "MOD", 0}, {"MOVE", "MOVE", 0}},   /* (#42) */
    {{"TRANSP", "TRANSPOSE", 1}, {"KEYTR", "KEY TRANSPOSE", 0}, {0, 0, 0}, {0, 0, 0}},
};

int32_t ui_seq_value(const ui *u, uint32_t k)
{
    seq *s = play_seq(u->p);
    const seq_step *st = seq_get_step(s, u->seq_cursor);
    switch (u->seq_page * 4u + k) {
    case 0: return (int32_t)seq_tempo(s);
    case 1: return (int32_t)seq_length(s);
    case 2: return s->edit.swing_pct;
    case 3: return s->edit.gate_pct;
    case 4: return st->kind == SEQ_NOTES_ON ? st->vel : 0;
    case 8: return seq_transpose(s);
    default: return 0;
    }
}

/* knob k on the SEQ page shown, clicks (3 or more at once: fast) */
static void turn(ui *u, uint32_t k, int32_t clicks)
{
    seq *s = play_seq(u->p);
    int32_t fast = clicks >= 3 || clicks <= -3, v = ui_seq_value(u, k);
    const seq_step *st;
    uint8_t notes[SEQ_NOTES];
    uint32_t i;
    if (!UI_SEQ_SLOTS[u->seq_page][k].live)
        return;
    switch (u->seq_page * 4u + k) {
    case 0:                                     /* (the footer shows it too) */
        v += clicks * (fast ? 100 : 10);
        seq_set_tempo(s, (uint32_t)(v < 0 ? 0 : v));
        u->dirty |= UI_D_FOOT;
        break;
    case 1: v += clicks; seq_set_length(s, (uint32_t)(v < 1 ? 1 : v)); break;
    case 2: v += clicks * (fast ? 5 : 1); seq_set_swing(s, (uint32_t)(v < 0 ? 0 : v)); break;
    case 3: v += clicks * (fast ? 5 : 1); seq_set_gate(s, (uint32_t)(v < 0 ? 0 : v)); break;
    case 4:                                     /* the cursor step's velocity: a note or chord only */
        st = seq_get_step(s, u->seq_cursor);
        if (st->kind != SEQ_NOTES_ON)
            return;
        for (i = 0; i < st->n; i++)
            notes[i] = st->note[i];
        v += clicks * (fast ? 10 : 1);
        seq_set_step(s, u->seq_cursor, notes, st->n, (uint32_t)(v < 1 ? 1 : v > 127 ? 127 : v));
        break;
    case 8: v += clicks; seq_set_transpose(s, v); break;
    default: return;
    }
    if (u->seq_cursor >= seq_length(s))        /* (a shorter pattern: the cursor inside it) */
        u->seq_cursor = seq_length(s) - 1u;
    u->focus = k;
    u->dirty |= UI_D_COL(0) | UI_D_COL(1) | UI_D_COL(2) | UI_D_COL(3) | UI_D_FOCUS | UI_D_SLOT;
}

#define UI_PATTERNS 64u                 /* pattern slots 11..88 */
static char *put_slot(char *s, uint32_t q);
static char *put_s(char *s, const char *t);

/* PRESETS in SEQ view (interaction.md 5, Pattern slots): the next or previous pattern, around (a fast
 * turn: a row, 8). Stopped it loads at once, playing it is queued for the end of the pass (back to the
 * running one: none queued). With PATTERN EDITED the first click only warns, as for patches. */
static void browse(ui *u, int32_t clicks, uint32_t ms)
{
    const seq *s = play_seq(u->p);
    int32_t way = clicks > 0 ? 1 : -1, from = play_pattern_next(u->p), q;
    if (seq_edited(s) && !(u->warned == way && ms - u->warned_ms < UI_DISCARD_MS)) {
        banner(u, "PTN EDITED - TURN TO DISCARD", ms);   /* the first click loads nothing */
        u->warned = way;
        u->warned_ms = ms;
        return;
    }
    u->warned = 0;
    if (clicks >= 3 || clicks <= -3)
        clicks = clicks > 0 ? 8 : -8;
    if (from < 0)                               /* (on from the queued one, if there is one) */
        from = (int32_t)play_pattern_prog(u->p);
    q = ((from + clicks) % (int32_t)UI_PATTERNS + (int32_t)UI_PATTERNS) % (int32_t)UI_PATTERNS;
    play_load_pattern(u->p, (uint32_t)q);
    if (s->want && play_pattern_next(u->p) >= 0) {   /* queued: say which */
        put_slot(put_s(u->seq_msg, "NEXT PATTERN "), (uint32_t)q);
        banner(u, u->seq_msg, ms);
    } else {
        u->banner = "";
    }
    if (u->seq_cursor >= seq_length(s))
        u->seq_cursor = 0;
    u->dirty |= (UI_D_ALL & ~UI_D_FULL) | UI_D_FOOT;
}

void ui_seq_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms)
{
    seq *s = play_seq(u->p);
    uint32_t id = ev & 0xFFu, i, len;
    int tap = (ev & 0xFF000000u) == UI_EV_BTN_UP && id < UI_BUTTONS && ms - u->down_ms[id] < UI_TAP_MS;
    if (ev == (UI_EV_BTN_UP | UI_BTN_SEQ) && tap) {          /* back, on the page it left */
        u->state = u->seq_from;
        u->dirty |= UI_D_FULL;
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_HOME) && tap) {
        u->state = UI_PLAY;
        u->dirty |= UI_D_FULL;
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_EDIT) && tap) {  /* the last edit page */
        u->state = UI_EDIT;
        u->dirty |= UI_D_FULL;
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_SEL)) {
        if (ms - u->down_ms[UI_BTN_SEL] >= UI_SEQ_CLEAR_MS) {   /* held: the whole pattern */
            for (i = 0; i < SEQ_STEPS; i++)
                seq_clear_step(s, i);
        } else if (tap) {
            seq_clear_step(s, u->seq_cursor);   /* the cursor step: a rest */
        }
        u->dirty |= UI_D_SLOT | UI_D_FOCUS | UI_D_COL(0);
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_ENV) && tap) {
        seq_toggle_tie(s, u->seq_cursor);
        u->dirty |= UI_D_SLOT | UI_D_FOCUS | UI_D_COL(0);
    } else if (ev == (UI_EV_ENC | UI_ENC_SELECT) && arg) {  /* the cursor, around the length */
        len = seq_length(s);
        u->seq_cursor = (uint32_t)(((int32_t)(u->seq_cursor % len) + arg % (int32_t)len + (int32_t)len) %
                                   (int32_t)len);
        u->dirty |= UI_D_SLOT | UI_D_FOCUS | UI_D_COL(0);
    } else if (ev == (UI_EV_ENC | UI_ENC_ALGORITHM) && arg) {   /* the SEQ page, around */
        u->seq_page = (uint32_t)(((int32_t)u->seq_page + arg % (int32_t)UI_SEQ_PAGES + (int32_t)UI_SEQ_PAGES) %
                                 (int32_t)UI_SEQ_PAGES);
        u->focus = 4u;
        u->dirty |= UI_D_FULL;
    } else if (ev == (UI_EV_ENC | UI_ENC_PRESETS) && arg) {
        browse(u, arg, ms);
    } else if ((ev & 0xFF000000u) == UI_EV_ENC && id >= UI_ENC_KNOB1 && id <= UI_ENC_KNOB4 && arg) {
        turn(u, id - UI_ENC_KNOB1, arg);
    }
}

uint32_t ui_key_led(const ui *u, uint32_t k, int down)
{
    const seq *s = play_seq(u->p);
    const seq_step *st;
    int32_t at = seq_step_now(s);
    uint32_t led;
    if (u->state != UI_SEQ) {                   /* the playhead's key while playing; the keypad; held keys */
        if (ui_keypad_led(u, k) == UI_LED_GLOW)
            return UI_LED_GLOW;
        return (down && u->set.key_leds) || (s->want && at >= 0 && (uint32_t)at == k) ? UI_LED_LIT
                                                                                       : UI_LED_OFF;   /* (G33) */
    }
    if (k >= UI_STEP_KEYS)                      /* a chord key: the play zone glows, lit while held */
        return down ? UI_LED_LIT : UI_LED_GLOW;
    if (k >= seq_length(s))
        return UI_LED_OFF;
    st = seq_get_step(s, k);
    led = st->kind == SEQ_REST ? UI_LED_GLOW : UI_LED_LIT;
    if (st->kind == SEQ_TIE && u->now_ms % 500u >= 400u)    /* a tie: a short dark blink every 500 ms */
        led = UI_LED_OFF;
    if (s->want && at >= 0) {                   /* playing: the playhead, its step inverted */
        if ((uint32_t)at == k)
            led = st->kind == SEQ_REST ? UI_LED_LIT : UI_LED_OFF;
    } else if (k == u->seq_cursor) {            /* stopped: the cursor blinks lit / glow at 4 Hz */
        led = u->now_ms / 125u & 1u ? UI_LED_GLOW : UI_LED_LIT;
    }
    return led;
}

/* s and then the unsigned number v at the end of it; returns the new end */
static char *put_u(char *s, uint32_t v)
{
    char d[10];
    uint32_t n = 0;
    do {
        d[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *s++ = d[--n];
    *s = 0;
    return s;
}

static char *put_s(char *s, const char *t)
{
    while (*t)
        *s++ = *t++;
    *s = 0;
    return s;
}

void ui_seq_column(const ui *u, uint32_t k, char *num, char *unit)
{
    int32_t v = ui_seq_value(u, k);
    char *n = num;
    *num = 0, *unit = 0;
    if (!UI_SEQ_SLOTS[u->seq_page][k].label)
        return;
    if (!UI_SEQ_SLOTS[u->seq_page][k].live) {
        put_s(num, "--");
        return;
    }
    switch (u->seq_page * 4u + k) {
    case 0:                                     /* tenths of a BPM: the tenth only when there is one */
        n = put_u(n, (uint32_t)v / 10u);
        if (v % 10)
            *n++ = '.', put_u(n, (uint32_t)v % 10u);
        put_s(unit, "BPM");
        break;
    case 2:
    case 3:
        put_u(n, (uint32_t)v);
        put_s(unit, "%");
        break;
    case 4:                                     /* VEL: a rest or a tie has none */
        if (v)
            put_u(n, (uint32_t)v);
        else
            put_s(n, "--");
        break;
    case 8:                                     /* TRANSPOSE: signed, in semitones */
        if (v > 0)
            *n++ = '+';
        else if (v < 0)
            *n++ = '-';
        put_u(n, (uint32_t)(v < 0 ? -v : v));
        put_s(unit, "st");
        break;
    default:
        put_u(n, (uint32_t)v);
    }
}

/* notes into s by name, a space between */
static void note_names(const uint8_t *note, uint32_t n, char *s)
{
    uint32_t i;
    char name[PLAY_NOTE_NAME];
    *s = 0;
    for (i = 0; i < n; i++) {
        play_note_name(note[i], name);
        if (i)
            s = put_s(s, " ");
        s = put_s(s, name);
    }
}

void ui_seq_step_text(const ui *u, uint32_t i, char *s)
{
    const seq_step *st = seq_get_step(play_seq(u->p), i);
    if (st->kind == SEQ_TIE)
        put_s(s, "TIE");
    else if (st->kind != SEQ_NOTES_ON || !st->n)
        put_s(s, "REST");
    else
        note_names(st->note, st->n, s);
}

void ui_seq_chord_text(const ui *u, char *s)
{
    note_names(u->chord, u->chord_n < PLAY_VOICES ? u->chord_n : PLAY_VOICES, s);
}

/* a pattern slot (0..63) as the panel numbers it, 11..88 */
static char *put_slot(char *s, uint32_t q)
{
    *s++ = (char)('1' + q / 8u % 8u);
    *s++ = (char)('1' + q % 8u);
    *s = 0;
    return s;
}

void ui_seq_footer_text(const ui *u, char *left, char *right)
{
    char name[STORE_NAME_LEN + 1u];
    char *s = put_s(left, "PTN ");
    int32_t next = play_pattern_next(u->p);
    s = put_slot(s, play_pattern_prog(u->p));
    play_pattern_name(u->p, name);
    if (*name) {
        s = put_s(s, " ");
        s = put_s(s, name);
    }
    if (seq_edited(play_seq(u->p)))
        put_s(s, "*");
    *right = 0;
    if (next >= 0)
        put_slot(put_s(right, "NEXT "), (uint32_t)next);
}
