/* The sequencer core (firmware/seq.c, #36): the pattern, the transport, and note events at exact sample
 * times, through its public API with a fake clock (the frames rendered). */
#include "../firmware/seq.h"

static seq sq;

/* every event rendered so far, at its frame from the play */
typedef struct {
    uint32_t t;
    uint8_t note, vel;
} seq_at;
static seq_at seq_log[2048];
static uint32_t seq_logged, seq_clock, seq_worst_block;

static void seq_fresh(void)
{
    seq_init(&sq);
    seq_logged = seq_clock = seq_worst_block = 0;
}

/* frames in blocks cycling through sizes[0..n_sizes-1] (each <= SEQ_BLOCK), into seq_log */
static void seq_run(uint32_t frames, const uint32_t *sizes, uint32_t n_sizes, uint32_t fs, uint32_t voices)
{
    static seq_event ev[SEQ_EVENTS];
    uint32_t i = 0, k, n, got;
    while (frames) {
        n = sizes[i++ % n_sizes];
        n = n < frames ? n : frames;
        got = seq_render(&sq, n, fs, voices, ev);
        seq_worst_block = got > seq_worst_block ? got : seq_worst_block;
        for (k = 0; k < got && seq_logged < sizeof seq_log / sizeof seq_log[0]; k++) {
            seq_log[seq_logged].t = seq_clock + ev[k].at;
            seq_log[seq_logged].note = ev[k].note;
            seq_log[seq_logged].vel = ev[k].vel;
            seq_logged++;
        }
        seq_clock += n;
        frames -= n;
    }
}

static const uint32_t SEQ_256[] = {256};

/* step k's start and its gate's end, in frames from the play: the first frame the clock (tempo x 4 a
 * frame) reaches k steps of fs x 600, closed form (64-bit here, in the test only) */
static uint32_t seq_start_at(uint32_t k, uint32_t fs, uint32_t tempo)
{
    uint64_t thr = (uint64_t)fs * 600u, inc = (uint64_t)tempo * SEQ_PPQ;
    return (uint32_t)((k * thr + inc - 1u) / inc);
}
static uint32_t seq_gate_at(uint32_t k, uint32_t fs, uint32_t tempo, uint32_t gate)
{
    uint64_t thr = (uint64_t)fs * 600u, inc = (uint64_t)tempo * SEQ_PPQ;
    return (uint32_t)((k * thr + thr / 100u * gate + inc - 1u) / inc);
}

/* A fresh sequencer: 16 rests, the defaults, stopped. */
TT_TEST(seq_starts_empty_and_stopped)
{
    uint32_t i;
    seq_fresh();
    TT_CHECK_EQ(seq_length(&sq), SEQ_STEPS);
    TT_CHECK_EQ(seq_tempo(&sq), SEQ_TEMPO_DEF);
    for (i = 0; i < SEQ_STEPS; i++)
        TT_CHECK_EQ(seq_get_step(&sq, i)->kind, SEQ_REST);
    TT_CHECK_EQ(seq_playing(&sq), 0u);
    TT_CHECK_EQ(seq_step_now(&sq), -1);
    seq_run(44100, SEQ_256, 1, 44100, 6);       /* stopped: nothing */
    TT_CHECK_EQ(seq_logged, 0u);
}

/* The edit calls: a step's notes (cut to SEQ_NOTES; none is a rest), a rest, a tie and back, and the
 * length and tempo clamped. */
TT_TEST(seq_edits_steps_length_and_tempo)
{
    static const uint8_t chord[8] = {60, 64, 67, 71, 74, 77, 81, 84};
    seq_fresh();
    seq_set_step(&sq, 2, chord, 8, 90);
    TT_CHECK_EQ(seq_get_step(&sq, 2)->kind, SEQ_NOTES_ON);
    TT_CHECK_EQ(seq_get_step(&sq, 2)->n, SEQ_NOTES);
    TT_CHECK_EQ(seq_get_step(&sq, 2)->vel, 90u);
    TT_CHECK_EQ(seq_get_step(&sq, 2)->note[5], 77u);
    seq_set_step(&sq, 3, chord, 0, 90);
    TT_CHECK_EQ(seq_get_step(&sq, 3)->kind, SEQ_REST);
    seq_toggle_tie(&sq, 4);
    TT_CHECK_EQ(seq_get_step(&sq, 4)->kind, SEQ_TIE);
    seq_toggle_tie(&sq, 4);
    TT_CHECK_EQ(seq_get_step(&sq, 4)->kind, SEQ_REST);
    seq_clear_step(&sq, 2);
    TT_CHECK_EQ(seq_get_step(&sq, 2)->kind, SEQ_REST);
    seq_set_length(&sq, 0);
    TT_CHECK_EQ(seq_length(&sq), 1u);
    seq_set_length(&sq, 99);
    TT_CHECK_EQ(seq_length(&sq), SEQ_STEPS);
    seq_set_tempo(&sq, 1);
    TT_CHECK_EQ(seq_tempo(&sq), SEQ_TEMPO_MIN);
    seq_set_tempo(&sq, 99999);
    TT_CHECK_EQ(seq_tempo(&sq), SEQ_TEMPO_MAX);
    seq_set_gate(&sq, 1);                       /* interaction.md: GATE 5..100 % */
    TT_CHECK_EQ(sq.edit.gate_pct, SEQ_GATE_MIN);
    seq_set_gate(&sq, 101);
    TT_CHECK_EQ(sq.edit.gate_pct, 100u);
    TT_CHECK(seq_get_step(&sq, SEQ_STEPS) == 0);   /* past the end: none */
}

/* Every step's note fires at its exact frame, and its gate lets it go at its exact frame, at an awkward
 * tempo (137.3 BPM), around the pattern several times: the clock carries its remainder, never drifts. */
TT_TEST(seq_steps_fire_at_their_exact_frames)
{
    uint32_t i, k, fs = 44100, tempo = 1373, len = 5;
    seq_fresh();
    seq_set_tempo(&sq, tempo);
    seq_set_length(&sq, len);
    for (i = 0; i < len; i++) {
        uint8_t note = (uint8_t)(60 + i);
        seq_set_step(&sq, i, &note, 1, 100);
    }
    seq_play(&sq);
    seq_run(seq_start_at(41, fs, tempo) - 1u, SEQ_256, 1, fs, 6);   /* steps 0..40 */
    TT_CHECK_EQ(seq_logged, 2u * 41u);
    for (k = 0; k < 41u; k++) {
        TT_CHECK_EQ(seq_log[2u * k].t, seq_start_at(k, fs, tempo));
        TT_CHECK_EQ(seq_log[2u * k].note, 60u + k % len);
        TT_CHECK_EQ(seq_log[2u * k].vel, 100u);
        TT_CHECK_EQ(seq_log[2u * k + 1u].t, seq_gate_at(k, fs, tempo, SEQ_GATE_DEF));
        TT_CHECK_EQ(seq_log[2u * k + 1u].note, 60u + k % len);
        TT_CHECK_EQ(seq_log[2u * k + 1u].vel, 0u);
    }
    TT_CHECK_EQ(seq_step_now(&sq), 40 % 5);
}

/* The same pattern rendered in blocks of 1, 7, 256 and a mix gives the same events at the same frames. */
TT_TEST(seq_events_do_not_depend_on_the_block_size)
{
    static const uint32_t ONE[] = {1}, SEVEN[] = {7}, MIX[] = {256, 1, 33, 200, 2, 97, 256, 3};
    static const uint32_t *SIZES[] = {ONE, SEVEN, SEQ_256, MIX};
    static const uint32_t N_SIZES[] = {1, 1, 1, 8};
    static seq_at first[256];
    static const uint8_t chord[3] = {48, 55, 64};
    uint32_t r, i, n_first = 0;
    for (r = 0; r < 4u; r++) {
        seq_fresh();
        seq_set_tempo(&sq, 2999);
        seq_set_step(&sq, 0, chord, 3, 127);
        seq_toggle_tie(&sq, 1);
        seq_set_step(&sq, 3, chord + 1, 1, 64);
        seq_set_length(&sq, 4);
        seq_play(&sq);
        seq_run(48000, SIZES[r], N_SIZES[r], 48000, 6);
        TT_CHECK(seq_logged > 0u && seq_logged <= 256u);
        if (r == 0) {
            n_first = seq_logged;
            for (i = 0; i < seq_logged; i++)
                first[i] = seq_log[i];
            continue;
        }
        TT_CHECK_EQ(seq_logged, n_first);
        for (i = 0; i < seq_logged; i++) {
            TT_CHECK_EQ(seq_log[i].t, first[i].t);
            TT_CHECK_EQ(seq_log[i].note, first[i].note);
            TT_CHECK_EQ(seq_log[i].vel, first[i].vel);
        }
    }
}

/* A tie holds the notes on through the next step (no off at the first gate, no new on), and they go at
 * the tie step's gate; a rest plays nothing. */
TT_TEST(seq_tie_holds_and_rest_is_silent)
{
    uint32_t fs = 44100, tempo = 1200;
    uint8_t note = 60;
    seq_fresh();
    seq_set_tempo(&sq, tempo);
    seq_set_length(&sq, 4);
    seq_set_step(&sq, 0, &note, 1, 100);
    seq_toggle_tie(&sq, 1);                     /* 0 holds through 1 */
    /* 2 and 3: rests */
    seq_play(&sq);
    seq_run(seq_start_at(4, fs, tempo) + 1u, SEQ_256, 1, fs, 6);   /* once round, and step 0 again */
    TT_CHECK_EQ(seq_logged, 3u);
    TT_CHECK_EQ(seq_log[0].t, 0u);
    TT_CHECK_EQ(seq_log[0].vel, 100u);
    TT_CHECK_EQ(seq_log[1].t, seq_gate_at(1, fs, tempo, SEQ_GATE_DEF));   /* the tie step's gate */
    TT_CHECK_EQ(seq_log[1].vel, 0u);
    TT_CHECK_EQ(seq_log[2].t, seq_start_at(4, fs, tempo));   /* round again: step 0 */
    TT_CHECK_EQ(seq_log[2].vel, 100u);
}

/* A tie on step 1 at the wrap holds the last step's notes; a tie with nothing sounding (the first time
 * through) plays nothing. */
TT_TEST(seq_tie_at_the_wrap_holds_the_last_step)
{
    uint32_t fs = 44100, tempo = 1200;
    uint8_t note = 67;
    seq_fresh();
    seq_set_tempo(&sq, tempo);
    seq_set_length(&sq, 2);
    seq_toggle_tie(&sq, 0);
    seq_set_step(&sq, 1, &note, 1, 100);
    seq_play(&sq);
    seq_run(seq_start_at(4, fs, tempo), SEQ_256, 1, fs, 6);   /* steps 0 (tie, nothing), 1, 0 (tie), 1 */
    TT_CHECK_EQ(seq_logged, 3u);
    TT_CHECK_EQ(seq_log[0].t, seq_start_at(1, fs, tempo));
    TT_CHECK_EQ(seq_log[1].t, seq_gate_at(2, fs, tempo, SEQ_GATE_DEF));   /* held through the wrap */
    TT_CHECK_EQ(seq_log[1].vel, 0u);
    TT_CHECK_EQ(seq_log[2].t, seq_start_at(3, fs, tempo));
}

/* A chord step never asks for more notes than the voices: the first `voices` of it. */
TT_TEST(seq_chord_never_exceeds_the_voices)
{
    static const uint8_t chord[6] = {48, 52, 55, 59, 62, 65};
    uint32_t i, ons = 0;
    seq_fresh();
    seq_set_step(&sq, 0, chord, 6, 100);
    seq_set_length(&sq, 1);
    seq_play(&sq);
    seq_run(1000, SEQ_256, 1, 44100, 4);
    for (i = 0; i < seq_logged; i++)
        if (seq_log[i].t == 0 && seq_log[i].vel) {
            TT_CHECK_EQ(seq_log[i].note, chord[ons]);
            ons++;
        }
    TT_CHECK_EQ(ons, 4u);
}

/* Stop lets go of every note the sequencer has on, at the next block's start, and nothing plays after;
 * play again starts at step 1. */
TT_TEST(seq_stop_releases_every_note)
{
    static const uint8_t chord[3] = {60, 64, 67};
    uint32_t i, ons = 0, offs = 0;
    seq_fresh();
    seq_set_step(&sq, 0, chord, 3, 100);
    seq_toggle_tie(&sq, 1);
    seq_set_length(&sq, 2);
    seq_play(&sq);
    seq_run(5000, SEQ_256, 1, 44100, 6);       /* in step 0, the chord on */
    TT_CHECK_EQ(seq_playing(&sq), 1u);
    seq_stop(&sq);
    TT_CHECK_EQ(seq_playing(&sq), 1u);          /* until the interrupt has let the notes go */
    seq_run(256, SEQ_256, 1, 44100, 6);
    TT_CHECK_EQ(seq_playing(&sq), 0u);
    TT_CHECK_EQ(seq_step_now(&sq), -1);
    for (i = 0; i < seq_logged; i++) {
        ons += seq_log[i].vel != 0;
        offs += seq_log[i].vel == 0;
        if (seq_log[i].vel == 0)
            TT_CHECK_EQ(seq_log[i].t, 5000u);   /* the block after the stop: its first frame */
    }
    TT_CHECK_EQ(ons, 3u);
    TT_CHECK_EQ(offs, 3u);
    seq_run(44100, SEQ_256, 1, 44100, 6);
    TT_CHECK_EQ(seq_logged, 6u);                /* nothing more */
    seq_play(&sq);
    seq_run(256, SEQ_256, 1, 44100, 6);
    TT_CHECK_EQ(seq_step_now(&sq), 0);
    TT_CHECK_EQ(seq_logged, 9u);                /* step 1's chord again */
}

/* play_silent's half (#201): playing from the ask, before the interrupt has started anything. */
TT_TEST(seq_playing_from_the_ask)
{
    seq_fresh();
    seq_play(&sq);
    TT_CHECK_EQ(seq_playing(&sq), 1u);
    TT_CHECK_EQ(seq_step_now(&sq), -1);         /* not started yet */
    seq_run(1, SEQ_256, 1, 44100, 6);
    TT_CHECK_EQ(seq_step_now(&sq), 0);
}

/* An edit is taken at a step's start, never in the middle of one, and not while the main loop is part
 * way through it (editing set); the step counter moves once a step. */
TT_TEST(seq_edits_are_taken_whole_at_a_step_start)
{
    uint32_t fs = 44100, tempo = 1200, steps0;
    uint8_t a = 60, b = 72;
    seq_fresh();
    seq_set_tempo(&sq, tempo);
    seq_set_length(&sq, 1);
    seq_set_step(&sq, 0, &a, 1, 100);
    seq_play(&sq);
    seq_run(1000, SEQ_256, 1, fs, 6);
    steps0 = seq_steps(&sq);
    seq_set_step(&sq, 0, &b, 1, 100);           /* mid-step: the next start takes it */
    seq_run(seq_start_at(1, fs, tempo) + 1u - 1000u, SEQ_256, 1, fs, 6);
    TT_CHECK_EQ(seq_steps(&sq), steps0 + 1u);
    TT_CHECK_EQ(seq_log[seq_logged - 1u].note, 72u);
    TT_CHECK_EQ(seq_log[seq_logged - 1u].vel, 100u);
    sq.editing = 1;                             /* part way through an edit at the next start */
    sq.edit.step[0].note[0] = 50;
    sq.edits++;
    seq_run(seq_start_at(2, fs, tempo) - seq_start_at(1, fs, tempo), SEQ_256, 1, fs, 6);
    TT_CHECK_EQ(seq_log[seq_logged - 1u].note, 72u);   /* the old step again */
    sq.editing = 0;
    seq_run(seq_start_at(3, fs, tempo) - seq_start_at(2, fs, tempo), SEQ_256, 1, fs, 6);
    TT_CHECK_EQ(seq_log[seq_logged - 1u].note, 50u);
}

/* A 100 % gate lets the notes go at the next step's start, before its notes: an off before an on at
 * the same frame, once. */
TT_TEST(seq_full_gate_lets_go_before_the_next_on)
{
    uint32_t fs = 44100, tempo = 1200;
    uint8_t note = 60;
    seq_fresh();
    seq_set_tempo(&sq, tempo);
    seq_set_gate(&sq, 100);
    seq_set_length(&sq, 1);
    seq_set_step(&sq, 0, &note, 1, 100);
    seq_play(&sq);
    seq_run(seq_start_at(1, fs, tempo) + 1u, SEQ_256, 1, fs, 6);
    TT_CHECK_EQ(seq_logged, 3u);
    TT_CHECK_EQ(seq_log[1].t, seq_start_at(1, fs, tempo));
    TT_CHECK_EQ(seq_log[1].vel, 0u);
    TT_CHECK_EQ(seq_log[2].t, seq_start_at(1, fs, tempo));
    TT_CHECK_EQ(seq_log[2].vel, 100u);
}

/* The busiest block (6-note chords, the shortest gate, 300 BPM at 8 kHz, blocks of 256) stays within
 * SEQ_EVENTS, and the clock fits 32 bits at 192 kHz. */
TT_TEST(seq_busiest_block_fits)
{
    static const uint8_t chord[6] = {48, 52, 55, 59, 62, 65};
    uint32_t i;
    seq_fresh();
    seq_set_tempo(&sq, SEQ_TEMPO_MAX);
    seq_set_gate(&sq, SEQ_GATE_MIN);
    for (i = 0; i < SEQ_STEPS; i++)
        seq_set_step(&sq, i, chord, 6, 100);
    seq_play(&sq);
    seq_run(8000 * 4, SEQ_256, 1, 8000, 6);
    seq_stop(&sq);
    seq_run(256, SEQ_256, 1, 8000, 6);
    TT_CHECK(seq_worst_block <= SEQ_EVENTS);
    TT_CHECK(seq_logged > 0u);
    {                                           /* none dropped: every on has its off (put clamps silently) */
        uint32_t i, ons = 0, offs = 0;
        for (i = 0; i < seq_logged; i++) {
            ons += seq_log[i].vel != 0;
            offs += seq_log[i].vel == 0;
        }
        TT_CHECK_EQ(ons, offs);
    }
    TT_CHECK((uint64_t)192000u * 600u + (uint64_t)SEQ_TEMPO_MAX * SEQ_PPQ * SEQ_BLOCK < 0x100000000ull);
}

/* step k's start with swing (pct): steps pair up, the first of a pair fs x 12 x swing long, the second
 * the rest of fs x 1200; closed form, 64-bit in the test only */
static uint32_t seq_swing_start_at(uint32_t k, uint32_t fs, uint32_t tempo, uint32_t swing)
{
    uint64_t pair = (uint64_t)fs * 1200u, first = (uint64_t)fs * 12u * swing, inc = (uint64_t)tempo * SEQ_PPQ;
    uint64_t at = (k / 2u) * pair + (k % 2u) * first;
    return (uint32_t)((at + inc - 1u) / inc);
}
static uint32_t seq_swing_gate_at(uint32_t k, uint32_t fs, uint32_t tempo, uint32_t swing, uint32_t gate)
{
    uint64_t pair = (uint64_t)fs * 1200u, first = (uint64_t)fs * 12u * swing, inc = (uint64_t)tempo * SEQ_PPQ;
    uint64_t len = k % 2u ? pair - first : first;
    uint64_t at = (k / 2u) * pair + (k % 2u) * first + len / 100u * gate;
    return (uint32_t)((at + inc - 1u) / inc);
}

/* #37: swing moves the even steps (2, 4, ...) late by its amount, exactly, and each step's gate is its
 * own length's share; the odd steps stay on the beat. */
TT_TEST(seq_swing_moves_even_steps_exactly)
{
    uint32_t i, k, fs = 44100, tempo = 1373, swing = 66, gate = 40, len = 4;
    seq_fresh();
    seq_set_tempo(&sq, tempo);
    seq_set_swing(&sq, swing);
    seq_set_gate(&sq, gate);
    seq_set_length(&sq, len);
    for (i = 0; i < len; i++) {
        uint8_t note = (uint8_t)(60 + i);
        seq_set_step(&sq, i, &note, 1, 100);
    }
    seq_play(&sq);
    seq_run(seq_swing_start_at(21, fs, tempo, swing) - 1u, SEQ_256, 1, fs, 6);
    TT_CHECK_EQ(seq_logged, 2u * 21u);
    for (k = 0; k < 21u; k++) {
        TT_CHECK_EQ(seq_log[2u * k].t, seq_swing_start_at(k, fs, tempo, swing));
        TT_CHECK_EQ(seq_log[2u * k + 1u].t, seq_swing_gate_at(k, fs, tempo, swing, gate));
    }
    TT_CHECK(seq_log[2].t > seq_start_at(1, fs, tempo));    /* step 2 late */
    TT_CHECK_EQ(seq_log[4].t, seq_start_at(2, fs, tempo));  /* step 3 on the beat */
}

/* Swing is 50..75 % (interaction.md, S1 PATTERN); 50 is straight. */
TT_TEST(seq_swing_is_clamped)
{
    seq_fresh();
    seq_set_swing(&sq, 20);
    TT_CHECK_EQ(sq.edit.swing_pct, 50u);
    seq_set_swing(&sq, 90);
    TT_CHECK_EQ(sq.edit.swing_pct, 75u);
}

/* #37: a transpose change plays from the next step; the notes sounding are let go as they were played,
 * so nothing hangs. It is -24..+24, and a note it pushes outside 0..127 is left out. */
TT_TEST(seq_transpose_takes_effect_at_the_next_step)
{
    static const uint8_t chord[2] = {60, 120};
    uint32_t fs = 44100, tempo = 1200, i, ons = 0, offs = 0;
    seq_fresh();
    seq_set_tempo(&sq, tempo);
    seq_set_gate(&sq, 100);                     /* held to the next step: the off comes with it */
    seq_set_length(&sq, 1);
    seq_set_step(&sq, 0, chord, 2, 100);
    seq_play(&sq);
    seq_run(1000, SEQ_256, 1, fs, 6);           /* step 0: 60 and 120 */
    seq_set_transpose(&sq, 12);
    TT_CHECK_EQ(seq_transpose(&sq), 12);
    seq_run(seq_start_at(1, fs, tempo) + 1u - 1000u, SEQ_256, 1, fs, 6);
    TT_CHECK_EQ(seq_logged, 2u + 2u + 1u);      /* on 60, 120; off 60, 120; on 72 (132 left out) */
    TT_CHECK_EQ(seq_log[2].note, 60u);
    TT_CHECK_EQ(seq_log[2].vel, 0u);
    TT_CHECK_EQ(seq_log[3].note, 120u);
    TT_CHECK_EQ(seq_log[3].vel, 0u);
    TT_CHECK_EQ(seq_log[4].note, 72u);
    TT_CHECK_EQ(seq_log[4].vel, 100u);
    seq_set_transpose(&sq, -99);
    TT_CHECK_EQ(seq_transpose(&sq), -24);
    seq_set_transpose(&sq, 99);
    TT_CHECK_EQ(seq_transpose(&sq), 24);
    seq_stop(&sq);
    seq_run(256, SEQ_256, 1, fs, 6);
    for (i = 0; i < seq_logged; i++) {
        ons += seq_log[i].vel != 0;
        offs += seq_log[i].vel == 0;
    }
    TT_CHECK_EQ(ons, offs);                     /* nothing hangs */
}

/* two patterns told apart by their first note, `len` steps each, every step that note */
static void seq_two(seq_pattern *a, seq_pattern *b, uint32_t len)
{
    uint32_t i;
    seq_pattern_empty(a);
    seq_pattern_empty(b);
    a->length = b->length = (uint8_t)len;
    for (i = 0; i < len; i++) {
        a->step[i].kind = b->step[i].kind = SEQ_NOTES_ON;
        a->step[i].n = b->step[i].n = 1;
        a->step[i].vel = b->step[i].vel = 100;
        a->step[i].note[0] = 60;
        b->step[i].note[0] = 72;
    }
}

/* #40: stopped, a pattern loads at once: the edit buffer is it, it is not edited, and play plays it. */
TT_TEST(seq_load_stopped_loads_at_once)
{
    static seq_pattern a, b;
    seq_fresh();
    seq_two(&a, &b, 3);
    seq_load_pattern(&sq, &b);
    TT_CHECK_EQ(seq_length(&sq), 3u);
    TT_CHECK_EQ(seq_get_step(&sq, 0)->note[0], 72u);
    TT_CHECK_EQ(seq_edited(&sq), 0u);
    TT_CHECK_EQ(seq_next_queued(&sq), 0u);
    seq_play(&sq);
    seq_run(256, SEQ_256, 1, 44100, 6);
    TT_CHECK_EQ(seq_log[0].note, 72u);
}

/* #40, check 51: playing, a load waits for the running pattern's pass to end; the new pattern's step 1
 * plays right after the running one's last step, and the main loop then has it in the edit buffer. */
TT_TEST(seq_load_playing_takes_over_after_the_pass)
{
    static seq_pattern a, b;
    uint32_t fs = 44100, tempo = 1200, i, at;
    seq_fresh();
    seq_two(&a, &b, 3);
    seq_set_tempo(&sq, tempo);
    a.tempo = b.tempo = (uint16_t)tempo;
    seq_load_pattern(&sq, &a);
    seq_play(&sq);
    seq_run(1000, SEQ_256, 1, fs, 6);           /* in step 1 of a */
    seq_load_pattern(&sq, &b);
    TT_CHECK_EQ(seq_next_queued(&sq), 1u);
    TT_CHECK_EQ(seq_get_step(&sq, 0)->note[0], 60u);    /* the edit buffer is still the running one */
    seq_run(seq_start_at(4, fs, tempo) + 1u - 1000u, SEQ_256, 1, fs, 6);
    for (at = 0, i = 0; i < seq_logged; i++)
        if (seq_log[i].vel && seq_log[i].note == 72u) {
            at = seq_log[i].t;
            break;
        }
    TT_CHECK_EQ(at, seq_start_at(3, fs, tempo));        /* right after a's step 3 */
    for (i = 0; i < seq_logged; i++)
        if (seq_log[i].t < seq_start_at(3, fs, tempo) && seq_log[i].vel)
            TT_CHECK_EQ(seq_log[i].note, 60u);          /* a's pass played whole */
    TT_CHECK_EQ(seq_main_tick(&sq), 1u);                /* the main loop takes it */
    TT_CHECK_EQ(seq_get_step(&sq, 0)->note[0], 72u);
    TT_CHECK_EQ(seq_next_queued(&sq), 0u);
    TT_CHECK_EQ(seq_edited(&sq), 0u);
    TT_CHECK_EQ(seq_main_tick(&sq), 0u);
}

/* #40, check 52: canceling the queued pattern (turning back to the running one's number) keeps the
 * running pattern going round. */
TT_TEST(seq_load_queued_can_be_canceled)
{
    static seq_pattern a, b;
    uint32_t fs = 44100, tempo = 1200, i;
    seq_fresh();
    seq_two(&a, &b, 2);
    a.tempo = (uint16_t)tempo;
    seq_load_pattern(&sq, &a);
    seq_play(&sq);
    seq_run(1000, SEQ_256, 1, fs, 6);
    seq_load_pattern(&sq, &b);
    seq_cancel_next(&sq);
    TT_CHECK_EQ(seq_next_queued(&sq), 0u);
    seq_run(seq_start_at(5, fs, tempo), SEQ_256, 1, fs, 6);
    for (i = 0; i < seq_logged; i++)
        TT_CHECK(!seq_log[i].vel || seq_log[i].note == 60u);
    TT_CHECK_EQ(seq_main_tick(&sq), 0u);
}

/* #40, check 53: stopping with a pattern queued loads it at once; a second load before the pass ends
 * replaces the first. */
TT_TEST(seq_stop_loads_the_queued_pattern)
{
    static seq_pattern a, b, c;
    seq_fresh();
    seq_two(&a, &b, 2);
    seq_two(&c, &c, 5);
    seq_load_pattern(&sq, &a);
    seq_play(&sq);
    seq_run(1000, SEQ_256, 1, 44100, 6);
    seq_load_pattern(&sq, &b);
    seq_load_pattern(&sq, &c);                  /* the latest queued wins */
    seq_stop(&sq);
    TT_CHECK_EQ(seq_next_queued(&sq), 0u);
    TT_CHECK_EQ(seq_length(&sq), 5u);
    TT_CHECK_EQ(seq_get_step(&sq, 0)->note[0], 72u);
    TT_CHECK_EQ(seq_edited(&sq), 0u);
}

/* The PATTERN EDITED marker: off after a load or a save, on after an edit. */
TT_TEST(seq_edited_marks_an_edit_until_saved)
{
    static seq_pattern a, b;
    uint8_t note = 64;
    seq_fresh();
    seq_two(&a, &b, 4);
    seq_load_pattern(&sq, &a);
    TT_CHECK_EQ(seq_edited(&sq), 0u);
    seq_set_step(&sq, 2, &note, 1, 90);
    TT_CHECK_EQ(seq_edited(&sq), 1u);
    seq_mark_saved(&sq);
    TT_CHECK_EQ(seq_edited(&sq), 0u);
    seq_set_tempo(&sq, 1500);
    TT_CHECK_EQ(seq_edited(&sq), 1u);
    seq_load_pattern(&sq, &a);
    TT_CHECK_EQ(seq_edited(&sq), 0u);
}

static void run_seq_tests(void)
{
    TT_RUN(seq_starts_empty_and_stopped);
    TT_RUN(seq_edits_steps_length_and_tempo);
    TT_RUN(seq_steps_fire_at_their_exact_frames);
    TT_RUN(seq_events_do_not_depend_on_the_block_size);
    TT_RUN(seq_tie_holds_and_rest_is_silent);
    TT_RUN(seq_tie_at_the_wrap_holds_the_last_step);
    TT_RUN(seq_chord_never_exceeds_the_voices);
    TT_RUN(seq_stop_releases_every_note);
    TT_RUN(seq_playing_from_the_ask);
    TT_RUN(seq_edits_are_taken_whole_at_a_step_start);
    TT_RUN(seq_full_gate_lets_go_before_the_next_on);
    TT_RUN(seq_busiest_block_fits);
    TT_RUN(seq_swing_moves_even_steps_exactly);
    TT_RUN(seq_swing_is_clamped);
    TT_RUN(seq_transpose_takes_effect_at_the_next_step);
    TT_RUN(seq_load_stopped_loads_at_once);
    TT_RUN(seq_load_playing_takes_over_after_the_pass);
    TT_RUN(seq_load_queued_can_be_canceled);
    TT_RUN(seq_stop_loads_the_queued_pattern);
    TT_RUN(seq_edited_marks_an_edit_until_saved);
}
