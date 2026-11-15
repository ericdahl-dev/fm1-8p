/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p sequencer core (seq.h, #36). The clock: each frame adds tempo x SEQ_PPQ (tenths of a BPM x
 * steps a beat) to acc, and a step is fs x 600 of it (fs frames a second x 60 s x 10 tenths), so a step
 * starts on the first frame acc reaches it, exactly, with the remainder carried: no drift, no float,
 * and only 32-bit sums and divides. */
#include "seq.h"

/* the main loop's stores land in this order for the interrupt (play.c's BARRIER): a compiler barrier,
 * enough on the FM-1's one core, where the interrupt only ever runs between two instructions */
#define BARRIER() __asm__ volatile("" ::: "memory")

static void edit_begin(seq *s)
{
    s->editing = 1;                             /* the interrupt leaves `edit` alone until edit_end */
    BARRIER();                                  /* set before any store to `edit` */
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
    s->next_pending = s->next_taken = s->next_taken_seen = 0;
    s->editing = 0;
    s->edits = s->edits_seen = 0;
    s->ask_play = s->ask_stop = s->play_seen = s->stop_seen = 0;
    s->want = 0;
    s->transpose = s->run_transpose = 0;
    s->pos = s->acc = s->gated = 0;
    s->n_sounding = 0;
    s->cur_bend = s->cur_mod = 0;
    s->from_bend = s->from_mod = s->to_bend = s->to_mod = 0;
    s->glide_on = s->glide_next = 0;
    s->playing = 0;
    s->step_now = -1;
    s->steps = 0;
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

void seq_set_step(seq *s, uint32_t i, const uint8_t *notes, uint32_t n, uint32_t vel)
{
    seq_step *st;
    uint32_t k;
    if (i >= SEQ_STEPS)
        return;
    edit_begin(s);
    st = &s->edit.step[i];
    n = n > SEQ_NOTES ? SEQ_NOTES : n;
    for (k = 0; k < n; k++)
        st->note[k] = (uint8_t)(notes[k] & 0x7Fu);
    st->n = (uint8_t)n;
    st->vel = (uint8_t)(vel < 1u ? 1u : vel > 127u ? 127u : vel);
    st->kind = n ? SEQ_NOTES_ON : SEQ_REST;
    st->bend = 0;
    st->mod = 0;
    st->glide = 0;
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
    s->want = 1;
    BARRIER();                                  /* the wish before the ask: never an ask with an old one */
    s->ask_play++;
}

void seq_stop(seq *s)
{
    s->want = 0;
    BARRIER();
    s->ask_stop++;
    (void)seq_main_tick(s);                     /* (one the interrupt took already) */
    if (s->next_pending) {                      /* stopping with one queued: it loads at once */
        s->next_pending = 0;
        BARRIER();
        edit_begin(s);
        s->edit = s->next;
        edit_end(s);
        s->stored = s->next;
    }
}

void seq_load_pattern(seq *s, const seq_pattern *p)
{
    (void)seq_main_tick(s);
    if (s->want && s->playing) {                /* playing: queued for the end of the pass */
        s->next_pending = 0;                    /* (the interrupt leaves `next` alone while it's written) */
        BARRIER();
        s->next = *p;
        BARRIER();
        s->next_pending = 1;
        return;
    }
    s->next_pending = 0;
    edit_begin(s);
    s->edit = *p;
    edit_end(s);
    s->stored = *p;
}

uint32_t seq_next_queued(const seq *s)
{
    return s->next_pending;
}

void seq_cancel_next(seq *s)
{
    s->next_pending = 0;
}

uint32_t seq_main_tick(seq *s)
{
    if (s->next_taken == s->next_taken_seen)
        return 0;
    s->next_taken_seen = s->next_taken;
    edit_begin(s);
    s->edit = s->next;                          /* what the interrupt took */
    edit_end(s);
    s->stored = s->next;
    return 1;
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
    if (s->edits != s->edits_seen && !s->editing) {
        s->run = s->edit;
        s->edits_seen = s->edits;
    }
    if (s->pos >= s->run.length) {             /* the end of a pass */
        s->pos = 0;
        if (s->next_pending) {                  /* the queued pattern takes over (#40) */
            s->run = s->next;
            s->edits_seen = s->edits;           /* (an edit to the old one is dropped with it) */
            s->next_pending = 0;
            s->next_taken++;
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
    uint32_t cnt = 0, t = 0, thr, inc, gate, target, j, next, glide;
    if (s->ask_play != s->play_seen || s->ask_stop != s->stop_seen) {
        s->play_seen = s->ask_play;             /* the latest ask wins: play (again, from step 1) or stop */
        s->stop_seen = s->ask_stop;
        cnt = let_go(s, ev, cnt, 0);
        if (s->want) {
            s->pos = s->acc = 0;
            s->cur_bend = s->cur_mod = 0;       /* (a first GLIDE moves from center and 0) */
            s->glide_on = 0;
            s->playing = 1;
            cnt = start_step(s, ev, cnt, 0, voices);
        } else {
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
    while (t < n) {
        inc = (uint32_t)s->run.tempo * SEQ_PPQ;
        thr = step_len(s, fs);                  /* (at 50 % swing, fs x 600: every step alike) */
        gate = thr / 100u * s->run.gate_pct;
        next = s->run.step[s->pos + 1u < s->run.length ? s->pos + 1u : 0].kind;
        target = !s->gated && s->n_sounding && gate < thr && next != SEQ_TIE ? gate : thr;
        if (s->glide_on && !s->glide_next)
            s->glide_next = inc * SEQ_GLIDE_FRAMES;    /* (the step's first update) */
        glide = s->glide_on && s->glide_next < thr ? s->glide_next : thr;
        target = glide < target ? glide : target;
        j = s->acc >= target ? 0 : (target - s->acc + inc - 1u) / inc;
        if (j >= n - t) {                       /* not in this block */
            s->acc += (n - t) * inc;
            break;
        }
        t += j;
        s->acc += j * inc;
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
