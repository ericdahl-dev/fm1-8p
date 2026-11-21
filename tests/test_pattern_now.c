/* Pattern identity (#324): the running pattern, the queued pattern, the stored pattern and the takeover.
 * "The window" is a takeover the interrupt has made that no main-loop call has seen yet: rendered to the
 * pass end with no draw since. Uses test_play_patterns.c's RAM flash and test_seq_play.c's helpers. */

/* A (slot 0, notes 60) and B (slot 1, notes 64) and C (slot 2, notes 67), two steps each; A loaded,
 * playing at a step a block or so (a pass ends every few blocks) */
static void pn_setup(void)
{
    store_flash_blank();
    pat_lib_load(&pp_lib, &store_fake_io);
    pp_write(0, 60, 2, "A");
    pp_write(1, 64, 2, "B");
    pp_write(2, 67, 2, "C");
    pp_setup();
    seq_set_tempo(play_seq(&sp_a), 3000);
    seq_play(play_seq(&sp_a));
    sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
}

/* renders until the queued pattern has taken over, with no main-loop call: the window */
static void pn_take(void)
{
    uint32_t k;
    for (k = 0; k < 1000u && seq_next_queued(play_seq(&sp_a)); k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    TT_CHECK(!seq_next_queued(play_seq(&sp_a)));
}

/* T5: a step edit in the window edits B, the pattern playing; A never comes back */
TT_TEST(pattern_step_edit_in_the_window_edits_the_running_pattern)
{
    uint8_t g = 71;
    seq *s;
    pn_setup();
    s = play_seq(&sp_a);
    pattern_queue(play_patterns(&sp_a), 1);
    pn_take();
    seq_set_step(s, 1, &g, 1, 100);
    TT_CHECK_EQ(seq_get_step(s, 0)->note[0], 64u);   /* B's, not A's */
    TT_CHECK_EQ(seq_get_step(s, 1)->note[0], 71u);
    sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
    (void)pattern_takeover(play_patterns(&sp_a));
    TT_CHECK_EQ(seq_get_step(s, 0)->note[0], 64u);
    TT_CHECK_EQ(seq_get_step(s, 1)->note[0], 71u);   /* the edit kept */
}

/* the running pattern's slot and name, and the queued one's slot (-1: none), are these */
static void pn_is(uint32_t running, const char *name, int32_t queued)
{
    char n[STORE_NAME_LEN + 1u];
    TT_CHECK_EQ(pattern_running(play_patterns(&sp_a), 0), running);
    (void)pattern_running(play_patterns(&sp_a), n);
    TT_CHECK(strcmp(n, name) == 0);
    TT_CHECK_EQ(pattern_queued(play_patterns(&sp_a), 0), queued);
}

/* T2: in the window, PRESETS back to A's slot queues A again; B, playing, is the running pattern */
TT_TEST(pattern_back_to_the_old_slot_in_the_window_queues_it)
{
    pn_setup();
    pattern_queue(play_patterns(&sp_a), 1);
    pn_take();
    pattern_queue(play_patterns(&sp_a), 0);
    pn_is(1, "B", 0);
    TT_CHECK(seq_next_queued(play_seq(&sp_a)));
}

/* T3: in the window, PRESETS on to C queues C; B is the running pattern */
TT_TEST(pattern_on_to_another_slot_in_the_window_keeps_the_takeover)
{
    pn_setup();
    pattern_queue(play_patterns(&sp_a), 1);
    pn_take();
    pattern_queue(play_patterns(&sp_a), 2);
    pn_is(1, "B", 2);
}

static void run_pattern_now_tests(void)
{
    TT_RUN(pattern_back_to_the_old_slot_in_the_window_queues_it);
    TT_RUN(pattern_on_to_another_slot_in_the_window_keeps_the_takeover);
    TT_RUN(pattern_step_edit_in_the_window_edits_the_running_pattern);
}
