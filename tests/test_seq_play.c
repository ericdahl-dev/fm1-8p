/* The sequencer in play (firmware/play.c, #36, #201): play owns a seq, play_render applies its notes at
 * their frames, and the unit isn't silent while it runs. */

static play sp_a, sp_b;
static int32_t sp_out_a[2 * PLAY_BLOCK], sp_out_b[2 * PLAY_BLOCK];

static void sp_render(play *p, int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++)
        out[i] = 0;
    play_render(p, out, n);
}

/* The same pattern on a bare seq (seq_render alone, test_seq.c's log) and in play: */
static void sp_pattern(seq *s)
{
    uint8_t note = 60;
    seq_set_tempo(s, 1373);
    seq_set_length(s, 2);
    seq_set_step(s, 0, &note, 1, 100);
    note = 67;
    seq_set_step(s, 1, &note, 1, 90);
    seq_play(s);
}

/* The sequencer's notes sound exactly as the same notes played by hand at those frames (each block cut
 * there, the note sent between the two renders): same samples, bit for bit. */
TT_TEST(seq_play_note_lands_on_its_frame)
{
    uint32_t i, k, at, e = 0, diff = 0, blocks = 40;
    seq_fresh();
    sp_pattern(&sq);
    seq_run(blocks * PLAY_BLOCK, SEQ_256, 1, PLAY_FS, PLAY_VOICES);   /* where the notes fall */
    (void)play_init(&sp_a);
    (void)play_init(&sp_b);
    sp_pattern(play_seq(&sp_a));
    for (k = 0; k < blocks; k++) {
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
        for (i = 0; i < 2u * PLAY_BLOCK; i++)
            sp_out_b[i] = 0;
        at = 0;
        for (; e < seq_logged && seq_log[e].t / PLAY_BLOCK == k; e++) {
            uint32_t f = seq_log[e].t % PLAY_BLOCK;
            if (f > at) {
                play_render(&sp_b, sp_out_b + 2u * at, f - at);
                at = f;
            }
            play_note(&sp_b, seq_log[e].note, seq_log[e].vel);
        }
        play_render(&sp_b, sp_out_b + 2u * at, PLAY_BLOCK - at);
        for (i = 0; i < 2u * PLAY_BLOCK; i++)
            diff += sp_out_a[i] != sp_out_b[i];
    }
    TT_CHECK(seq_logged >= 4u);                 /* two steps' ons and gates at least */
    TT_CHECK(seq_log[1].t % PLAY_BLOCK != 0);   /* (a frame inside a block, not only block starts) */
    TT_CHECK_EQ(diff, 0u);
}

/* #201: a running sequencer is not silent, even on a rest with no voice sounding, so a SAVE waits;
 * from the ask on, and until the stop has been taken. */
TT_TEST(seq_play_running_is_not_silent)
{
    (void)play_init(&sp_a);
    TT_CHECK_EQ(play_silent(&sp_a), 1u);
    seq_play(play_seq(&sp_a));                  /* every step a rest */
    TT_CHECK_EQ(play_silent(&sp_a), 0u);        /* from the ask */
    sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK_EQ(play_silent(&sp_a), 0u);
    seq_stop(play_seq(&sp_a));
    sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK_EQ(play_silent(&sp_a), 1u);
}

/* Stop lets every sequenced note go: the unit goes silent. */
TT_TEST(seq_play_stop_goes_silent)
{
    static const uint8_t chord[4] = {48, 55, 60, 64};
    uint32_t k;
    seq *s;
    (void)play_init(&sp_a);
    s = play_seq(&sp_a);
    seq_set_step(s, 0, chord, 4, 100);
    seq_toggle_tie(s, 1);
    seq_play(s);
    for (k = 0; k < 10u; k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK(sp_a.sounding > 0u);
    seq_stop(s);
    for (k = 0; k < 2000u && !play_silent(&sp_a); k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK_EQ(play_silent(&sp_a), 1u);
}

/* A patch load mid-pattern lets the notes go (as a load does), and the pattern plays on: the next step
 * sounds, and a stop still ends silent. */
TT_TEST(seq_play_survives_a_patch_load)
{
    uint8_t note = 60;
    uint32_t k, steps;
    seq *s;
    (void)play_init(&sp_a);
    s = play_seq(&sp_a);
    seq_set_tempo(s, 3000);
    seq_set_gate(s, 100);
    seq_set_step(s, 0, &note, 1, 100);
    seq_set_length(s, 1);
    seq_play(s);
    for (k = 0; k < 4u; k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    play_load_patch(&sp_a, 3);
    steps = seq_steps(s);
    for (k = 0; k < 30u; k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK(seq_steps(s) > steps);
    TT_CHECK(sp_a.sounding > 0u);
    seq_stop(s);
    for (k = 0; k < 2000u && !play_silent(&sp_a); k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK_EQ(play_silent(&sp_a), 1u);
}

/* A key held on the note a step plays: the step's gate lets that note go too (the engine has one voice a
 * note, as with two MIDI sources on one note), so it dies away though the key is still down. Pinned as
 * the behavior today. */
TT_TEST(seq_play_same_note_as_a_key)
{
    uint8_t note = 60;
    uint32_t k;
    seq *s;
    (void)play_init(&sp_a);
    s = play_seq(&sp_a);
    seq_set_step(s, 0, &note, 1, 100);
    seq_set_length(s, 1);
    seq_set_tempo(s, SEQ_TEMPO_MIN);            /* one step: 2 s, its gate at 1 s */
    play_note(&sp_a, 60, 100);                  /* the key, held */
    seq_play(s);
    for (k = 0; k < 40u; k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK_EQ(sp_a.sounding, 1u);             /* one voice for the key and the step */
    seq_stop(s);
    for (k = 0; k < 4000u && sp_a.sounding; k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK_EQ(sp_a.sounding, 0u);             /* the stop let the key's note go too */
}

/* #37: a step's velocity reaches the engine: a loud step and a soft one, the soft one quieter. */
TT_TEST(seq_play_step_velocity_sets_the_level)
{
    uint8_t note = 57;
    uint32_t k, i;
    int32_t peak[2] = {0, 0};
    seq *s;
    (void)play_init(&sp_a);
    play_set_param(&sp_a, FM_PARAM_AMP_VELOCITY, 100);
    play_set_param(&sp_a, FM_PARAM_AMP_RELEASE_MS, 5);   /* (no tail into the next step) */
    s = play_seq(&sp_a);
    seq_set_tempo(s, 600);                      /* a step 11025 frames: 43 blocks */
    seq_set_gate(s, 50);
    seq_set_length(s, 2);
    seq_set_step(s, 0, &note, 1, 127);
    seq_set_step(s, 1, &note, 1, 40);
    seq_play(s);
    for (k = 0; k < 86u; k++) {
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
        if (k % 43u > 8u && k % 43u < 20u)      /* each step's held part */
            for (i = 0; i < 2u * PLAY_BLOCK; i++) {
                int32_t v = sp_out_a[i] < 0 ? -sp_out_a[i] : sp_out_a[i];
                peak[k / 43u] = v > peak[k / 43u] ? v : peak[k / 43u];
            }
    }
    TT_CHECK(peak[0] > 0);
    TT_CHECK(peak[1] < peak[0] / 2);
}

static void run_seq_play_tests(void)
{
    TT_RUN(seq_play_note_lands_on_its_frame);
    TT_RUN(seq_play_running_is_not_silent);
    TT_RUN(seq_play_stop_goes_silent);
    TT_RUN(seq_play_survives_a_patch_load);
    TT_RUN(seq_play_same_note_as_a_key);
    TT_RUN(seq_play_step_velocity_sets_the_level);
}
