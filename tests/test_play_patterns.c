/* Pattern slots in play (firmware/play.c, #40): which pattern plays, loading, the queue while playing,
 * saving, and the first draw's read. Uses test_patch_store.c's RAM flash and test_seq_play.c's helpers. */

static pat_lib pp_lib;

/* a play with the user slots and the pattern slots on the RAM flash, read as at the first draw */
static void pp_setup(void)
{
    (void)play_init(&sp_a);
    play_attach_store(&sp_a, &store_lib_a, &store_fake_io);
    play_attach_patterns(&sp_a, &pp_lib);
    play_store_tick(&sp_a);
}

static void pp_write(uint32_t slot, uint8_t note, uint32_t len, const char *name)
{
    static seq_pattern p;
    uint32_t i;
    seq_pattern_empty(&p);
    p.length = (uint8_t)len;
    for (i = 0; i < len; i++) {
        p.step[i].kind = SEQ_NOTES_ON;
        p.step[i].n = 1;
        p.step[i].note[0] = note;
    }
    TT_CHECK_EQ(pat_slot_write(&pp_lib, &store_fake_io, slot, &p, name), 0);
}

/* Before anything is stored: pattern slot 0 (shown 11), no name, nothing queued, from play_init on. */
TT_TEST(play_pattern_starts_on_slot_0)
{
    char name[STORE_NAME_LEN + 1u];
    (void)play_init(&sp_a);
    TT_CHECK_EQ(play_pattern_prog(&sp_a), 0u);
    TT_CHECK_EQ(play_pattern_next(&sp_a), -1);
    play_pattern_name(&sp_a, name);
    TT_CHECK_EQ(name[0], '\0');
}

/* The first draw reads the pattern banks and loads slot 0's pattern, if it holds one. */
TT_TEST(play_pattern_first_draw_loads_slot_0)
{
    char name[STORE_NAME_LEN + 1u];
    store_flash_blank();
    pat_lib_load(&pp_lib, &store_fake_io);
    pp_write(0, 50, 3, "INTRO");
    pp_setup();
    TT_CHECK_EQ(seq_length(play_seq(&sp_a)), 3u);
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 0)->note[0], 50u);
    play_pattern_name(&sp_a, name);
    TT_CHECK(strcmp(name, "INTRO") == 0);
    TT_CHECK_EQ(seq_edited(play_seq(&sp_a)), 0u);
}

/* Saving writes the edit buffer to a pattern slot, never a patch slot (check 56); that slot is then the
 * pattern playing, not edited, and loading it gives it back. An empty slot loads as the empty pattern. */
TT_TEST(play_pattern_save_then_load)
{
    uint8_t note = 66;
    char name[STORE_NAME_LEN + 1u];
    seq *s;
    store_flash_blank();
    pp_setup();
    s = play_seq(&sp_a);
    seq_set_step(s, 0, &note, 1, 100);
    seq_set_length(s, 5);
    TT_CHECK_EQ(seq_edited(s), 1u);
    TT_CHECK_EQ(play_pattern_save(&sp_a, 12, "RIFF A"), 0);
    TT_CHECK_EQ(store_flash.len[1], -1);        /* no patch bank written */
    TT_CHECK_EQ(play_pattern_prog(&sp_a), 12u);
    play_pattern_name(&sp_a, name);
    TT_CHECK(strcmp(name, "RIFF A") == 0);
    TT_CHECK_EQ(seq_edited(s), 0u);
    play_load_pattern(&sp_a, 30);               /* empty */
    TT_CHECK_EQ(play_pattern_prog(&sp_a), 30u);
    TT_CHECK_EQ(seq_length(s), SEQ_STEPS);
    TT_CHECK_EQ(seq_get_step(s, 0)->kind, SEQ_REST);
    play_pattern_name(&sp_a, name);
    TT_CHECK_EQ(name[0], '\0');
    play_load_pattern(&sp_a, 12);
    TT_CHECK_EQ(seq_length(s), 5u);
    TT_CHECK_EQ(seq_get_step(s, 0)->note[0], 66u);
}

/* Playing, a load is queued (play_pattern_next) and takes over after the pass; the pattern number and
 * name follow once it has (checks 50, 51). */
TT_TEST(play_pattern_load_while_playing_queues)
{
    char name[STORE_NAME_LEN + 1u];
    uint32_t k;
    store_flash_blank();
    pat_lib_load(&pp_lib, &store_fake_io);
    pp_write(0, 60, 1, "ONE");
    pp_write(7, 72, 1, "TWO");
    pp_setup();
    seq_set_tempo(play_seq(&sp_a), 3000);       /* a pass: one step, 2205 frames */
    seq_play(play_seq(&sp_a));
    sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    play_load_pattern(&sp_a, 7);
    TT_CHECK_EQ(play_pattern_next(&sp_a), 7);
    TT_CHECK_EQ(play_pattern_prog(&sp_a), 0u);
    for (k = 0; k < 12u; k++) {
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
        play_pattern_tick(&sp_a);
    }
    TT_CHECK_EQ(play_pattern_prog(&sp_a), 7u);
    TT_CHECK_EQ(play_pattern_next(&sp_a), -1);
    play_pattern_name(&sp_a, name);
    TT_CHECK(strcmp(name, "TWO") == 0);
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 0)->note[0], 72u);
}

/* Turning back to the running pattern's number cancels the queued one (check 52); stopping with one
 * queued loads it at once (check 53). */
TT_TEST(play_pattern_cancel_and_stop)
{
    store_flash_blank();
    pat_lib_load(&pp_lib, &store_fake_io);
    pp_write(0, 60, 2, "ONE");
    pp_write(3, 72, 2, "TWO");
    pp_setup();
    seq_play(play_seq(&sp_a));
    sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    play_load_pattern(&sp_a, 3);
    play_load_pattern(&sp_a, 0);                /* back to the running one */
    TT_CHECK_EQ(play_pattern_next(&sp_a), -1);
    TT_CHECK_EQ(seq_next_queued(play_seq(&sp_a)), 0u);
    play_load_pattern(&sp_a, 3);
    seq_stop(play_seq(&sp_a));
    play_pattern_tick(&sp_a);
    TT_CHECK_EQ(play_pattern_prog(&sp_a), 3u);
    TT_CHECK_EQ(play_pattern_next(&sp_a), -1);
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 0)->note[0], 72u);
}

/* A patch change while playing leaves the running pattern, its step and its tempo (check 54, Q8). */
TT_TEST(play_pattern_survives_a_patch_change)
{
    uint32_t k, before;
    seq *s;
    store_flash_blank();
    pat_lib_load(&pp_lib, &store_fake_io);
    pp_write(0, 60, 4, "ONE");
    pp_setup();
    s = play_seq(&sp_a);
    seq_play(s);
    for (k = 0; k < 30u; k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    before = seq_steps(s);
    play_load_patch(&sp_a, 5);
    for (k = 0; k < 30u; k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK(seq_steps(s) > before);            /* it plays on */
    TT_CHECK_EQ(play_pattern_prog(&sp_a), 0u);
    TT_CHECK_EQ(seq_length(s), 4u);
    TT_CHECK_EQ(seq_tempo(s), SEQ_TEMPO_DEF);
}

static void run_play_patterns_tests(void)
{
    TT_RUN(play_pattern_starts_on_slot_0);
    TT_RUN(play_pattern_first_draw_loads_slot_0);
    TT_RUN(play_pattern_save_then_load);
    TT_RUN(play_pattern_load_while_playing_queues);
    TT_RUN(play_pattern_cancel_and_stop);
    TT_RUN(play_pattern_survives_a_patch_change);
}
