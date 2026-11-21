/* Pattern identity (#324): the running pattern, the queued pattern, the stored pattern and the takeover.
 * "The window" is a takeover the interrupt has made that no main-loop call has seen yet: rendered to the
 * pass end with no draw since. Uses test_play_patterns.c's RAM flash and test_seq_play.c's helpers. */

/* two steps of `note`, at 300 BPM (a pass is a few blocks) */
static void pn_pat(seq_pattern *p, uint8_t note)
{
    seq_pattern_empty(p);
    p->length = 2;
    p->tempo = 3000;
    p->step[0].kind = p->step[1].kind = SEQ_NOTES_ON;
    p->step[0].n = p->step[1].n = 1;
    p->step[0].note[0] = p->step[1].note[0] = note;
}

/* A (slot 0, notes 60) and B (slot 1, notes 64) and C (slot 2, notes 67), two steps each at 300 BPM;
 * A loaded, unedited, and playing */
static void pn_write(uint32_t slot, uint8_t note, const char *name)
{
    static seq_pattern p;
    pn_pat(&p, note);
    TT_CHECK_EQ(pat_slot_write(&pp_lib, &store_fake_io, slot, &p, name), 0);
}

static void pn_setup(void)
{
    store_flash_blank();
    pat_lib_load(&pp_lib, &store_fake_io);
    pn_write(0, 60, "A");
    pn_write(1, 64, "B");
    pn_write(2, 67, "C");
    pp_setup();
    TT_CHECK(!pattern_edited(play_patterns(&sp_a)));
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
    TT_CHECK_EQ(pattern_running(play_patterns(&sp_a), n), running);
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

#define PN (play_patterns(&sp_a))

/* renders a few passes (at the slots' default tempo too), the draw's pattern_takeover after each block */
static void pn_passes(void)
{
    uint32_t k;
    for (k = 0; k < 400u; k++) {
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
        (void)pattern_takeover(PN);
    }
}

/* T1 (pin): the queued pattern takes over at the pass end: running B, nothing queued, not edited */
TT_TEST(pattern_takeover_makes_the_queued_one_running)
{
    pn_setup();
    pattern_queue(PN, 1);
    pn_is(0, "A", 1);
    pn_passes();
    pn_is(1, "B", -1);
    TT_CHECK(!pattern_edited(PN));
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 0)->note[0], 64u);
}

/* T6: B's slot saved anew before the takeover: the takeover plays what the slot holds now, by its name */
TT_TEST(pattern_save_into_the_queued_slot_changes_what_takes_over)
{
    static seq_pattern d;
    pn_setup();
    pattern_queue(PN, 1);
    pn_pat(&d, 74);
    TT_CHECK_EQ(pattern_save(PN, 1, &d, "D", pattern_snapshot(PN, &(seq_pattern){0})), 0);
    pn_is(0, "A", 1);
    {
        char n[STORE_NAME_LEN + 1u];
        (void)pattern_queued(PN, n);
        TT_CHECK(strcmp(n, "D") == 0);
    }
    pn_passes();
    pn_is(1, "D", -1);
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 0)->note[0], 74u);
    TT_CHECK(!pattern_edited(PN));
}

/* T8 (pin): A edited, B queued, takeover: A's edits are gone, B not edited */
TT_TEST(pattern_takeover_drops_the_old_edits)
{
    uint8_t g = 79;
    pn_setup();
    seq_set_step(play_seq(&sp_a), 1, &g, 1, 100);
    TT_CHECK(pattern_edited(PN));
    pattern_queue(PN, 1);
    pn_passes();
    TT_CHECK(!pattern_edited(PN));
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 1)->note[0], 64u);
}

/* T9: a save of A waits; B takes over meanwhile; then it's written: A's slot holds the snapshot, and B
 * stays the running pattern, unedited */
TT_TEST(pattern_waiting_save_after_a_takeover_keeps_the_running_pattern)
{
    static seq_pattern snap, out;
    char n[STORE_NAME_LEN + 1u];
    uint8_t g = 79;
    uint32_t t;
    pn_setup();
    seq_set_step(play_seq(&sp_a), 1, &g, 1, 100);
    t = pattern_snapshot(PN, &snap);
    pattern_queue(PN, 1);
    pn_passes();
    TT_CHECK_EQ(pattern_save(PN, 0, &snap, "A2", t), 0);
    TT_CHECK_EQ(pat_slot_read(&pp_lib, 0, &out, n), 0);
    TT_CHECK_EQ(out.step[1].note[0], 79u);
    pn_is(1, "B", -1);
    TT_CHECK(!pattern_edited(PN));
}

/* T10: as T9, the save's destination B's slot: B holds A's snapshot, B runs, and shows edited */
TT_TEST(pattern_waiting_save_into_the_new_running_slot_shows_it_edited)
{
    static seq_pattern snap;
    uint32_t t;
    pn_setup();
    t = pattern_snapshot(PN, &snap);
    pattern_queue(PN, 1);
    pn_passes();
    TT_CHECK_EQ(pattern_save(PN, 1, &snap, "AA", t), 0);
    pn_is(1, "AA", -1);
    TT_CHECK(pattern_edited(PN));
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 0)->note[0], 64u);   /* B plays on */
}

/* T11: stopping with B queued loads it at once; in the window, it's the one taken that runs */
TT_TEST(pattern_stop_with_one_queued_loads_it)
{
    pn_setup();
    pattern_queue(PN, 1);
    seq_stop(play_seq(&sp_a));
    pn_is(1, "B", -1);
    TT_CHECK(!pattern_edited(PN));
    pn_setup();
    pattern_queue(PN, 1);
    pn_take();
    seq_stop(play_seq(&sp_a));
    pn_is(1, "B", -1);
    TT_CHECK(!pattern_edited(PN));
}

/* T12: a takeover is recorded once: pattern_takeover says so once, whichever call saw it first */
TT_TEST(pattern_takeover_is_recorded_once)
{
    pn_setup();
    pattern_queue(PN, 1);
    pn_take();
    TT_CHECK(!pattern_edited(PN));              /* (this call sees it first) */
    TT_CHECK_EQ(pattern_takeover(PN), 1u);
    TT_CHECK_EQ(pattern_takeover(PN), 0u);
    pn_passes();
    pn_is(1, "B", -1);
}

/* T13: queue, cancel and queue again in the window, no draw: the last ask holds */
TT_TEST(pattern_the_last_ask_in_the_window_holds)
{
    pn_setup();
    pattern_queue(PN, 1);
    pn_take();                                  /* B runs */
    pattern_queue(PN, 2);                       /* C next */
    pattern_queue(PN, 1);                       /* back to B: none */
    pattern_queue(PN, 0);                       /* A next */
    pn_is(1, "B", 0);
    pn_passes();
    pn_is(0, "A", -1);
}

/* T14 (pin): the running pattern's slot deleted: it plays on, edited against the empty slot */
TT_TEST(pattern_delete_of_the_running_slot_plays_on_edited)
{
    pn_setup();
    TT_CHECK_EQ(pattern_delete(PN, 0), 0);
    pn_is(0, "A", -1);
    TT_CHECK(pattern_edited(PN));
    pn_passes();
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 0)->note[0], 60u);
}

/* T15: a save in the window (no draw since the takeover) goes to the running slot, B's */
TT_TEST(pattern_save_in_the_window_targets_the_running_slot)
{
    static seq_pattern snap, out;
    char n[STORE_NAME_LEN + 1u];
    uint32_t t;
    pn_setup();
    pattern_queue(PN, 1);
    pn_take();
    t = pattern_snapshot(PN, &snap);
    TT_CHECK_EQ(pattern_save(PN, pattern_running(PN, 0), &snap, "B", t), 0);
    TT_CHECK_EQ(pat_slot_read(&pp_lib, 1, &out, n), 0);
    TT_CHECK_EQ(out.step[0].note[0], 64u);      /* B's, into B's slot */
    TT_CHECK_EQ(pat_slot_read(&pp_lib, 0, &out, n), 0);
    TT_CHECK_EQ(out.step[0].note[0], 60u);      /* A's slot as it was */
    pn_is(1, "B", -1);
    TT_CHECK(!pattern_edited(PN));
}

/* T16: each call, made first after a takeover (the window), answers for B, the running pattern: what it
 * returns or does is right whichever call came first (A was edited, so a stale answer shows), and the
 * draw hears of the takeover once */
static uint32_t pn_c_running(void) { return pattern_running(PN, 0) == 1u; }
static uint32_t pn_c_queued(void) { return pattern_queued(PN, 0) == -1; }
static uint32_t pn_c_edited(void) { return pattern_edited(PN) == 0u; }
static uint32_t pn_c_takeover(void) { return pattern_takeover(PN) == 1u; }
static uint32_t pn_c_snapshot(void)
{
    static seq_pattern x;
    (void)pattern_snapshot(PN, &x);
    return x.step[0].note[0] == 64u;
}
static uint32_t pn_c_queue_a(void) { pattern_queue(PN, 0); return pattern_queued(PN, 0) == 0; }
static uint32_t pn_c_delete_b(void) { return pattern_delete(PN, 1) == 0 && pattern_edited(PN); }
static uint32_t pn_c_step_edit(void)
{
    uint8_t g = 77;
    seq_set_step(play_seq(&sp_a), 1, &g, 1, 100);
    return seq_get_step(play_seq(&sp_a), 0)->note[0] == 64u;
}
TT_TEST(pattern_every_call_takes_in_a_takeover_first)
{
    static uint32_t (*const calls[])(void) = {
        pn_c_running, pn_c_queued, pn_c_edited, pn_c_takeover, pn_c_snapshot, pn_c_queue_a, pn_c_delete_b,
        pn_c_step_edit,
    };
    uint8_t g = 79;
    uint32_t i, bad = 0;
    for (i = 0; i < sizeof calls / sizeof calls[0]; i++) {
        uint32_t right, told;
        pn_setup();
        seq_set_step(play_seq(&sp_a), 1, &g, 1, 100);   /* A edited */
        pattern_queue(PN, 1);
        pn_take();
        right = calls[i]();
        told = calls[i] == pn_c_takeover ? 1u : pattern_takeover(PN);
        if (!right || !told || pattern_takeover(PN) || pattern_running(PN, 0) != 1u) {
            printf("    call %u: right %u, told %u\n", (unsigned)i, (unsigned)right, (unsigned)told);
            bad++;
        }
    }
    TT_CHECK_EQ(bad, 0u);
}

/* The handoff's gaps (handoff_test.h): the interrupt runs through several pass ends at gap n of `where`
 * (each of the main loop's steps: close, sync, write, open). A pass end in a gap takes nothing (the ask
 * is closed), so what runs and what is queued come out as if it hadn't been there, and the pattern that
 * plays is always the one named running. */
static const char *pn_gap_where;
static uint32_t pn_gap_n, pn_gap_hit;
static void pn_gap(const char *where, uint32_t n)
{
    uint32_t k;
    if (pn_gap_hit || n != pn_gap_n || strcmp(where, pn_gap_where) != 0)
        return;                                 /* (once: one takeover) */
    pn_gap_hit++;
    for (k = 0; k < 400u; k++)
        sp_render(&sp_a, sp_out_a, PLAY_BLOCK);
}

/* A runs, B queued; op at gap n; then what runs, by name and by its notes, after some passes */
static uint32_t pn_gap_case(const char *where, uint32_t n, uint32_t op)
{
    static seq_pattern d;
    char name[STORE_NAME_LEN + 1u];
    uint32_t ok = 1, want, note;
    const char *want_name;
    pn_setup();
    pattern_queue(PN, 1);
    pn_gap_where = where;
    pn_gap_n = n;
    pn_gap_hit = 0;
    handoff_gap = pn_gap;
    switch (op) {
    case 0: pattern_queue(PN, 2); want = 2; want_name = "C"; note = 67; break;     /* on to C */
    case 1: pattern_queue(PN, 0); want = 0; want_name = "A"; note = 60; break;     /* back: cancel */
    case 2: pn_pat(&d, 74);                                                       /* B's slot saved anew */
        ok &= pattern_save(PN, 1, &d, "D", pattern_snapshot(PN, &(seq_pattern){0})) == 0;
        want = 1; want_name = "D"; note = 74; break;
    default: ok &= pattern_delete(PN, 1) == 0; want = 0; want_name = "A"; note = 60; break;   /* B deleted */
    }
    handoff_gap = 0;
    ok &= pn_gap_hit == 1u;
    ok &= pattern_running(PN, 0) == 0u;                 /* closed: nothing took over in the gap */
    pn_passes();
    ok &= pattern_running(PN, name) == want && strcmp(name, want_name) == 0 && pattern_queued(PN, 0) == -1;
    ok &= seq_get_step(play_seq(&sp_a), 0)->note[0] == note && !pattern_edited(PN);
    if (!ok)
        printf("    %s gap %u, op %u: running %u\n", where, (unsigned)n, (unsigned)op,
               (unsigned)pattern_running(PN, 0));
    return ok;
}

TT_TEST(pattern_a_pass_end_in_any_gap_of_the_handoff_takes_nothing)
{
    uint32_t n, op, bad = 0;
    for (op = 0; op < 4u; op++)
        for (n = 1; n <= 3u; n++)
            bad += !pn_gap_case("ask", n, op);
    TT_CHECK_EQ(bad, 0u);
}

/* A takeover in the middle of a step edit (after it synced): the edit, of A, is dropped with A; B, the
 * pattern playing, is what the main loop sees and edits next */
TT_TEST(pattern_a_takeover_inside_a_step_edit_drops_the_edit)
{
    uint8_t g = 77;
    pn_setup();
    pattern_queue(PN, 1);
    pn_gap_where = "edit";
    pn_gap_n = 1;
    pn_gap_hit = 0;
    handoff_gap = pn_gap;
    seq_set_step(play_seq(&sp_a), 1, &g, 1, 100);     /* B takes over inside it */
    handoff_gap = 0;
    TT_CHECK_EQ(pn_gap_hit, 1u);
    TT_CHECK_EQ(pattern_running(PN, 0), 1u);
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 0)->note[0], 64u);
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 1)->note[0], 64u);   /* A's edit gone with A */
    TT_CHECK(!pattern_edited(PN));
    pn_passes();
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 1)->note[0], 64u);
}

/* Gap 0: a pass end after a call decided and before the ask closes, so B takes over just then. Each call
 * ends as if it had come right after the takeover (T2, T3, T7, T10's rules) */
TT_TEST(pattern_a_takeover_just_before_the_close_is_taken_in)
{
    static seq_pattern d;
    char name[STORE_NAME_LEN + 1u];
    uint32_t op, bad = 0;
    for (op = 0; op < 5u; op++) {
        uint32_t ok = 1;
        pn_setup();
        pattern_queue(PN, 1);
        pn_gap_where = "ask";
        pn_gap_n = 0;
        pn_gap_hit = 0;
        handoff_gap = pn_gap;
        switch (op) {
        case 0: pattern_queue(PN, 2); break;                                  /* on to C */
        case 1: pattern_queue(PN, 0); break;                                  /* back to A */
        case 2: pn_pat(&d, 74); (void)pattern_save(PN, 1, &d, "D", 0); break;  /* B's slot saved */
        case 3: (void)pattern_delete(PN, 1); break;                           /* B's slot deleted */
        default: pattern_queue(PN, 1); break;                                 /* B again */
        }
        handoff_gap = 0;
        ok &= pn_gap_hit >= 1u;
        switch (op) {
        case 0: ok &= pattern_running(PN, 0) == 1u && pattern_queued(PN, 0) == 2; break;
        case 1: ok &= pattern_running(PN, 0) == 1u && pattern_queued(PN, 0) == 0; break;
        case 2: ok &= pattern_running(PN, name) == 1u && strcmp(name, "D") == 0 && pattern_edited(PN) &&
                      pattern_queued(PN, 0) == -1; break;
        case 3: ok &= pattern_running(PN, 0) == 1u && pattern_edited(PN) && pattern_queued(PN, 0) == -1; break;
        default: ok &= pattern_running(PN, 0) == 1u && !pattern_edited(PN) && pattern_queued(PN, 0) == -1;
        }
        if (!ok) {
            printf("    op %u: running %u queued %d hit %u\n", (unsigned)op, (unsigned)pattern_running(PN, 0),
                   (int)pattern_queued(PN, 0), (unsigned)pn_gap_hit);
            bad++;
        }
    }
    TT_CHECK_EQ(bad, 0u);
}

/* A pass end inside a stop, between its ask and the queue's close: B, queued, still loads, once */
TT_TEST(pattern_a_takeover_inside_a_stop_loads_the_queued_one)
{
    pn_setup();
    pattern_queue(PN, 1);
    pn_gap_where = "stop";
    pn_gap_n = 1;
    pn_gap_hit = 0;
    handoff_gap = pn_gap;
    seq_stop(play_seq(&sp_a));
    handoff_gap = 0;
    TT_CHECK_EQ(pn_gap_hit, 1u);
    pn_is(1, "B", -1);
    TT_CHECK(!pattern_edited(PN));
    TT_CHECK_EQ(seq_get_step(play_seq(&sp_a), 0)->note[0], 64u);
    TT_CHECK_EQ(pattern_takeover(PN), 1u);
    TT_CHECK_EQ(pattern_takeover(PN), 0u);
}

static void run_pattern_now_tests(void)
{
    TT_RUN(pattern_a_takeover_just_before_the_close_is_taken_in);
    TT_RUN(pattern_a_takeover_inside_a_stop_loads_the_queued_one);
    TT_RUN(pattern_a_pass_end_in_any_gap_of_the_handoff_takes_nothing);
    TT_RUN(pattern_a_takeover_inside_a_step_edit_drops_the_edit);
    TT_RUN(pattern_every_call_takes_in_a_takeover_first);
    TT_RUN(pattern_takeover_makes_the_queued_one_running);
    TT_RUN(pattern_save_into_the_queued_slot_changes_what_takes_over);
    TT_RUN(pattern_takeover_drops_the_old_edits);
    TT_RUN(pattern_waiting_save_after_a_takeover_keeps_the_running_pattern);
    TT_RUN(pattern_waiting_save_into_the_new_running_slot_shows_it_edited);
    TT_RUN(pattern_stop_with_one_queued_loads_it);
    TT_RUN(pattern_takeover_is_recorded_once);
    TT_RUN(pattern_the_last_ask_in_the_window_holds);
    TT_RUN(pattern_delete_of_the_running_slot_plays_on_edited);
    TT_RUN(pattern_save_in_the_window_targets_the_running_slot);
    TT_RUN(pattern_back_to_the_old_slot_in_the_window_queues_it);
    TT_RUN(pattern_on_to_another_slot_in_the_window_keeps_the_takeover);
    TT_RUN(pattern_step_edit_in_the_window_edits_the_running_pattern);
}
