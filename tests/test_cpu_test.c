/* CPU test (firmware/cpu_test.c) behavior: what the FM-1 module drives, run on the desktop. */
#include "../firmware/cpu_test.h"

static cpu_test ct_a, ct_b;
static int32_t ct_out[2 * CT_BLOCK];

TT_TEST(cpu_test_render_adds_engine_audio_into_host_buffer)
{
    static int32_t base[2 * CT_BLOCK], fresh[2 * CT_BLOCK];
    uint32_t i, sound = 0;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    TT_CHECK_EQ(cpu_test_init(&ct_b), 0);
    for (i = 0; i < 2 * CT_BLOCK; i++) { base[i] = 1000; fresh[i] = 0; }
    cpu_test_render(&ct_a, base, CT_BLOCK);
    cpu_test_render(&ct_b, fresh, CT_BLOCK);
    for (i = 0; i < 2 * CT_BLOCK; i++) {
        TT_CHECK_EQ(base[i], fresh[i] + 1000);
        sound |= fresh[i] != 0;
    }
    TT_CHECK(sound);
}

TT_TEST(cpu_test_voice_request_clamps_and_applies_at_next_block)
{
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), 1);
    cpu_test_request_voices(&ct_a, 12);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), 1);            /* the main loop asks; the ISR applies */
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_BURST);      /* the steps run 1..8, then U */
    cpu_test_request_voices(&ct_a, (int32_t)CT_BURST - 1);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), FM_MAX_VOICES);
    cpu_test_request_voices(&ct_a, 0);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), 1);
}

TT_TEST(cpu_test_keeps_worst_block_time_per_voice_count)
{
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, 1), 0);          /* nothing measured yet */
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 4);                 /* 1 voice: 25 % */
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 10);                /* a lighter block keeps the worst */
    cpu_test_request_voices(&ct_a, 3);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);                   /* starts 3 voices: not timed */
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 2);                 /* 3 voices: 50 % */
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, 1), 250);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, 3), 500);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, 2), 0);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, CT_BURST + 1), 0); /* out of range: nothing */
}

TT_TEST(cpu_test_sweep_steps_up_one_voice_per_hold_and_ends_at_the_burst)
{
    uint32_t i;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_sweep(&ct_a, 2);                                   /* 2 blocks per voice count */
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), 1);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), 1);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), 2);
    for (i = 0; i < 40; i++)
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_BURST);              /* after 8 voices: the unison burst */
}

static uint32_t take_all(uint32_t *counts, uint32_t *pms)
{
    uint32_t k = 0, v, pm;
    while (k < 2u * FM_MAX_VOICES && cpu_test_take_completed(&ct_a, &v, &pm)) {
        counts[k] = v;
        pms[k] = pm;
        k++;
    }
    return k;
}

static void sweep_to_burst(void)
{
    uint32_t i;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_sweep(&ct_a, 2);
    for (i = 0; i < 2u * FM_MAX_VOICES + 1u; i++)                /* 1..8 held 2 blocks each, then U */
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_BURST);
}

TT_TEST(cpu_test_burst_plays_all_voices_in_unison)
{
    sweep_to_burst();
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_VOICE_MODE), FM_VOICE_MODE_UNISON);
    TT_CHECK_EQ(fm_engine_active_voices(&ct_a.engine), FM_MAX_VOICES);
}

/* U picked by hand (no sweep, no hold) keeps bursting, a new detune every block. */
TT_TEST(cpu_test_burst_changes_detune_every_block)
{
    int32_t a, b;
    uint32_t i;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_request_voices(&ct_a, (int32_t)CT_BURST);
    for (i = 0; i < 10; i++)
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_BURST);
    a = fm_engine_get_param(&ct_a.engine, FM_PARAM_UNISON_DETUNE_CENTS);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    b = fm_engine_get_param(&ct_a.engine, FM_PARAM_UNISON_DETUNE_CENTS);
    TT_CHECK(a != b);
}

TT_TEST(cpu_test_burst_completes_as_its_own_count)
{
    uint32_t counts[2 * FM_MAX_VOICES], pms[2 * FM_MAX_VOICES], k, v, pm;
    sweep_to_burst();
    while (cpu_test_take_completed(&ct_a, &v, &pm))
        ;
    cpu_test_record(&ct_a, CT_BLOCK_TICKS);             /* the block that started U: setup, not timed */
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 4);         /* 250 per mille */
    k = take_all(counts, pms);
    TT_CHECK_EQ(k, 1);
    TT_CHECK_EQ(counts[0], CT_BURST);
    TT_CHECK_EQ(pms[0], 250);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, CT_BURST), 250);
}

TT_TEST(cpu_test_chord_gives_each_voice_its_own_note)
{
    uint8_t notes[FM_MAX_VOICES];
    uint32_t k, i, j;
    for (k = 1; k <= FM_MAX_VOICES; k++) {
        TT_CHECK_EQ(cpu_test_chord(k, notes), k);
        for (i = 0; i < k; i++) {
            TT_CHECK(notes[i] >= 24 && notes[i] <= 108);        /* C1..C8: a playable range */
            for (j = 0; j < i; j++)
                TT_CHECK(notes[i] != notes[j]);
        }
    }
}

TT_TEST(cpu_test_request_after_sweep_still_applies)
{
    uint32_t i;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_request_voices(&ct_a, 1);
    cpu_test_sweep(&ct_a, 1);
    for (i = 0; i < 20; i++)
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_BURST);
    cpu_test_sweep(&ct_a, 0);
    cpu_test_request_voices(&ct_a, 1);                          /* the same count as the last request */
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), 1);
}

TT_TEST(cpu_test_block_that_starts_a_count_is_not_timed)
{
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_request_voices(&ct_a, 1);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);           /* starts 1 voice: engine init + note on */
    cpu_test_record(&ct_a, CT_BLOCK_TICKS);             /* setup cost: not the render's */
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, 1), 0);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 4);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, 1), 250);
    cpu_test_request_voices(&ct_a, 2);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);           /* a new count: its first block again */
    cpu_test_record(&ct_a, CT_BLOCK_TICKS);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, 2), 0);
}

TT_TEST(cpu_test_starting_a_count_again_forgets_its_old_worst)
{
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 2);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, 1), 500);
    cpu_test_request_voices(&ct_a, 2);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_request_voices(&ct_a, 1);                  /* back to 1 (a new sweep starts here) */
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, 1), 0);
}

TT_TEST(cpu_test_each_held_count_completes_once)
{
    uint32_t counts[2 * FM_MAX_VOICES], pms[2 * FM_MAX_VOICES], i, k;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_request_voices(&ct_a, 1);
    cpu_test_sweep(&ct_a, 2);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(take_all(counts, pms), 0);              /* not held long enough yet */
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 8);             /* 12.5 %: 125 per mille */
    TT_CHECK_EQ(take_all(counts, pms), 1);
    TT_CHECK_EQ(counts[0], 1);
    TT_CHECK_EQ(pms[0], 125);
    for (i = 0; i < 40; i++) {                          /* the rest of the sweep, then a long hold at max */
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        cpu_test_record(&ct_a, 0);
    }
    k = take_all(counts, pms);
    TT_CHECK_EQ(k, CT_BURST - 1);                       /* 2..8 and the burst, each once */
    for (i = 0; i < k; i++)
        TT_CHECK_EQ(counts[i], i + 2);
}

TT_TEST(cpu_test_counts_picked_by_hand_never_complete)
{
    uint32_t counts[2 * FM_MAX_VOICES], pms[2 * FM_MAX_VOICES], i;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    for (i = 1; i <= 3; i++) {
        cpu_test_request_voices(&ct_a, (int32_t)i);
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    }
    TT_CHECK_EQ(take_all(counts, pms), 0);
}

/* After its hold the burst stops: no more events, and row U holds what the log line said. */
TT_TEST(cpu_test_burst_ends_after_its_hold)
{
    uint32_t v, pm, i;
    int32_t a, b;
    sweep_to_burst();                                   /* hold 2: U started (not timed) */
    cpu_test_record(&ct_a, CT_BLOCK_TICKS);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 4);         /* U's hold complete: 250 per mille */
    while (cpu_test_take_completed(&ct_a, &v, &pm))
        ;
    for (i = 0; i < 4; i++) {
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        cpu_test_record(&ct_a, CT_BLOCK_TICKS / 2);     /* heavier, but after the hold */
    }
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, CT_BURST), 250);
    a = fm_engine_get_param(&ct_a.engine, FM_PARAM_UNISON_DETUNE_CENTS);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    b = fm_engine_get_param(&ct_a.engine, FM_PARAM_UNISON_DETUNE_CENTS);
    TT_CHECK_EQ(a, b);
    TT_CHECK_EQ(cpu_test_take_completed(&ct_a, &v, &pm), 0);
}

static void run_cpu_test_tests(void)
{
    TT_RUN(cpu_test_render_adds_engine_audio_into_host_buffer);
    TT_RUN(cpu_test_voice_request_clamps_and_applies_at_next_block);
    TT_RUN(cpu_test_keeps_worst_block_time_per_voice_count);
    TT_RUN(cpu_test_sweep_steps_up_one_voice_per_hold_and_ends_at_the_burst);
    TT_RUN(cpu_test_chord_gives_each_voice_its_own_note);
    TT_RUN(cpu_test_request_after_sweep_still_applies);
    TT_RUN(cpu_test_block_that_starts_a_count_is_not_timed);
    TT_RUN(cpu_test_starting_a_count_again_forgets_its_old_worst);
    TT_RUN(cpu_test_each_held_count_completes_once);
    TT_RUN(cpu_test_counts_picked_by_hand_never_complete);
    TT_RUN(cpu_test_burst_plays_all_voices_in_unison);
    TT_RUN(cpu_test_burst_changes_detune_every_block);
    TT_RUN(cpu_test_burst_completes_as_its_own_count);
    TT_RUN(cpu_test_burst_ends_after_its_hold);
}
