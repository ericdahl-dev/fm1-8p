/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_seq.h"

static void banner(ui *u, const char *s, uint32_t ms)
{
    u->banner = s;
    u->banner_ms = ms;
    u->dirty |= UI_D_FOOT;
}

/* the roll's columns in mask redraw (with the slot band they're in) */
static void roll(ui *u, uint32_t mask)
{
    u->roll_dirty |= mask & 0xFFFFu;
    u->dirty |= UI_D_SLOT;
}
#define ROLL_ALL 0xFFFFu
#define ROLL_AT(i) (1u << ((i) & 15u))

/* the notes played since the step was tapped, into the cursor step (the first PLAY_VOICES pressed, at the
 * highest velocity; MAX n NOTES when more were); the cursor on to the next, and disarmed */
static void write_step(ui *u)
{
    seq *s = play_seq(u->p);
    uint32_t i, n = u->built_n < PLAY_VOICES ? u->built_n : PLAY_VOICES, vel = 0;
    for (i = 0; i < n; i++)
        vel = u->built_vel[i] > vel ? u->built_vel[i] : vel;
    seq_write_step(s, u->seq_cursor, u->built, n, vel,     /* with the bend and mod of its first note (#42) */
                   n ? u->built_bend : 0, n ? (int32_t)u->built_mod : 0, 0);
    if (u->built_n > PLAY_VOICES) {
        static const char MAX[] = "MAX 0 NOTES";    /* (the voice count: one digit) */
        for (i = 0; i < sizeof MAX; i++)
            u->seq_msg[i] = MAX[i];
        u->seq_msg[4] = (char)('0' + PLAY_VOICES % 10u);
        banner(u, u->seq_msg, u->now_ms);
    }
    u->seq_cursor = (u->seq_cursor + 1u) % seq_length(s);
    u->seq_armed = 0;
    u->built_n = 0;
    roll(u, ROLL_ALL);                          /* (a written step can change the ties after it) */
    u->dirty |= UI_D_SLOT | UI_D_FOCUS | UI_D_COL(0) | UI_D_COL(1) | UI_D_COL(2) | UI_D_COL(3);
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
    if (u->state == UI_SEQ && (u->seq_armed || u->seq_rec)) {   /* armed or REC: to the cursor step */
        for (i = 0; i < u->built_n && u->built[i] != note; i++)
            ;
        if (on && i == u->built_n && u->built_n < UI_CHORD_MAX) {
            if (!u->built_n)                    /* the first note: the bend and mod it's written with (#42) */
                u->built_bend = u->live_bend, u->built_mod = u->live_mod;
            u->built[u->built_n] = (uint8_t)note;
            u->built_vel[u->built_n++] = (uint8_t)vel;
            roll(u, ROLL_AT(u->seq_cursor));    /* (the note shows at 45 % as it goes down) */
        } else if (!on && !u->chord_n && u->built_n) {
            write_step(u);                      /* all let go: written, on to the next step */
        }
    }
    if (u->state == UI_SEQ)
        u->dirty |= UI_D_FOCUS;                 /* (the chord held, on the screen) */
}

/* KEY TRANSPOSE taking played keys now: on, and the sequencer playing */
static int ktr_active(const ui *u)
{
    const seq *s = play_seq(u->p);
    return seq_key_transpose(s) && s->want && s->playing;   /* (playing, not waiting for a clock; a STOP press:
                                                             * keys play at once) */
}

/* the transpose from the lowest key or MIDI note held for it, as a pitch: C4 (60) is 0 */
static void ktr_set(ui *u)
{
    uint32_t i, low = 128u;
    for (i = 0; i < PLAY_KEYS && low == 128u; i++)
        if (u->ktr_keys >> i & 1u)
            low = PLAY_KEY0 + i;
    for (i = 0; i < 128u && i < low; i++)
        if (u->ktr_note_n[i] & 15u) {
            low = i;
            break;
        }
    if (low < 128u) {
        seq_set_transpose(play_seq(u->p), (int32_t)low - 60);   /* (clamped there to -24..+24) */
        if (u->state == UI_SEQ && u->seq_page == 2u)
            u->dirty |= UI_D_COL(0);            /* (S3 shows TRANSPOSE) */
    }
}

int ui_seq_ktr_key(ui *u, uint32_t k, int down, int may)
{
    if (k >= PLAY_KEYS)
        return 0;
    if (!down) {
        if (!(u->ktr_keys >> k & 1u))
            return 0;
        u->ktr_keys &= ~(1u << k);              /* (let go: the transpose stays) */
        return 1;
    }
    if (!may || u->state == UI_SEQ || !ktr_active(u))
        return 0;                               /* (SEQ view: the panel keys are steps, or play the armed step) */
    u->ktr_keys |= 1u << k;
    ktr_set(u);
    return 1;
}

int ui_seq_ktr_note(ui *u, uint32_t note, uint32_t vel, int on)
{
    uint8_t *c;
    if (note > 127u)
        return 0;
    c = &u->ktr_note_n[note];
    if (!on || !vel) {
        if (!(*c >> 4))
            return 0;                           /* (not one of ours: to the engine) */
        *c = (uint8_t)(*c - 16u - ((*c & 15u) ? 1u : 0u));   /* an off owed, and one held fewer */
        return 1;                               /* (let go: the transpose stays) */
    }
    if (!ktr_active(u) || (u->state == UI_SEQ && (u->seq_armed || u->seq_rec))) {
        *c = 0;                                 /* (it plays: an old taken one whose off was lost lets go) */
        return 0;                               /* (a step being written takes it: chord entry first) */
    }
    if ((*c & 15u) < 15u && (*c >> 4) < 15u)
        *c = (uint8_t)(*c + 17u);               /* one more held, one more off owed (15 at most) */
    ktr_set(u);
    return 1;
}

void ui_seq_ktr_all_off(ui *u)
{
    uint32_t i;
    for (i = 0; i < 128u; i++)                  /* none held for the transpose now; their offs, if they */
        u->ktr_note_n[i] &= 0xF0u;              /* come, are still taken (the engine never had them) */
}

int32_t ui_seq_step_of_key(uint32_t k)
{
    static const int8_t STEP[27] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1,
                                    12, -1, 13, 14, -1, 15};
    return k < 27u ? STEP[k] : -1;
}

void ui_seq_step_key(ui *u, uint32_t k, int down, uint32_t ms)
{
    int32_t st = ui_seq_step_of_key(k);
    if (!down || st < 0 || (uint32_t)st >= seq_length(play_seq(u->p)))
        return;                                 /* (a black key, or a step past the length: nothing) */
    roll(u, ROLL_AT(u->seq_cursor) | ROLL_AT(st));
    u->seq_cursor = (uint32_t)st;               /* the cursor there, armed: the notes played next go to it */
    u->seq_armed = 1;
    u->seq_arm_ms = ms;
    u->built_n = 0;
    u->dirty |= UI_D_SLOT | UI_D_FOCUS | UI_D_COL(0) | UI_D_COL(1) | UI_D_COL(2) | UI_D_COL(3);
}

void ui_seq_open(ui *u)
{
    u->seq_from = u->state;
    u->state = UI_SEQ;
    u->seq_armed = u->built_n = 0;
    u->seq_rec = 0;
    u->roll_pps = 0;                            /* (fitted anew) */
    u->roll_dirty = ROLL_ALL;
    u->shown_playhead = -1;
    u->readout = 0;
    u->warned = 0;
    u->focus = 4u;
    u->dirty |= UI_D_FULL;
}

void ui_seq_rec(ui *u)
{
    u->seq_rec = !u->seq_rec;
    u->seq_armed = 0;                           /* (one arming at a time: REC takes over from a tapped step) */
    u->built_n = 0;                             /* (a step part-built when it disarms: dropped) */
    roll(u, ROLL_AT(u->seq_cursor));
    u->dirty |= UI_D_HEAD | UI_D_FOCUS | UI_D_FOOT;   /* (the chip, the readout, the legend and key map) */
}

void ui_seq_transport(ui *u)
{
    seq *s = play_seq(u->p);
    if (s->want)
        seq_stop(s);
    else
        seq_play(s);
    if (u->seq_rec) {                           /* PLAY in REC: it plays, and REC disarms (interaction.md 5) */
        u->seq_rec = 0;
        u->built_n = 0;
        u->dirty |= UI_D_FOCUS | UI_D_FOOT;
        roll(u, ROLL_AT(u->seq_cursor));
    }
    u->dirty |= UI_D_HEAD;
}

const char *const UI_SEQ_PAGE_NAME[UI_SEQ_PAGES] = {"S1 PATTERN", "S2 STEP", "S3 PLAY"};
const ui_seq_slot UI_SEQ_SLOTS[UI_SEQ_PAGES][4] = {
    {{"TEMPO", "TEMPO", 1}, {"LENGTH", "LENGTH", 1}, {"SWING", "SWING", 1}, {"GATE", "GATE", 1}},
    {{"VEL", "VELOCITY", 1}, {"BEND", "BEND", 1}, {"MOD", "MOD", 1}, {"MOVE", "MOVE", 1}},   /* (#42) */
    {{"TRANSP", "TRANSPOSE", 1}, {"KEYTR", "KEY TRANSPOSE", 1}, {0, 0, 0}, {0, 0, 0}},
};

/* a step's bend (-8192..8191) as -100..+100 % of the bend range, and back: the ends are exact both ways */
static int32_t bend_pct(int32_t bend)
{
    return bend < 0 ? -((-bend * 100 + 4096) / 8192) : (bend * 100 + 4095) / 8191;
}

static int32_t pct_bend(int32_t pct)
{
    return pct < 0 ? pct * 8192 / 100 : pct * 8191 / 100;
}

int32_t ui_seq_value(const ui *u, uint32_t k)
{
    seq *s = play_seq(u->p);
    const seq_step *st = seq_get_step(s, u->seq_cursor);
    switch (u->seq_page * 4u + k) {
    case 0: return (int32_t)ui_seq_tempo_shown(u);
    case 1: return (int32_t)seq_length(s);
    case 2: return s->edit.swing_pct;
    case 3: return s->edit.gate_pct;
    case 4: return st->kind == SEQ_NOTES_ON ? st->vel : 0;
    case 5: return bend_pct(st->bend);
    case 6: return st->mod;
    case 7: return st->glide;
    case 8: return seq_transpose(s);
    case 9: return (int32_t)seq_key_transpose(s);
    default: return 0;
    }
}

/* knob k on the SEQ page shown, clicks (3 or more at once: fast) */
static void turn(ui *u, uint32_t k, int32_t clicks)
{
    seq *s = play_seq(u->p);
    int32_t fast = clicks >= 3 || clicks <= -3, v;
    const seq_step *st;
    (void)pattern_running(play_patterns(u->p), 0);   /* a takeover first: the value is the running one's */
    v = ui_seq_value(u, k);
    if (!UI_SEQ_SLOTS[u->seq_page][k].live)
        return;
    HANDOFF_GAP("turn", 1);                     /* (#348: a takeover here, before the setter's take-in) */
    switch (u->seq_page * 4u + k) {
    case 0:                                     /* (the header and the footer show it too) */
        if (seq_clock_external(s)) {            /* the clock sets it: nothing, and why (case 48) */
            banner(u, "TEMPO FOLLOWS MIDI CLOCK", u->now_ms);
            break;
        }
        v += clicks * (fast ? 100 : 10);
        seq_set_tempo(s, (uint32_t)(v < 0 ? 0 : v));
        u->dirty |= UI_D_FOOT | UI_D_HEAD;
        break;
    case 1:
        v += clicks;
        seq_set_length(s, (uint32_t)(v < 1 ? 1 : v));
        if (u->seq_cursor >= seq_length(s))     /* (the cursor stays inside the pattern) */
            u->seq_cursor = seq_length(s) - 1u;
        roll(u, ROLL_ALL);
        break;
    case 2: v += clicks * (fast ? 5 : 1); seq_set_swing(s, (uint32_t)(v < 0 ? 0 : v)); break;
    case 3: v += clicks * (fast ? 5 : 1); seq_set_gate(s, (uint32_t)(v < 0 ? 0 : v)); break;
    case 4:                                     /* the cursor step's velocity, only it: a note or chord only */
        st = seq_get_step(s, u->seq_cursor);
        if (st->kind != SEQ_NOTES_ON)
            return;
        v += clicks * (fast ? 10 : 1);
        seq_set_step_vel(s, u->seq_cursor, (uint32_t)(v < 1 ? 1 : v));
        roll(u, ROLL_AT(u->seq_cursor));
        break;
    case 5:                                     /* BEND, 1 % of the range a click (#42): a note or chord only */
        st = seq_get_step(s, u->seq_cursor);
        if (st->kind != SEQ_NOTES_ON)
            return;
        v += clicks * (fast ? 10 : 1);
        seq_set_step_bend_mod(s, u->seq_cursor, pct_bend(v < -100 ? -100 : v > 100 ? 100 : v), st->mod, st->glide);
        break;
    case 6:                                     /* MOD, 0..127 (#42): a note or chord only */
        st = seq_get_step(s, u->seq_cursor);
        if (st->kind != SEQ_NOTES_ON)
            return;
        v += clicks * (fast ? 10 : 1);
        seq_set_step_bend_mod(s, u->seq_cursor, st->bend, v, st->glide);
        break;
    case 7:                                     /* MOVE: JUMP (0) or GLIDE (1), held at the ends */
        st = seq_get_step(s, u->seq_cursor);
        if (st->kind != SEQ_NOTES_ON)
            return;
        v += clicks;
        seq_set_step_bend_mod(s, u->seq_cursor, st->bend, st->mod, v > 0);
        break;
    case 8: v += clicks; seq_set_transpose(s, v); break;
    case 9: v += clicks; seq_set_key_transpose(s, v > 0); break;   /* KEY TRANSPOSE: OFF, ON (held at the ends) */
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
    pattern_now *pn = play_patterns(u->p);
    int32_t way = clicks > 0 ? 1 : -1, from = pattern_queued(pn, 0), q;
    if (pattern_edited(pn) && !(u->warned == way && ms - u->warned_ms < UI_DISCARD_MS)) {
        banner(u, "PTN EDITED - TURN TO DISCARD", ms);   /* the first click loads nothing */
        u->warned = way;
        u->warned_ms = ms;
        return;
    }
    u->warned = 0;
    if (clicks >= 3 || clicks <= -3)
        clicks = clicks > 0 ? 8 : -8;
    if (from < 0)                               /* (on from the queued one, if there is one) */
        from = (int32_t)pattern_running(pn, 0);
    q = ((from + clicks) % (int32_t)UI_PATTERNS + (int32_t)UI_PATTERNS) % (int32_t)UI_PATTERNS;
    pattern_queue(pn, (uint32_t)q);
    if (s->want && pattern_queued(pn, 0) >= 0) {   /* queued: say which */
        put_slot(put_s(u->seq_msg, "NEXT PATTERN "), (uint32_t)q);
        banner(u, u->seq_msg, ms);
    } else {
        u->banner = "";
    }
    if (u->seq_cursor >= seq_length(s))
        u->seq_cursor = 0;
    u->built_n = 0;                             /* (a step part-built in REC: dropped with the old pattern) */
    roll(u, ROLL_ALL);
    u->dirty |= (UI_D_ALL & ~UI_D_FULL) | UI_D_FOOT;
}

/* the knob columns: S2's four show the cursor step, so a new cursor step or kind redraws them all */
#define STEP_COLS (UI_D_COL(0) | UI_D_COL(1) | UI_D_COL(2) | UI_D_COL(3))

void ui_seq_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms)
{
    seq *s = play_seq(u->p);
    uint32_t id = ev & 0xFFu, i, len;
    int tap = (ev & 0xFF000000u) == UI_EV_BTN_UP && id < UI_BUTTONS && ms - u->down_ms[id] < UI_TAP_MS;
    if (u->seq_armed && !u->built_n) {          /* a tapped step, nothing played: any control lets go of it */
        u->seq_armed = 0;
        u->dirty |= UI_D_FOCUS;
        roll(u, ROLL_AT(u->seq_cursor));
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_REC) && tap) {          /* step record: armed or not */
        if (s->want)
            banner(u, "STOP TO RECORD", ms);    /* (playing: no recording, decision Q3) */
        else
            ui_seq_rec(u);
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_SEQ) && tap) {          /* back, on the page it left */
        u->seq_armed = u->built_n = u->seq_rec = 0;
        u->state = u->seq_from;
        u->dirty |= UI_D_FULL;
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_HOME) && tap) {
        u->seq_armed = u->built_n = u->seq_rec = 0;
        u->state = UI_PLAY;
        u->dirty |= UI_D_FULL;
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_EDIT) && tap) {  /* the last edit page */
        u->seq_armed = u->built_n = u->seq_rec = 0;
        u->state = UI_EDIT;
        u->dirty |= UI_D_FULL;
    } else if (u->seq_rec && tap && ev == (UI_EV_BTN_UP | UI_BTN_FX)) {   /* REC: back one, nothing written */
        roll(u, ROLL_AT(u->seq_cursor));
        u->built_n = 0;
        u->seq_cursor = (u->seq_cursor + seq_length(s) - 1u) % seq_length(s);
        roll(u, ROLL_AT(u->seq_cursor));
        u->dirty |= UI_D_FOCUS | STEP_COLS;
    } else if (u->seq_rec && tap && (ev == (UI_EV_BTN_UP | UI_BTN_SEL) || ev == (UI_EV_BTN_UP | UI_BTN_ENV))) {
        if (id == UI_BTN_SEL)                   /* REC: a rest or a tie at the cursor, then on (interaction.md 5) */
            seq_clear_step(s, u->seq_cursor);
        else if (seq_get_step(s, u->seq_cursor)->kind != SEQ_TIE)
            seq_toggle_tie(s, u->seq_cursor);
        u->built_n = 0;
        u->seq_cursor = (u->seq_cursor + 1u) % seq_length(s);
        u->dirty |= UI_D_FOCUS | STEP_COLS;
        roll(u, ROLL_ALL);
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_SEL) && u->seq_rec) {
        /* REC: SEL held does nothing (interaction.md's table) */
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_SEL)) {
        if (ms - u->down_ms[UI_BTN_SEL] >= UI_SEQ_CLEAR_MS) {   /* held: the whole pattern */
            for (i = 0; i < SEQ_STEPS; i++)
                seq_clear_step(s, i);
        } else if (tap) {
            seq_clear_step(s, u->seq_cursor);   /* the cursor step: a rest */
        }
        u->dirty |= UI_D_FOCUS | STEP_COLS;
        roll(u, ROLL_ALL);                      /* (a rest ends the ties after it) */
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_LFO) && tap) {   /* MOVE on the cursor step, in REC too (#42) */
        const seq_step *st = seq_get_step(s, u->seq_cursor);
        if (st->kind == SEQ_NOTES_ON)
            seq_set_step_bend_mod(s, u->seq_cursor, st->bend, st->mod, !st->glide);
        u->dirty |= UI_D_FOCUS | UI_D_COL(3);   /* (MOVE's column, if S2 shows) */
    } else if (ev == (UI_EV_BTN_UP | UI_BTN_ENV) && tap) {
        seq_toggle_tie(s, u->seq_cursor);
        u->dirty |= UI_D_FOCUS | STEP_COLS;
        roll(u, ROLL_ALL);
    } else if (ev == (UI_EV_ENC | UI_ENC_SELECT) && arg) {  /* the cursor, around the length */
        len = seq_length(s);
        roll(u, ROLL_AT(u->seq_cursor));
        u->seq_cursor = (uint32_t)(((int32_t)(u->seq_cursor % len) + arg % (int32_t)len + (int32_t)len) %
                                   (int32_t)len);
        roll(u, ROLL_AT(u->seq_cursor));
        u->dirty |= UI_D_FOCUS | STEP_COLS;
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
    int32_t step = ui_seq_step_of_key(k);
    if (u->boot) {                              /* the boot screen (#50): one sweep, left to right, in 500 ms */
        uint32_t t = u->now_ms - u->boot_ms;
        return u->boot == 2u && t < UI_BOOT_SWEEP_MS && t * PLAY_KEYS / UI_BOOT_SWEEP_MS == k ? UI_LED_LIT
                                                                                             : UI_LED_OFF;
    }
    if (u->state != UI_SEQ) {                   /* the playhead's key while playing; the keypad; held keys */
        if (ui_keypad_led(u, k) == UI_LED_GLOW)
            return UI_LED_GLOW;
        return (down && u->set.key_leds) || (s->want && at >= 0 && step == at) ? UI_LED_LIT
                                                                               : UI_LED_OFF;   /* (G33) */
    }
    if ((u->seq_armed || u->seq_rec) && down)   /* armed or REC: every key plays, lit while held */
        return UI_LED_LIT;
    if (step < 0 || (uint32_t)step >= seq_length(s))   /* a black key; a step past the length */
        return UI_LED_OFF;
    k = (uint32_t)step;
    st = seq_get_step(s, k);
    if (u->seq_rec)                             /* REC (Eric, R2): the cursor at 8 Hz, the rest a dim map */
        return k == u->seq_cursor ? (u->now_ms / 63u & 1u ? UI_LED_GLOW : UI_LED_LIT)
                                  : st->kind == SEQ_REST ? UI_LED_OFF : UI_LED_GLOW;
    led = st->kind == SEQ_REST ? UI_LED_GLOW : UI_LED_LIT;
    if (st->kind == SEQ_TIE && u->now_ms % 500u >= 400u)    /* a tie: a short dark blink every 500 ms */
        led = UI_LED_OFF;
    if (s->want && at >= 0) {                   /* playing: the playhead, its step inverted */
        if ((uint32_t)at == k)
            led = st->kind == SEQ_REST ? UI_LED_LIT : UI_LED_OFF;
    } else if (k == u->seq_cursor) {            /* stopped: the cursor blinks lit / glow at 4 Hz (armed: 8) */
        led = u->now_ms / (u->seq_armed ? 63u : 125u) & 1u ? UI_LED_GLOW : UI_LED_LIT;
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
        if (ui_seq_no_clock(u)) {               /* MIDI, no clock: no tempo to show */
            put_s(num, "--");
            put_s(unit, "MIDI CLOCK");
            break;
        }
        n = put_u(n, (uint32_t)v / 10u);
        if (v % 10)
            *n++ = '.', put_u(n, (uint32_t)v % 10u);
        put_s(unit, seq_clock_external(play_seq(u->p)) ? "MIDI CLOCK" : "BPM");   /* (under the clock: its tempo) */
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
    case 5:                                     /* BEND: signed, in percent */
        if (v > 0)
            *n++ = '+';
        else if (v < 0)
            *n++ = '-';
        put_u(n, (uint32_t)(v < 0 ? -v : v));
        put_s(unit, "%");
        break;
    case 7:
        put_s(n, v ? "GLIDE" : "JUMP");
        break;
    case 9:
        put_s(n, v ? "ON" : "OFF");
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

int ui_seq_active(const ui *u, uint32_t k)
{
    (void)k;                                    /* (every S2 knob is the cursor step's) */
    return u->seq_page != 1u || seq_get_step(play_seq(u->p), u->seq_cursor)->kind == SEQ_NOTES_ON;
}

void ui_seq_detail(const ui *u, uint32_t k, char *s)
{
    int32_t v = ui_seq_value(u, k), a = v < 0 ? -v : v, r;
    uint32_t t;
    char *n = s;
    *s = 0;
    if (u->seq_page * 4u + k != 5u)
        return;
    r = play_get_param(u->p, FM_PARAM_BEND_RANGE);
    t = ((uint32_t)a * (uint32_t)(r < 0 ? 0 : r > 24 ? 24 : r) + 5u) / 10u;  /* semitones, tenths */
    if (v)
        *n++ = v < 0 ? '-' : '+';
    n = put_s(put_u(n, (uint32_t)a), " % = ");
    if (v)
        *n++ = v < 0 ? '-' : '+';
    n = put_u(n, t / 10u);
    *n++ = '.';
    put_s(put_u(n, t % 10u), " st");
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
    note_names(u->built, u->seq_armed || u->seq_rec ? (u->built_n < PLAY_VOICES ? u->built_n : PLAY_VOICES) : 0u, s);
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
    pattern_seen v;                             /* one look: no takeover between its parts */
    char *s = put_s(left, "PTN ");
    pattern_look(play_patterns(u->p), &v);
    s = put_slot(s, v.running);
    if (*v.name) {
        s = put_s(s, " ");
        s = put_s(s, v.name);
    }
    if (v.edited)
        put_s(s, "*");
    *right = 0;
    if (v.queued >= 0)
        put_slot(put_s(right, "NEXT "), (uint32_t)v.queued);
}

/* every note of the pattern's steps (within its length) and an armed step's notes so far, into *lo, *hi;
 * 0 when there's none */
static int roll_notes(const ui *u, uint32_t *lo, uint32_t *hi)
{
    const seq *s = play_seq(u->p);
    uint32_t i, j, len = seq_length(s), any = 0;
    *lo = 127u, *hi = 0u;
    for (i = 0; i < len; i++) {
        const seq_step *st = seq_get_step(s, i);
        for (j = 0; st->kind == SEQ_NOTES_ON && j < st->n; j++, any = 1)
            *lo = st->note[j] < *lo ? st->note[j] : *lo, *hi = st->note[j] > *hi ? st->note[j] : *hi;
    }
    for (j = 0; (u->seq_armed || u->seq_rec) && j < u->built_n; j++, any = 1)
        *lo = u->built[j] < *lo ? u->built[j] : *lo, *hi = u->built[j] > *hi ? u->built[j] : *hi;
    return (int)any;
}

int ui_seq_roll_fit(ui *u)
{
    uint32_t lo, hi, span, pps, rows, from;
    if (!roll_notes(u, &lo, &hi))
        lo = hi = 60u;                          /* (none: an octave around C4) */
    if (u->roll_pps && ((lo >= u->roll_lo && hi < u->roll_lo + UI_ROLL_H / u->roll_pps) ||
                        (lo >= u->roll_seen_lo && hi <= u->roll_seen_hi)))
        return 0;                               /* all inside, or no wider than it was fitted to: no jump */
    u->roll_seen_lo = lo;
    u->roll_seen_hi = hi;
    span = hi - lo + 1u;
    span = span < 13u ? 13u : span;             /* at least an octave */
    pps = span <= UI_ROLL_H / 3u ? 3u : 2u;
    rows = UI_ROLL_H / pps;
    if (hi - lo + 1u <= rows) {                 /* it fits: centered */
        from = (lo + hi + 1u) / 2u;
        from = from > rows / 2u ? from - rows / 2u : 0u;
        from = from < lo && hi - from + 1u > rows ? lo : from;
    } else {                                    /* wider: centered on the cursor step's notes */
        const seq_step *st = seq_get_step(play_seq(u->p), u->seq_cursor);
        uint32_t mid = st->kind == SEQ_NOTES_ON && st->n ? st->note[0] : (lo + hi) / 2u;
        from = mid > rows / 2u ? mid - rows / 2u : 0u;
    }
    from = from + rows > 128u ? 128u - rows : from;
    u->roll_lo = from;
    u->roll_pps = pps;
    u->roll_dirty = ROLL_ALL;
    return 1;
}

uint32_t ui_seq_roll_take(ui *u)
{
    uint32_t d = u->roll_dirty;
    u->roll_dirty = 0;
    return d;
}

void ui_seq_tick(ui *u, uint32_t ms)
{
    const seq *s = play_seq(u->p);
    int32_t at = s->want ? seq_step_now(s) : -1;
    uint32_t blink = u->seq_armed || u->seq_rec ? ms / 125u & 1u : 0u;
    if (at != u->shown_playhead) {              /* the playhead: its old and new columns, the header's step */
        if (u->shown_playhead >= 0)
            roll(u, ROLL_AT((uint32_t)u->shown_playhead));
        if (at >= 0)
            roll(u, ROLL_AT((uint32_t)at));
        u->shown_playhead = at;
        u->dirty |= UI_D_HEAD | UI_D_FOOT;      /* (the transport's step, the key strip) */
    }
    if (blink != u->shown_blink) {              /* the armed step's frame blinks */
        u->shown_blink = blink;
        roll(u, ROLL_AT(u->seq_cursor));
        u->dirty |= UI_D_FOOT;
    }
}

void ui_seq_chord_name(const uint8_t *note, uint32_t n, char *s)
{
    static const struct { uint16_t set; const char *name; } Q[9] = {
        {0x091u, "MAJ"}, {0x089u, "MIN"}, {0x049u, "DIM"}, {0x111u, "AUG"}, {0x085u, "SUS2"},
        {0x0A1u, "SUS4"}, {0x491u, "7"}, {0x891u, "MAJ7"}, {0x489u, "MIN7"},
    };   /* pitch classes from the root, bit k: k semitones up (C E G: 0, 4, 7) */
    static const char *const ROOT[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    uint32_t pcs = 0, i, r, q, rot;
    *s = 0;
    for (i = 0; i < n; i++)
        pcs |= 1u << (note[i] % 12u);
    for (r = 0; r < 12u; r++) {                 /* each pitch class held as the root */
        if (!(pcs >> r & 1u))
            continue;
        rot = ((pcs >> r) | (pcs << (12u - r))) & 0xFFFu;
        for (q = 0; q < 9u; q++)
            if (rot == Q[q].set) {
                s = put_s(s, ROOT[r]);
                s = put_s(s, " ");
                put_s(s, Q[q].name);
                return;
            }
    }
}

int ui_seq_no_clock(const ui *u)
{
    const seq *q = play_seq(u->p);
    return q->clk_src == SEQ_CLK_MIDI && !seq_clock_tempo(q);
}

void ui_seq_clock_text(const ui *u, char *s)
{
    const seq *q = play_seq(u->p);
    put_s(s, seq_waiting(q) ? "WAITING FOR CLOCK" : ui_seq_no_clock(u) ? "MIDI NO CLOCK" :
             seq_clock_external(q) ? "MIDI" : "INT");
}

uint32_t ui_seq_tempo_shown(const ui *u)
{
    const seq *s = play_seq(u->p);
    if (seq_clock_external(s) && seq_clock_tempo_whole(s)) {  /* (armed for its next tick too: the clock's) */
        uint32_t t = seq_clock_tempo_whole(s);  /* the clock's, in whole BPM, held: its jitter doesn't show */
        return t > 9990u ? 9990u : t;           /* (three digits at most: a start's first ticks can read far higher) */
    }
    return seq_tempo(s);                        /* (before the clock, or on INT: the pattern's) */
}
