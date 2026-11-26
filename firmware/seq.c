/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p sequencer core (seq.h, #36). The clock: each frame adds tempo x SEQ_PPQ (tenths of a BPM x
 * steps a beat) to acc, and a step is fs x 600 of it (fs frames a second x 60 s x 10 tenths), so a step
 * starts on the first frame acc reaches it, exactly, with the remainder carried: no drift, no float,
 * and only 32-bit sums and divides. */
#include "seq.h"

/* the main loop's stores land in this order for the interrupt (play.c's BARRIER): a compiler barrier,
 * enough on the FM-1's one core, where the interrupt only ever runs between two instructions */
#define BARRIER() __asm__ volatile("" ::: "memory")

#ifdef FM1_HANDOFF_TEST
void (*handoff_gap)(const char *where, uint32_t n);
#endif

static uint32_t take_in(seq *s);
static void ask_close(seq *s);
static uint32_t ask_open(seq *s);
static uint32_t queued_closed(const seq *s);
static void load_now(seq *s, const seq_pattern *p);

static void edit_begin(seq *s)
{
    (void)take_in(s);                           /* an edit is of the pattern playing (#324) */
    s->editing = 1;                             /* the interrupt leaves `edit` alone until edit_end */
    BARRIER();                                  /* set before any store to `edit` */
    HANDOFF_GAP("edit", 1);
}

static void edit_end(seq *s)
{
    BARRIER();                                  /* every store to `edit` before it's published */
    s->edits++;
    s->editing = 0;
}

void seq_pattern_empty(seq_pattern *p)
{
    uint32_t i, k;
    p->length = SEQ_STEPS;
    p->gate_pct = SEQ_GATE_DEF;
    p->swing_pct = SEQ_SWING_DEF;
    p->tempo = SEQ_TEMPO_DEF;
    for (i = 0; i < SEQ_STEPS; i++) {
        p->step[i].kind = SEQ_REST;
        p->step[i].n = 0;
        p->step[i].vel = SEQ_VEL_DEF;
        for (k = 0; k < SEQ_NOTES; k++)
            p->step[i].note[k] = 0;
        p->step[i].bend = 0;
        p->step[i].mod = 0;
        p->step[i].glide = 0;
    }
}

void seq_init(seq *s)
{
    seq_pattern_empty(&s->edit);
    s->run = s->stored = s->edit;
    s->ask = s->took = s->took_pattern = s->took_seen = s->adopted = s->edit_base = 0;
    s->ask_kind = SEQ_ASK_NONE;
    s->editing = 0;
    s->edits = s->edits_seen = 0;
    s->ask_play = s->ask_stop = s->play_seen = s->stop_seen = 0;
    s->ask_last = SEQ_LAST_STOP;
    s->want = 0;
    s->transpose = s->run_transpose = 0;
    s->key_transpose = 0;
    s->pos = s->acc = s->gated = 0;
    s->n_sounding = 0;
    s->cur_bend = s->cur_mod = 0;
    s->from_bend = s->from_mod = s->to_bend = s->to_mod = 0;
    s->glide_on = s->glide_next = 0;
    s->playing = 0;
    s->step_now = -1;
    s->steps = 0;
    s->clk_src = SEQ_CLK_AUTO;
    s->clk_present = s->clk_pending = s->clk_running = s->clk_armed = s->clk_started = 0;
    s->clk_pend_t = 0;
    s->clk_n = s->clk_dt = s->clk_shown = s->clk_target = s->clk_k = s->clk_view = s->clk_whole = s->clk_win = s->clk_far = s->clk_late = s->clk_raw = 0;
    s->clk_freq = 0;
    s->ext = s->ext_inc = s->ext_t0 = s->ext_cont = s->ext_hold = 0;
    s->blk_seq = s->blk_t = s->blk_u = s->blk_inc = s->blk_fs = 0;
    s->upos = s->fs = 0;
    s->out_want = s->out_on = s->out_block = 0;
    s->out_acc = s->out_ticks = s->out_sent = s->out_w = s->out_r = 0;
    s->out_mw = s->out_mr = 0;
}

/* interrupt: a start or stop out, after the ticks counted so far */
static void out_put(seq *s, uint8_t st)
{
    if (s->out_w - s->out_r >= SEQ_OUT_EV)
        return;                                 /* (the main loop gone for 8 transport changes: dropped) */
    s->out_ev[s->out_w % SEQ_OUT_EV].st = st;
    s->out_ev[s->out_w % SEQ_OUT_EV].at = s->out_ticks;
    BARRIER();
    s->out_w++;
}

/* interrupt: its own clock moved on by d (as acc counts it): the clock out's ticks that fell due, 24 a
 * quarter, unswung (a step is fs x 600 of acc at 50 %: six ticks). Moved with acc, step by step, so a
 * tempo change at a step's start can't drift it from the steps */
static void out_adv(seq *s, uint32_t d, uint32_t xi)
{
    uint32_t unit = s->fs * 100u;
    if (!s->out_on || xi)
        return;
    s->out_acc += d;
    while (s->out_acc >= unit) {
        s->out_acc -= unit;
        s->out_ticks++;
    }
}

/* main loop: a followed clock's byte to pass on (dropped past SEQ_OUT_M untaken) */
static void out_pass(seq *s, uint8_t st)
{
    uint32_t i = s->out_mw % SEQ_OUT_M;
    if (s->out_mw - s->out_mr >= SEQ_OUT_M)
        return;
    s->out_m[i].t = s->out_ticks;               /* (what the interrupt had counted and put by now: those go */
    BARRIER();                                  /* out first) */
    s->out_m[i].w = s->out_w;
    s->out_m[i].st = st;
    s->out_mw++;
}

void seq_clock_out_stop(seq *s)
{
    s->out_block = 1;                           /* (a PLAY asked and not yet taken starts no clock out) */
    BARRIER();
    HANDOFF_GAP("out_stop", 0);
    /* the let-go's stop is the main loop's, sent now (nothing drains after a release). out_block is the
     * gate: while it is set the interrupt neither writes out_on nor sends a start or stop, so an
     * interrupt taking a pending stop or restart anywhere in here leaves out_on for us (#349) */
    if (!s->out_on)
        return;
    HANDOFF_GAP("out_stop", 1);
    s->out_on = 0;                              /* (the interrupt counts and stops no more for this run) */
    BARRIER();
    out_pass(s, 0xFCu);
}

void seq_set_clock_out(seq *s, uint32_t on)
{
    s->out_want = (uint8_t)(on != 0);
}

int seq_clock_out_take(seq *s, uint8_t *st)
{
    uint32_t t = s->out_ticks, w;               /* the ticks, then the events: an event put after this */
    int m = s->out_mr != s->out_mw;             /* read is stamped at or after t, so never goes before them */
    BARRIER();
    w = s->out_w;
    if (s->out_r != w && (!m || (int32_t)(s->out_r - s->out_m[s->out_mr % SEQ_OUT_M].w) < 0)) {
        BARRIER();                              /* (the event's fields after its index) */
        if ((int32_t)(s->out_ev[s->out_r % SEQ_OUT_EV].at - s->out_sent) > 0) {
            *st = 0xF8u;                        /* the ticks before it, then it */
            s->out_sent++;
            return 1;
        }
        *st = s->out_ev[s->out_r % SEQ_OUT_EV].st;
        BARRIER();
        s->out_r++;
        return 1;
    }
    if (m) {                                    /* one the main loop queued: the ticks counted before it, then it */
        if ((int32_t)(s->out_m[s->out_mr % SEQ_OUT_M].t - s->out_sent) > 0) {
            *st = 0xF8u;
            s->out_sent++;
            return 1;
        }
        *st = s->out_m[s->out_mr % SEQ_OUT_M].st;
        s->out_mr++;
        return 1;
    }
    if ((int32_t)(t - s->out_sent) > 0) {
        *st = 0xF8u;
        s->out_sent++;
        return 1;
    }
    return 0;
}

uint32_t seq_length(const seq *s)
{
    return s->edit.length;
}

void seq_set_length(seq *s, uint32_t length)
{
    edit_begin(s);
    s->edit.length = (uint8_t)(length < 1u ? 1u : length > SEQ_STEPS ? SEQ_STEPS : length);
    edit_end(s);
}

const seq_step *seq_get_step(const seq *s, uint32_t i)
{
    return i < SEQ_STEPS ? &s->edit.step[i] : 0;
}

void seq_write_step(seq *s, uint32_t i, const uint8_t *notes, uint32_t n, uint32_t vel, int32_t bend,
                    int32_t mod, uint32_t glide)
{
    seq_step *st;
    uint32_t k;
    if (i >= SEQ_STEPS)
        return;
    edit_begin(s);                              /* (one publish: the interrupt takes all of it or none) */
    st = &s->edit.step[i];
    n = n > SEQ_NOTES ? SEQ_NOTES : n;
    for (k = 0; k < n; k++)
        st->note[k] = (uint8_t)(notes[k] & 0x7Fu);
    st->n = (uint8_t)n;
    st->vel = (uint8_t)(vel < 1u ? 1u : vel > 127u ? 127u : vel);
    st->kind = n ? SEQ_NOTES_ON : SEQ_REST;
    st->bend = (int16_t)(bend < -8192 ? -8192 : bend > 8191 ? 8191 : bend);
    st->mod = (uint8_t)(mod < 0 ? 0 : mod > 127 ? 127 : mod);
    st->glide = (uint8_t)(glide != 0);
    edit_end(s);
}

void seq_set_step(seq *s, uint32_t i, const uint8_t *notes, uint32_t n, uint32_t vel)
{
    seq_write_step(s, i, notes, n, vel, 0, 0, 0);
}

void seq_set_step_vel(seq *s, uint32_t i, uint32_t vel)
{
    if (i >= SEQ_STEPS || s->edit.step[i].kind != SEQ_NOTES_ON)
        return;
    edit_begin(s);
    s->edit.step[i].vel = (uint8_t)(vel < 1u ? 1u : vel > 127u ? 127u : vel);
    edit_end(s);
}

void seq_set_step_bend_mod(seq *s, uint32_t i, int32_t bend, int32_t mod, uint32_t glide)
{
    seq_step *st;
    if (i >= SEQ_STEPS)
        return;
    edit_begin(s);
    st = &s->edit.step[i];
    st->bend = (int16_t)(bend < -8192 ? -8192 : bend > 8191 ? 8191 : bend);
    st->mod = (uint8_t)(mod < 0 ? 0 : mod > 127 ? 127 : mod);
    st->glide = (uint8_t)(glide != 0);
    edit_end(s);
}

void seq_clear_step(seq *s, uint32_t i)
{
    if (i >= SEQ_STEPS)
        return;
    edit_begin(s);
    s->edit.step[i].kind = SEQ_REST;
    s->edit.step[i].n = 0;
    edit_end(s);
}

void seq_toggle_tie(seq *s, uint32_t i)
{
    if (i >= SEQ_STEPS)
        return;
    edit_begin(s);
    s->edit.step[i].kind = s->edit.step[i].kind == SEQ_TIE ? SEQ_REST : SEQ_TIE;
    s->edit.step[i].n = 0;
    edit_end(s);
}

void seq_set_transpose(seq *s, int32_t semis)
{
    s->transpose = semis < -SEQ_TRANSPOSE_MAX ? -SEQ_TRANSPOSE_MAX
                   : semis > SEQ_TRANSPOSE_MAX ? SEQ_TRANSPOSE_MAX : semis;
}

int32_t seq_transpose(const seq *s)
{
    return s->transpose;
}

void seq_set_key_transpose(seq *s, uint32_t on)
{
    s->key_transpose = (uint8_t)(on != 0);
}

uint32_t seq_key_transpose(const seq *s)
{
    return s->key_transpose;
}

uint32_t seq_tempo(const seq *s)
{
    return s->edit.tempo;
}

void seq_set_tempo(seq *s, uint32_t tenths_bpm)
{
    edit_begin(s);
    s->edit.tempo = (uint16_t)(tenths_bpm < SEQ_TEMPO_MIN ? SEQ_TEMPO_MIN
                               : tenths_bpm > SEQ_TEMPO_MAX ? SEQ_TEMPO_MAX : tenths_bpm);
    edit_end(s);
}

void seq_set_gate(seq *s, uint32_t pct)
{
    edit_begin(s);
    s->edit.gate_pct = (uint8_t)(pct < SEQ_GATE_MIN ? SEQ_GATE_MIN : pct > 100u ? 100u : pct);
    edit_end(s);
}

void seq_set_swing(seq *s, uint32_t pct)
{
    edit_begin(s);
    s->edit.swing_pct = (uint8_t)(pct < 50u ? 50u : pct > 75u ? 75u : pct);
    edit_end(s);
}

void seq_play(seq *s)
{
    s->out_block = 0;                           /* (#286: a PLAY after a let-go sends again) */
    if (s->clk_src == SEQ_CLK_MIDI || (s->clk_src == SEQ_CLK_AUTO && s->clk_present)) {
        s->want = 1;                            /* #38: the clock's in charge, so this arms (the interrupt
                                                 * acts on asks only); a second press (seq_stop) disarms.
                                                 * MIDI: it starts on a MIDI start or continue. AUTO, with a
                                                 * clock coming: on its next tick, from step 1 (Eric: many
                                                 * send clock and never a start) */
        s->clk_armed = 1;
        if (s->clk_src == SEQ_CLK_AUTO)
            s->clk_pending = 1;
        return;
    }
    s->ext = 0;                                 /* (its own clock) */
    s->clk_pending = 0;                         /* (a start come in with no clock: nothing to wait for) */
    s->want = 1;
    s->ask_last = SEQ_LAST_PLAY;
    BARRIER();                                  /* the wish before the ask: never an ask with an old one */
    HANDOFF_GAP("play", 1);
    s->ask_play++;
}

void seq_stop(seq *s)
{
    if (s->out_on && s->ext) {                  /* #286: a followed run's stop goes out after the ticks passed
                                                 * on (the main loop's queue, in order); its own run's, the
                                                 * interrupt sends after its own ticks */
        out_pass(s, 0xFCu);
        s->out_on = 0;
    }
    s->clk_armed = 0;
    s->clk_pending = 0;
    s->clk_running = 0;
    s->want = 0;
    s->ask_last = SEQ_LAST_STOP;
    BARRIER();
    HANDOFF_GAP("stop", 0);
    s->ask_stop++;
    HANDOFF_GAP("stop", 1);
    ask_close(s);
    (void)take_in(s);                           /* (one the interrupt took already) */
    if (queued_closed(s)) {                     /* stopping with one queued: it loads at once */
        s->adopted = s->ask & ~1u;
        load_now(s, &s->next);
    }
    s->ask_kind = SEQ_ASK_NONE;
    (void)ask_open(s);
}

/* The queued-pattern handoff's steps (#324): close, sync, write, open. Closed (`ask` odd), the
 * interrupt takes nothing, so what it took is known (sync) before `next` and `ask_kind` change. */
static void ask_close(seq *s)
{
    HANDOFF_GAP("ask", 0);                      /* (still open: the caller decided before this) */
    s->ask = s->ask | 1u;
    BARRIER();                                  /* closed before anything after it */
    HANDOFF_GAP("ask", 1);
}

static uint32_t ask_open(seq *s)
{
    uint32_t a = s->ask + 1u;                   /* the next even one */
    if (a == 0u)
        a = 2u;                                 /* (0 is "no ask yet") */
    HANDOFF_GAP("ask", 3);
    BARRIER();                                  /* every store to next and ask_kind before it */
    s->ask = a;
    return a;
}

/* closed: the ask being replaced is still queued (not taken, not a cancel) */
static uint32_t queued_closed(const seq *s)
{
    return s->ask_kind == SEQ_ASK_PATTERN && s->took != (s->ask & ~1u);
}

/* a takeover the main loop hasn't seen: the edit buffer and the stored copy become what was taken */
static uint32_t take_in(seq *s)
{
    uint32_t t = s->took_pattern;
    if (t == s->took_seen)
        return 0;
    s->took_seen = t;
    s->editing = 1;                             /* (as edit_begin, without its sync) */
    BARRIER();
    s->edit = s->next;                          /* what the interrupt took: `next` changes only after a sync */
    s->edit_base = t;
    edit_end(s);
    s->stored = s->next;
    s->adopted = t;
    return 1;
}

static void load_now(seq *s, const seq_pattern *p)
{
    edit_begin(s);
    s->edit = *p;
    s->edit_base = s->took_pattern;             /* an edit of what plays */
    edit_end(s);
    s->stored = *p;
}

uint32_t seq_load_pattern(seq *s, const seq_pattern *p)
{
    ask_close(s);
    (void)take_in(s);
    HANDOFF_GAP("ask", 2);
    if (s->want && s->playing) {                /* playing: queued for the end of the pass */
        s->next = *p;
        s->ask_kind = SEQ_ASK_PATTERN;
        return ask_open(s);
    }
    s->ask_kind = SEQ_ASK_NONE;
    (void)ask_open(s);
    load_now(s, p);
    return 0;
}

uint32_t seq_requeue(seq *s, const seq_pattern *p)
{
    ask_close(s);
    (void)take_in(s);
    HANDOFF_GAP("ask", 2);
    if (queued_closed(s)) {
        s->next = *p;
        return ask_open(s);
    }
    s->ask_kind = SEQ_ASK_NONE;                 /* (taken or canceled: nothing to queue again) */
    (void)ask_open(s);
    return 0;
}

uint32_t seq_next_queued(const seq *s)
{
    return s->ask_kind == SEQ_ASK_PATTERN && s->took != s->ask;
}

void seq_cancel_next(seq *s)
{
    ask_close(s);
    (void)take_in(s);
    HANDOFF_GAP("ask", 2);
    s->ask_kind = SEQ_ASK_NONE;
    (void)ask_open(s);
}

uint32_t seq_main_tick(seq *s)
{
    return take_in(s);
}

uint32_t seq_adopted(const seq *s)
{
    return s->adopted;
}

/* the same pattern, as played: settings, and each step's kind, velocity and the notes it has */
static int same_pattern(const seq_pattern *a, const seq_pattern *b)
{
    uint32_t i, k;
    if (a->length != b->length || a->tempo != b->tempo || a->swing_pct != b->swing_pct ||
        a->gate_pct != b->gate_pct)
        return 0;
    for (i = 0; i < SEQ_STEPS; i++) {
        const seq_step *x = &a->step[i], *y = &b->step[i];
        if (x->kind != y->kind || x->n != y->n || x->vel != y->vel || x->bend != y->bend ||
            x->mod != y->mod || x->glide != y->glide)
            return 0;
        for (k = 0; k < x->n && k < SEQ_NOTES; k++)
            if (x->note[k] != y->note[k])
                return 0;
    }
    return 1;
}

uint32_t seq_pattern_is_empty(const seq_pattern *p)
{
    uint32_t i;
    if (p->length != SEQ_STEPS || p->tempo != SEQ_TEMPO_DEF || p->swing_pct != SEQ_SWING_DEF ||
        p->gate_pct != SEQ_GATE_DEF)                /* (as seq_pattern_empty sets them, compared as same_pattern) */
        return 0;
    for (i = 0; i < SEQ_STEPS; i++) {
        const seq_step *x = &p->step[i];
        if (x->kind != SEQ_REST || x->n != 0 || x->vel != SEQ_VEL_DEF || x->bend != 0 || x->mod != 0 ||
            x->glide != 0)
            return 0;
    }
    return 1;
}

uint32_t seq_edited(const seq *s)
{
    return !same_pattern(&s->edit, &s->stored);
}

void seq_mark_saved(seq *s)
{
    s->stored = s->edit;
}

uint32_t seq_playing(const seq *s)
{
    return s->want || s->playing;
}

int32_t seq_step_now(const seq *s)
{
    return s->step_now;
}

uint32_t seq_steps(const seq *s)
{
    return s->steps;
}

/* ---- #38, external MIDI clock (main loop) ----
 * The clock's ticks reach the module in the main loop (fm1-dock's midi()), late and jittered by as much as
 * a render block, so the steps don't start on them. Each tick is stamped on the host counter, and the
 * interrupt stamps each block's start on it too, with where its clock (upos, units since the start's tick
 * 0) had got to. So the main loop sees how far the steps are from the ticks, and sets the rate they play
 * at (ext_inc, units a frame): the tick length measured over the last beat, plus a share of that
 * distance, made up over a beat (CLK_PULL ticks). The steps still start on exact frames. The units are
 * the internal clock's (a step is fs x 600): a tick is fs x 100, and a tick of dt counts is
 * 100 x SEQ_CLK_HZ / dt units a frame (fs drops out). */
#define CLK_UNITS_HZ (100u * SEQ_CLK_HZ)       /* 2.4e9: a tick's units a frame are this / dt */
#define CLK_TENTHS_COUNTS (600u * (SEQ_CLK_HZ / SEQ_CLK_PPQN))   /* 6e8: a tick's counts x tempo in tenths */
/* the loop, tuned against tests/test_seq_clock.c (+-1.5 ms jitter, a 120 -> 140 jump, a 100 -> 160 ramp) */
#define CLK_PULL 24u                           /* ticks a distance is made up in, riding the jitter */
#define CLK_PULL_MIN 3u                        /* ... from a start or a hard pull, widening to CLK_PULL */
#define CLK_PULL_RAMP 4u                       /* ... a tick wider every this many ticks */
#define CLK_FAR_US 2000u                       /* the steps this far off the ticks ... */
#define CLK_FAR_N 2u                           /* ... this many ticks running: the tempo moved, pull hard */
#define CLK_CHANGE_TICKS 6u                    /* a tempo change is seen in the last this many ticks ... */
#define CLK_CHANGE_PCT 4u                      /* ... past this % of the length: taken at once */
#define CLK_SMOOTH 16u                         /* the length is smoothed 1/this a tick */
#define CLK_VIEW 16u                           /* and again 1/this, for the tempo shown */
#define CLK_DT_MIN (SEQ_CLK_HZ / 1000u)         /* the shortest tick taken: 1 ms */
#define CLK_GONE_MIN (SEQ_CLK_HZ / 4u)         /* gone: no tick for 250 ms, or 3 ticks if longer */

void seq_set_clock_source(seq *s, uint32_t src)
{
    s->clk_src = (uint8_t)(src <= SEQ_CLK_MIDI ? src : SEQ_CLK_AUTO);
    if (s->clk_armed && !s->clk_running && s->clk_src == SEQ_CLK_MIDI) {
        s->clk_pending = 0;                     /* armed, now MIDI: a start or continue starts it, not a tick
                                                 * (one come in within the same tick is dropped: send again) */
        return;
    }
    if (s->clk_armed && !s->clk_running && s->clk_src == SEQ_CLK_AUTO) {
        if (s->clk_present) {
            if (!s->clk_pending)                /* armed, now AUTO: on the clock's next tick, from step 1, */
                s->clk_pending = 1;             /* or a start or continue come in already, as it says */
        } else {
            s->clk_armed = 0;                   /* (AUTO with no clock coming: its own, now) */
            s->want = 0;
            seq_play(s);
        }
        return;
    }
    if (s->clk_src != SEQ_CLK_INT)
        return;
    s->clk_pending = 0;                         /* INT: the clock is ignored from now */
    if (s->clk_armed) {                         /* (waiting for it: nothing to wait for) */
        s->clk_armed = 0;
        s->want = 0;
    }
    if (s->clk_running) {                       /* playing on it: on, on the pattern's own tempo, from
                                                 * where it is (the interrupt's next block) */
        s->clk_running = 0;
        s->ext = 0;
        BARRIER();
        s->ext_hold = 0;
    }
}

/* the block stamped last, read whole: its host time, clock, rate and fs */
static void block_read(const seq *s, uint32_t *t, uint32_t *u, uint32_t *inc, uint32_t *fs)
{
    uint32_t a;
    do {
        a = s->blk_seq;
        BARRIER();
        *t = s->blk_t;
        HANDOFF_GAP("blk", 1);
        *u = s->blk_u;
        *inc = s->blk_inc;
        *fs = s->blk_fs;
        BARRIER();
    } while ((a & 1u) || a != s->blk_seq);
}

/* frames in `counts` of the host counter at fs, Q8 (counts within +-2^23: 0.35 s). A frame is taken
 * as a whole number of counts (544 at 44.1 kHz for 544.2): 4e-4 off, 2 us over a block. 32-bit. */
static int32_t frames_q8(int32_t counts, uint32_t fs)
{
    uint32_t per = SEQ_CLK_HZ / (fs ? fs : 44100u);
    int32_t c = counts > (1 << 23) ? (1 << 23) : counts < -(1 << 23) ? -(1 << 23) : counts;
    return c < 0 ? -(int32_t)(((uint32_t)-c << 8) / per) : (int32_t)(((uint32_t)c << 8) / per);
}

/* the clock's rate, a second-order loop on the distance e (units the ticks are ahead of the steps):
 * the frequency (clk_freq, units a frame, Q4) follows e at 1 / (4 pull^2) a tick, and the rate is it
 * plus e made up over `pull` ticks: critically damped. The frequency starts from the measured length (a
 * start, a tempo change). From a start, or the steps far off the ticks for CLK_FAR_N ticks running, the
 * pull is tight (CLK_PULL_MIN) and widens to CLK_PULL: tick 0's own jitter and a tempo's move are taken
 * out fast, and the jitter is ridden once it's locked. 32-bit, two divides a tick, main loop only. */
static void set_rate(seq *s, int32_t e, uint32_t fs, int fresh)
{
    int32_t fpt = frames_q8((int32_t)s->clk_dt, fs) >> 8, inc;   /* frames a tick */
    int32_t pull, far = (int32_t)(fs * CLK_FAR_US / 1000000u) * (int32_t)(CLK_UNITS_HZ / s->clk_dt);
    if (e > far || e < -far) {                  /* off by more than jitter, CLK_FAR_N ticks running: the
                                                 * tempo moved, pull hard */
        if (++s->clk_far >= CLK_FAR_N)
            s->clk_k = 0;
    } else {
        s->clk_far = 0;
    }
    pull = (int32_t)(s->clk_k / CLK_PULL_RAMP);
    pull = pull < (int32_t)CLK_PULL_MIN ? (int32_t)CLK_PULL_MIN : pull > (int32_t)CLK_PULL ? (int32_t)CLK_PULL : pull;
    e = e > (1 << 23) ? (1 << 23) : e < -(1 << 23) ? -(1 << 23) : e;
    if (fresh || !s->clk_freq)
        s->clk_freq = (int32_t)((CLK_UNITS_HZ / s->clk_dt) << 4);
    else if (fpt > 0)
        s->clk_freq += e * 16 / (4 * pull * pull) / fpt;
    inc = (s->clk_freq >> 4) + (fpt > 0 ? e / (pull * fpt) : 0);
    s->ext_inc = (uint32_t)(inc < 1 ? 1 : inc);
}

/* the mean tick length over the last m lengths (m >= 1, m ticks kept since the window restarted) */
static uint32_t mean_len(const seq *s, uint32_t t, uint32_t m)
{
    return (t - s->clk_t[(s->clk_n - 1u - m) % SEQ_CLK_HIST]) / m;
}

#define CLK_HOLD 75u                            /* the whole BPM shown moves when the tempo is 0.75 BPM off it */

/* clk_whole from clk_view: the tempo rounded to a whole BPM, held while it is within CLK_HOLD hundredths
 * of the one shown. The edges are compared exactly, in counts (tempo x tick length is a constant), so
 * they are the same up and down: nothing truncated first (Codex: tenths held 0.71 down, 0.8 up) */
static void hold_whole(seq *s)
{
    const uint64_t k = (uint64_t)CLK_TENTHS_COUNTS * 10u;   /* hundredths of a BPM x counts a tick */
    uint32_t d = (s->clk_view + 8u) >> 4, w;
    if (!s->clk_present || !s->clk_view || !d)
        return;
    w = (uint32_t)((k + (uint64_t)d * 50u) / ((uint64_t)d * 100u)) * 10u;   /* rounded, in tenths */
    if (!s->clk_whole || k >= (uint64_t)(s->clk_whole * 10u + CLK_HOLD) * d ||
        k <= (uint64_t)(s->clk_whole * 10u - CLK_HOLD) * d)
        s->clk_whole = w < 10u ? 10u : w;
}

static void clock_tick(seq *s, uint32_t t)
{
    uint32_t bt, bu, binc, fs, held = 0;
    uint8_t blocked;
    if (!s->clk_present) {
        s->clk_present = 1;
        s->clk_win = 0;
        s->clk_dt = s->clk_shown = s->clk_view = s->clk_whole = 0;
    }
    if (!s->clk_win || !s->clk_dt)
        s->clk_raw = t;
    if (s->clk_win && s->clk_dt) {
        /* held up (a USB stall): a tick more than 1.5 lengths after the last is taken a length after it,
         * where it was due, and so are the ones that come bunched up after it (under a third of a length
         * apart), none later than it came. If the next comes a length or more on, not bunched, it was the
         * tempo slowing down: it is taken as it came, and the window starts again from it. */
        uint32_t prev = s->clk_t[(s->clk_n - 1u) % SEQ_CLK_HIST], gap = t - prev, raw = t - s->clk_raw;
        s->clk_raw = t;
        if ((!s->clk_late && gap > s->clk_dt + s->clk_dt / 2u) || (s->clk_late && raw < s->clk_dt / 3u)) {
            uint32_t due = prev + s->clk_dt;
            t = (int32_t)(t - due) < 0 ? t : due;
            s->clk_late = s->clk_late ? 2u : 1u;    /* (2: others came bunched after it) */
            held = 1;
        } else if (s->clk_late) {
            if (s->clk_late == 1u) {            /* it slowed: the length is the one it came after, and
                                                 * measured again from here */
                s->clk_win = 0;
                s->clk_dt = raw < CLK_DT_MIN ? CLK_DT_MIN : raw;
                s->clk_shown = s->clk_view = s->clk_dt << 4;
                s->clk_freq = 0;
                s->clk_k = 0;
            }
            s->clk_late = 0;
        }
    }
    s->clk_t[s->clk_n % SEQ_CLK_HIST] = t;
    s->clk_n++;
    s->clk_win += s->clk_win < SEQ_CLK_HIST;
    if (s->clk_win > 1u) {
        /* the length over the last beat (or the ticks since a change), smoothed (1/CLK_SMOOTH a tick,
         * Q4); a change past 4 % over the last 6 ticks is a new tempo: taken at once, the window and the
         * loop's integral start again from it */
        uint32_t m = s->clk_win - 1u < SEQ_CLK_HIST - 1u ? s->clk_win - 1u : SEQ_CLK_HIST - 1u;
        uint32_t d = mean_len(s, t, m), now = mean_len(s, t, m < CLK_CHANGE_TICKS ? m : CLK_CHANGE_TICKS);
        uint32_t was = s->clk_shown >> 4;
        if (!s->clk_shown || now * 100u > was * (100u + CLK_CHANGE_PCT) || now * (100u + CLK_CHANGE_PCT) < was * 100u) {
            if (s->clk_shown && m > CLK_CHANGE_TICKS)
                s->clk_win = CLK_CHANGE_TICKS + 1u;   /* (only the ticks at the new tempo) */
            s->clk_shown = s->clk_view = (s->clk_shown ? now : d) << 4;
            s->clk_freq = 0;
            s->clk_k = 0;                       /* (and the loop pulls hard again) */
        } else {
            s->clk_shown += (uint32_t)((int32_t)((d << 4) - s->clk_shown) / (int32_t)CLK_SMOOTH);
        }
        s->clk_dt = (s->clk_shown + 8u) >> 4;
        if (s->clk_dt < CLK_DT_MIN) {           /* (past any clock: 2500 BPM) */
            s->clk_dt = CLK_DT_MIN;
            s->clk_shown = CLK_DT_MIN << 4;
        }
        if (s->clk_view < CLK_DT_MIN << 4)      /* the tempo shown too: never past 2500.0 BPM */
            s->clk_view = CLK_DT_MIN << 4;
        s->clk_view += (uint32_t)((int32_t)(s->clk_shown - s->clk_view) / (int32_t)CLK_VIEW);   /* calmer, to read */
        hold_whole(s);
    }
    block_read(s, &bt, &bu, &binc, &fs);
    if (s->clk_pending) {                       /* (a clock that starts with the start: its length is
                                                 * the pattern's tempo's until a second tick) */
        if (!s->clk_dt) {
            s->clk_dt = CLK_TENTHS_COUNTS / s->edit.tempo;
            s->clk_shown = 0;                   /* (the second tick measures it outright) */
        }                       /* the start's tick 0 */
        s->ext_cont = s->clk_pending == 2u;
        blocked = s->out_block;                 /* #364: out_block is the gate (#349): while it is set the */
        s->out_block = 1;                       /* interrupt neither writes out_on nor sends a start or stop, */
        BARRIER();                              /* so out_on and the own run's stop are ours till the latch */
        if (s->out_on) {                        /* #286: its own run (or the last) stops first, and off (not */
            HANDOFF_GAP("clock_start", 0);      /* `playing`: a gated interrupt may have stopped the run and */
            s->out_on = 0;                      /* left out_on for us) */
            BARRIER();
            out_pass(s, 0xFCu);
        }
        HANDOFF_GAP("clock_start", 1);
        s->clk_started = 1;
        s->clk_pending = 0;
        s->clk_target = 0;
        s->clk_k = 0;
        s->ext_t0 = t;
        set_rate(s, 0, fs, 1);
        s->ext = 1;
        BARRIER();                              /* (following now: the interrupt counts no ticks of its own) */
        HANDOFF_GAP("clock_start", 2);
        s->clk_running = 1;
        s->clk_armed = 0;
        s->want = 1;
        s->ask_last = SEQ_LAST_PLAY;
        BARRIER();                              /* the rate and tick 0 before the ask */
        s->ask_play++;
        HANDOFF_GAP("clock_start", 3);
        BARRIER();                              /* #286: G14 latched after the ask, so a stop ask still */
        s->out_block = blocked;                 /* pending can't find it on and switch it off; the gate back */
        BARRIER();                              /* as it was (let go stays let go) */
        s->out_on = (uint8_t)(s->out_want && !blocked);
        if (s->out_on)
            out_pass(s, s->ext_cont ? 0xFBu : 0xFAu);
        HANDOFF_GAP("clock_start", 4);
        s->ext_hold = 0;                        /* (the interrupt takes the ask before it would step) */
        return;
    }
    if (s->clk_running && !s->clk_pending)
        s->ext_hold = 0;                        /* (a tick: on again) */
    if (s->clk_running && s->clk_dt && held) {  /* (a held-up tick: counted, the rate left till one comes
                                                 * as it should) */
        s->clk_target += fs * 100u;
        s->clk_k++;
    } else if (s->clk_running && s->clk_dt) {   /* where the steps were at this tick, against it */
        int32_t f = frames_q8((int32_t)(t - bt), fs);
        uint32_t at = bu + (uint32_t)(f >> 8) * binc + (((uint32_t)f & 255u) * binc >> 8);   /* (f < 0: wraps
                                                 * as the signed sum would, with no overflow) */
        s->clk_target += fs * 100u;
        s->clk_k++;
        set_rate(s, (int32_t)(s->clk_target - at), fs, 0);
    }
}

void seq_clock_in(seq *s, uint32_t st, uint32_t t)
{
    if (s->clk_src == SEQ_CLK_INT)
        return;
    switch (st) {
    case 0xF8u:
        clock_tick(s, t);
        if (s->clk_running && s->out_on)
            out_pass(s, 0xF8u);                 /* #286: passed on as it came, once (Eric, Q2) */
        break;
    case 0xFAu:                                 /* from step 1, on the next tick */
        s->clk_pending = 1;
        s->clk_pend_t = t;
        if (s->clk_running)
            s->ext_hold = 1;                    /* (playing: no next step till then) */
        break;
    case 0xFBu:                                 /* on from where it stopped, on the next tick (with no
                                                 * start before it, nowhere: from step 1) */
        if (!s->clk_running)
            s->clk_pending = s->clk_started ? 2u : 1u, s->clk_pend_t = t;
        break;
    case 0xFCu:
        s->clk_pending = 0;
        if (s->clk_running || s->clk_armed) {
            s->clk_running = 0;
            s->clk_armed = 0;
            seq_stop(s);
        }
        break;
    default: break;
    }
}

void seq_clock_poll(seq *s, uint32_t t)
{
    uint32_t gone = 3u * s->clk_dt > CLK_GONE_MIN ? 3u * s->clk_dt : CLK_GONE_MIN;
    uint32_t since = t - s->clk_t[(s->clk_n - 1u) % SEQ_CLK_HIST];
    if (!s->clk_present) {                      /* AUTO, a start with no clock after it: given up on (no clock
                                                 * is its own clock); MIDI waits on */
        if (s->clk_pending && !s->clk_running && s->clk_src == SEQ_CLK_AUTO && t - s->clk_pend_t > CLK_GONE_MIN)
            s->clk_pending = 0;
        return;
    }
    if (s->clk_running && since > s->clk_dt + s->clk_dt / 2u)
        s->ext_hold = 1;                        /* a tick late: no step past the clock's (till one comes) */
    if (since <= gone)
        return;
    s->clk_present = 0;                         /* gone: a transport it ran stops (Eric: as MIDI stop) */
    s->clk_pending = 0;
    if (s->clk_running || (s->clk_armed && s->clk_src == SEQ_CLK_AUTO)) {
        s->clk_running = 0;                     /* (AUTO armed for its next tick: that won't come, so
                                                 * disarmed as by a stop; MIDI armed waits on, by spec) */
        seq_stop(s);
    }
}

uint32_t seq_clock_external(const seq *s)
{
    if (s->clk_src != SEQ_CLK_AUTO)
        return s->clk_src == SEQ_CLK_MIDI;
    if (s->want && !s->clk_running && !s->clk_armed && !s->clk_pending)
        return 0;                               /* AUTO playing its own clock: a clock come in since doesn't take
                                                 * over without a start, so the steps are still its own (Codex) */
    return s->clk_present || s->clk_running;
}

uint32_t seq_clock_tempo(const seq *s)
{
    return s->clk_present && s->clk_view ? CLK_TENTHS_COUNTS / ((s->clk_view + 8u) >> 4) : 0;
}

uint32_t seq_clock_tempo_whole(const seq *s)
{
    return s->clk_present ? s->clk_whole : 0;
}

uint32_t seq_waiting(const seq *s)
{
    return s->clk_armed || (s->clk_pending && !s->clk_running);   /* (PLAY armed, or a start in with no tick) */
}

void seq_block_time(seq *s, uint32_t t)
{
    s->blk_seq++;                               /* odd: being written */
    BARRIER();
    s->blk_t = t;
    s->blk_u = s->upos;
    s->blk_inc = s->ext ? s->ext_inc : 0;
    s->blk_fs = s->fs;
    BARRIER();
    s->blk_seq++;
}

/* ---- the audio interrupt ---- */

/* the step playing's length in clock units: a step is fs x 600, and with swing the steps pair up, the
 * first of a pair (pos 0, 2, ...) 2 x swing % of a step long, the second the rest */
static uint32_t step_len(const seq *s, uint32_t fs)
{
    uint32_t first = fs * 12u * s->run.swing_pct;
    return s->pos & 1u ? fs * 1200u - first : first;
}

static uint32_t put(seq_event *ev, uint32_t cnt, uint32_t at, uint32_t note, uint32_t vel)
{
    if (cnt < SEQ_EVENTS) {
        ev[cnt].at = (uint16_t)at;
        ev[cnt].kind = SEQ_EV_NOTE;
        ev[cnt].note = (uint8_t)note;
        ev[cnt].vel = (uint8_t)vel;
        ev[cnt].value = 0;
        cnt++;
    }
    return cnt;
}

/* a bend or mod value at frame `at` (#42) */
static uint32_t put_ctl(seq_event *ev, uint32_t cnt, uint32_t at, uint32_t kind, int32_t value)
{
    if (cnt < SEQ_EVENTS) {
        ev[cnt].at = (uint16_t)at;
        ev[cnt].kind = (uint8_t)kind;
        ev[cnt].note = ev[cnt].vel = 0;
        ev[cnt].value = (int16_t)value;
        cnt++;
    }
    return cnt;
}

/* the bend and mod as `b` and `m` now: sent when they differ from the last (`force`: always) */
static uint32_t send_ctl(seq *s, seq_event *ev, uint32_t cnt, uint32_t at, int32_t b, int32_t m, int force)
{
    if (force || b != s->cur_bend)
        cnt = put_ctl(ev, cnt, at, SEQ_EV_BEND, b);
    if (force || m != s->cur_mod)
        cnt = put_ctl(ev, cnt, at, SEQ_EV_MOD, m);
    s->cur_bend = b;
    s->cur_mod = m;
    return cnt;
}

void seq_apply_event(fm_engine *e, const seq_event *ev, uint32_t live_mod)
{
    if (ev->kind == SEQ_EV_BEND)
        fm_engine_pitch_bend(e, (uint16_t)(ev->value + 8192));
    else if (ev->kind == SEQ_EV_MOD)
        fm_engine_controller(e, 1, (uint8_t)(ev->value == SEQ_MOD_LIVE ? live_mod & 127u : (uint32_t)ev->value));
    else if (ev->vel)
        fm_engine_note_on(e, ev->note, ev->vel);
    else
        fm_engine_note_off(e, ev->note);
}

/* every note the sequencer has on, let go at frame `at` */
static uint32_t let_go(seq *s, seq_event *ev, uint32_t cnt, uint32_t at)
{
    uint32_t k;
    for (k = 0; k < s->n_sounding; k++)
        cnt = put(ev, cnt, at, s->sounding[k], 0);
    s->n_sounding = 0;
    return cnt;
}

/* step `pos` starts at frame `at`: an edit published whole is taken first */
static uint32_t start_step(seq *s, seq_event *ev, uint32_t cnt, uint32_t at, uint32_t voices)
{
    const seq_step *st;
    uint32_t k, n;
    if (s->edits != s->edits_seen && !s->editing && s->edit_base == s->took_pattern) {   /* (#324) */
        s->run = s->edit;
        s->edits_seen = s->edits;
    }
    if (s->pos >= s->run.length) {             /* the end of a pass */
        s->pos = 0;
        uint32_t a = s->ask;
        if (!(a & 1u) && a != s->took) {        /* an open ask not acted on (#324) */
            if (s->ask_kind == SEQ_ASK_PATTERN) {   /* the queued pattern takes over (#40) */
                s->run = s->next;
                s->edits_seen = s->edits;       /* (an edit to the old one is dropped with it) */
                s->took_pattern = a;
            }
            s->took = a;
        }
    }
    s->run_transpose = s->transpose;            /* (a transpose plays from a step's start) */
    if (s->glide_on) {                          /* the last step's glide arrives, exactly */
        cnt = send_ctl(s, ev, cnt, at, s->to_bend, s->to_mod, 0);
        s->glide_on = 0;
    }
    st = &s->run.step[s->pos];
    if (st->kind != SEQ_TIE)
        cnt = let_go(s, ev, cnt, at);           /* (offs before ons, at the same frame) */
    if (st->kind == SEQ_NOTES_ON) {
        if (st->glide) {                        /* GLIDE: from where they are, over the step */
            s->from_bend = s->cur_bend;
            s->from_mod = s->cur_mod;
            s->to_bend = st->bend;
            s->to_mod = st->mod;
            s->glide_on = 1;
            s->glide_next = 0;                  /* (set by the render loop: a step's first update) */
        } else {                                /* JUMP: now, before its notes */
            cnt = send_ctl(s, ev, cnt, at, st->bend, st->mod, 1);
        }
        for (k = n = 0; k < st->n && n < voices; k++) {
            int32_t note = (int32_t)st->note[k] + s->run_transpose;
            if (note < 0 || note > 127)
                continue;                       /* pushed out of range: left out */
            cnt = put(ev, cnt, at, (uint32_t)note, st->vel);
            s->sounding[n++] = (uint8_t)note;   /* as played: its off matches */
        }
        s->n_sounding = (uint8_t)n;
    }
    s->gated = 0;
    s->step_now = (int32_t)s->pos;
    s->steps++;
    return cnt;
}

uint32_t seq_render(seq *s, uint32_t n, uint32_t fs, uint32_t voices, seq_event *ev)
{
    uint32_t cnt = 0, t = 0, thr, inc, gate, target, j, next, glide, xi;
    s->fs = fs;
    if (s->ask_play != s->play_seen || s->ask_stop != s->stop_seen) {
        /* the latest ask wins: play (again, from step 1) or stop. It is ask_last, written with each ask, not
         * `want`: an armed seq_play sets `want` with no ask of its own, so a stop then an armed PLAY (#349),
         * or a play, a stop and an armed PLAY (#373), are a stop. A play wins only with its own count new:
         * ask_last is written before the count, so a stop's block can see a play's ask_last early (gap
         * "play" 1). `want` must still be set, as a clock source change clears it with no ask */
        uint32_t go = s->ask_play != s->play_seen && s->ask_last == SEQ_LAST_PLAY && s->want;
        s->play_seen = s->ask_play;
        s->stop_seen = s->ask_stop;
        cnt = let_go(s, ev, cnt, 0);
        if (go && s->ext) {                     /* #38: the clock's start (or continue) */
            uint32_t late;
            int32_t f = frames_q8((int32_t)(s->blk_t - s->ext_t0), fs);   /* tick 0 was this long ago */
            f = f < 0 ? 0 : f;
            s->pos = s->ext_cont ? s->pos + 1u : 0;     /* (start_step wraps it at the length) */
            late = (uint32_t)(f >> 8) * s->ext_inc + (((uint32_t)f & 255u) * s->ext_inc >> 8);
            s->upos = late;
            s->acc = 0;
            s->cur_bend = s->cur_mod = 0;
            s->glide_on = 0;
            s->playing = 1;
            cnt = start_step(s, ev, cnt, 0, voices);
            thr = step_len(s, fs);
            s->acc = late < thr ? late : thr - 1u;  /* the step is as far on as tick 0 is gone */
        } else if (go) {
            if (!s->out_block) {                /* let go: out_on and its stop are the main loop's (#349) */
                if (s->playing && s->out_on)
                    out_put(s, 0xFCu);          /* #286: a restart: stop, then start (never two starts) */
                s->out_on = s->out_want;        /* #286: G14 as it is at the start */
                if (s->out_on) {
                    out_put(s, 0xFAu);          /* start, then the first tick, with step 1 */
                    s->out_acc = 0;
                    s->out_ticks++;
                }
            }
            s->pos = s->acc = 0;
            s->cur_bend = s->cur_mod = 0;       /* (a first GLIDE moves from center and 0) */
            s->glide_on = 0;
            s->playing = 1;
            cnt = start_step(s, ev, cnt, 0, voices);
        } else {
            if (!s->out_block) {                /* let go: out_on and its stop are the main loop's (#349) */
                if (s->playing && s->out_on)
                    out_put(s, 0xFCu);          /* #286: stop */
                s->out_on = 0;
            }
            if (s->playing) {                   /* bend to center, mod to the live wheel (#42) */
                cnt = put_ctl(ev, cnt, 0, SEQ_EV_BEND, 0);
                cnt = put_ctl(ev, cnt, 0, SEQ_EV_MOD, SEQ_MOD_LIVE);
            }
            s->glide_on = 0;
            s->playing = 0;
            s->step_now = -1;
        }
    }
    if (!s->playing)
        return cnt;
    xi = s->ext ? s->ext_inc : 0;
    s->upos += n * xi;
    while (t < n) {
        inc = xi ? xi : (uint32_t)s->run.tempo * SEQ_PPQ;
        thr = step_len(s, fs);                  /* (at 50 % swing, fs x 600: every step alike) */
        gate = thr / 100u * s->run.gate_pct;
        next = s->run.step[s->pos + 1u < s->run.length ? s->pos + 1u : 0].kind;
        target = !s->gated && s->n_sounding && gate < thr && next != SEQ_TIE ? gate : thr;
        if (s->glide_on && !s->glide_next)
            s->glide_next = inc * SEQ_GLIDE_FRAMES;    /* (the step's first update) */
        glide = s->glide_on && s->glide_next < thr ? s->glide_next : thr;
        target = glide < target ? glide : target;
        if (target == thr && xi && s->ext_hold) {   /* a start is coming: wait at this step's end */
            uint32_t to = s->acc + (n - t) * inc;
            s->acc = to < thr ? to : thr - 1u;
            s->upos -= to - s->acc;             /* (the clock waits as the step does) */
            break;
        }
        j = s->acc >= target ? 0 : (target - s->acc + inc - 1u) / inc;
        if (j >= n - t) {                       /* not in this block */
            s->acc += (n - t) * inc;
            out_adv(s, (n - t) * inc, xi);
            break;
        }
        t += j;
        s->acc += j * inc;
        out_adv(s, j * inc, xi);
        if (target == glide && glide != thr) {  /* a GLIDE update: where the step's clock has got to */
            uint32_t q = s->acc / (thr >> 10);  /* 0..1024 of the step */
            q = q > 1024u ? 1024u : q;
            cnt = send_ctl(s, ev, cnt, t, s->from_bend + (s->to_bend - s->from_bend) * (int32_t)q / 1024,
                           s->from_mod + (s->to_mod - s->from_mod) * (int32_t)q / 1024, 0);
            s->glide_next += inc * SEQ_GLIDE_FRAMES;
        } else if (target != thr) {             /* the gate: this step's notes go */
            cnt = let_go(s, ev, cnt, t);
            s->gated = 1;
        } else {                                /* the next step */
            s->acc -= thr;
            s->pos++;
            cnt = start_step(s, ev, cnt, t, voices);
        }
    }
    return cnt;
}
