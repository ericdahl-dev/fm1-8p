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
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_FULL);       /* the steps run 1..8, then U, then F */
    cpu_test_request_voices(&ct_a, (int32_t)CT_BURST);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_BURST);
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
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, CT_LAST + 1), 0);  /* out of range: nothing */
}

TT_TEST(cpu_test_sweep_steps_up_one_voice_per_hold_then_the_burst_then_the_full_patch)
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
    for (i = 0; i < 2u * FM_MAX_VOICES - 3u; i++)              /* to the last block of 8 voices */
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), FM_MAX_VOICES);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_BURST);              /* after 8 voices: the unison burst */
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_FULL);               /* then the full patch */
    for (i = 0; i < 40; i++)
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_FULL);               /* the last step: it stays */
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
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_FULL);
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
    TT_CHECK_EQ(k, CT_FULL - 1);                        /* 2..8, the burst and the full patch, each once */
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

/* After its hold the sweep moves on to F, and row U holds what the log line said. */
TT_TEST(cpu_test_burst_ends_after_its_hold)
{
    uint32_t v, pm, i;
    sweep_to_burst();                                   /* hold 2: U started (not timed) */
    cpu_test_record(&ct_a, CT_BLOCK_TICKS);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 4);         /* U's hold complete: 250 per mille */
    while (cpu_test_take_completed(&ct_a, &v, &pm))
        ;
    for (i = 0; i < 4; i++) {
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        cpu_test_record(&ct_a, CT_BLOCK_TICKS / 2);     /* heavier, but F's */
    }
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_FULL);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, CT_BURST), 250);
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_VOICE_MODE), FM_VOICE_MODE_POLY);
}

/* F is the sweep's last step: after its hold the chord just rings, and row F holds what the log said. */
TT_TEST(cpu_test_full_patch_ends_the_sweep_after_its_hold)
{
    uint32_t v, pm, i;
    sweep_to_burst();
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);           /* U's second block */
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);           /* F started (not timed) */
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_FULL);
    cpu_test_record(&ct_a, CT_BLOCK_TICKS);
    while (cpu_test_take_completed(&ct_a, &v, &pm))
        ;
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    cpu_test_record(&ct_a, CT_BLOCK_TICKS / 8);         /* F's hold complete: 125 per mille */
    TT_CHECK_EQ(cpu_test_take_completed(&ct_a, &v, &pm), 1);
    TT_CHECK_EQ(v, CT_FULL);
    TT_CHECK_EQ(pm, 125);
    for (i = 0; i < 4; i++) {
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        cpu_test_record(&ct_a, CT_BLOCK_TICKS / 2);     /* heavier, but after the hold */
    }
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_FULL);
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, CT_FULL), 125);
    TT_CHECK_EQ(cpu_test_take_completed(&ct_a, &v, &pm), 0);
}

/* Step F, after U: the full patch, every modulation the engine has turned on, on a 6-voice chord: F asks
 * "do 6 voices fit", so a device that can't keep up shows an overrun rather than having the module stopped
 * (the desktop bench plays it on 8). */
TT_TEST(cpu_test_full_patch_step_turns_on_every_modulation)
{
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_request_voices(&ct_a, (int32_t)CT_FULL);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(cpu_test_voices(&ct_a), CT_FULL);
    TT_CHECK_EQ(fm_engine_active_voices(&ct_a.engine), 6);
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_VOICE_MODE), FM_VOICE_MODE_POLY);
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_OSC1_WAVE), FM_WAVE_PULSE);
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_OSC2_WAVE), FM_WAVE_SAW);
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_OSC2_LEVEL), 500);
    TT_CHECK(fm_engine_get_param(&ct_a.engine, FM_PARAM_CHORUS_MODE) != 0);
    TT_CHECK(fm_engine_get_param(&ct_a.engine, FM_PARAM_LFO_PITCH_CENTS) > 0);
    TT_CHECK(fm_engine_get_param(&ct_a.engine, FM_PARAM_LFO_PW_PCT) > 0);
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_LFO_DELAY_MS), 0);   /* modulating from the start */
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_AMP_VELOCITY), 100);
}

/* F strikes its notes at different velocities, so the amp velocity curve gives each a different peak. */
TT_TEST(cpu_test_full_patch_strikes_varied_velocities)
{
    uint32_t i, j, differ = 0, n;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_request_voices(&ct_a, (int32_t)CT_FULL);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    n = ct_a.engine.voice_count;
    for (i = 0; i < n; i++) {
        TT_CHECK(cpu_test_full_velocity(i) >= 1 && cpu_test_full_velocity(i) < 127);
        TT_CHECK(ct_a.engine.voices[i].amp_env.peak < (1 << 30));       /* below full: the curve applied */
        for (j = 0; j < i; j++)
            differ += ct_a.engine.voices[i].amp_env.peak != ct_a.engine.voices[j].amp_env.peak;
    }
    TT_CHECK_EQ(differ, n * (n - 1) / 2);                       /* every pair differs */
}

/* Steps 1..8 and U play the default patch: F's settings never leak into them, even after F ran. */
TT_TEST(cpu_test_default_steps_leave_the_patch_at_its_defaults)
{
    static const uint16_t MOD[] = {FM_PARAM_OSC1_WAVE, FM_PARAM_OSC2_WAVE, FM_PARAM_OSC2_LEVEL,
                                   FM_PARAM_CHORUS_MODE, FM_PARAM_LFO_PITCH_CENTS, FM_PARAM_LFO_PW_PCT,
                                   FM_PARAM_AMP_VELOCITY, FM_PARAM_FILTER_CUTOFF_CENTS,
                                   FM_PARAM_FILTER_RESONANCE, FM_PARAM_FILTER_ENV_AMOUNT_CENTS,
                                   FM_PARAM_HIGHPASS_STEP};
    uint32_t v, i;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_request_voices(&ct_a, (int32_t)CT_FULL);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    for (v = 1; v <= CT_BURST; v++) {
        cpu_test_request_voices(&ct_a, (int32_t)v);
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        for (i = 0; i < sizeof MOD / sizeof MOD[0]; i++)
            TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, MOD[i]), fm_param_info_get(MOD[i])->def);
    }
}

/* The bench's full-patch rows: counts 1..8 play F's patch and velocities on their own chord. */
TT_TEST(cpu_test_full_patch_can_play_at_any_voice_count)
{
    uint32_t v;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_use_full_patch(&ct_a, 1);
    for (v = 1; v <= FM_MAX_VOICES; v++) {
        cpu_test_request_voices(&ct_a, (int32_t)v);
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        TT_CHECK_EQ(fm_engine_active_voices(&ct_a.engine), v);
        TT_CHECK(fm_engine_get_param(&ct_a.engine, FM_PARAM_LFO_PW_PCT) > 0);
        TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_AMP_VELOCITY), 100);
        TT_CHECK(ct_a.engine.voices[0].amp_env.peak < (1 << 30));
    }
    cpu_test_use_full_patch(&ct_a, 0);
    cpu_test_request_voices(&ct_a, 3);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_AMP_VELOCITY), 0);
}

/* A full patch the engine refuses (here an unknown param) fails loudly: the step is marked failed and
 * its blocks never count as the full patch's cost. */
TT_TEST(cpu_test_full_patch_that_fails_is_not_timed)
{
    static const cpu_test_param BAD[] = {{FM_PARAM_OSC2_LEVEL, 500}, {FM_PARAM_COUNT, 1}};
    uint32_t v, pm, i;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_set_full_patch(&ct_a, BAD, 2);
    TT_CHECK(!cpu_test_failed(&ct_a, CT_FULL));
    cpu_test_request_voices(&ct_a, (int32_t)CT_FULL);
    cpu_test_sweep(&ct_a, 2);
    for (i = 0; i < 4; i++) {
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        cpu_test_record(&ct_a, CT_BLOCK_TICKS / 4);
    }
    TT_CHECK(cpu_test_failed(&ct_a, CT_FULL));
    TT_CHECK_EQ(cpu_test_worst_permille(&ct_a, CT_FULL), 0);
    TT_CHECK_EQ(cpu_test_take_completed(&ct_a, &v, &pm), 1);    /* the step still ends, so the log says so */
    TT_CHECK_EQ(v, CT_FULL);
    TT_CHECK_EQ(pm, 0);
    cpu_test_set_full_patch(&ct_a, NULL, 0);                    /* back to the built-in patch */
    cpu_test_request_voices(&ct_a, (int32_t)CT_FULL);
    cpu_test_render(&ct_a, ct_out, CT_BLOCK);
    TT_CHECK(!cpu_test_failed(&ct_a, CT_FULL));
    TT_CHECK_EQ(fm_engine_get_param(&ct_a.engine, FM_PARAM_AMP_VELOCITY), 100);
}

/* F's filter does real work, off fm_filter_run's wide-open fast path (a wire): in every voice the lowpass
 * is below open (fade, and with it the resonance, engaged) and the highpass is on, each on its own. */
TT_TEST(cpu_test_full_patch_filter_is_off_the_fast_path)
{
    uint32_t i, b;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_request_voices(&ct_a, (int32_t)CT_FULL);
    for (b = 0; b < 200; b++) {                                 /* through attack and decay to sustain */
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        for (i = 0; i < ct_a.engine.voice_count; i++) {
            const fm_filter *f = &ct_a.engine.voices[i].filter;
            TT_CHECK(f->fade != 0);                             /* the lowpass */
            TT_CHECK(f->k != 0);                                /* its resonance */
            TT_CHECK(f->hp_g != 0);                             /* the highpass */
        }
    }
}

/* F times the lowpass's costlier general path: the envelope sweeps the cutoff into the top of its range
 * (the fade, 0 < fade < 1.0, where every stage weighs its previous input), not only the cheaper path
 * below it. */
TT_TEST(cpu_test_full_patch_sweeps_the_filter_through_its_fade)
{
    uint32_t i, b, faded = 0;
    TT_CHECK_EQ(cpu_test_init(&ct_a), 0);
    cpu_test_request_voices(&ct_a, (int32_t)CT_FULL);
    for (b = 0; b < 200; b++) {                                 /* through attack and decay to sustain */
        cpu_test_render(&ct_a, ct_out, CT_BLOCK);
        for (i = 0; i < ct_a.engine.voice_count; i++) {
            const fm_filter *f = &ct_a.engine.voices[i].filter;
            if (f->fade > 0 && f->fade < (1 << 30))
                faded++;
        }
    }
    TT_CHECK(faded > 0);
}

static void run_cpu_test_tests(void)
{
    TT_RUN(cpu_test_full_patch_sweeps_the_filter_through_its_fade);
    TT_RUN(cpu_test_full_patch_filter_is_off_the_fast_path);
    TT_RUN(cpu_test_full_patch_that_fails_is_not_timed);
    TT_RUN(cpu_test_full_patch_can_play_at_any_voice_count);
    TT_RUN(cpu_test_full_patch_strikes_varied_velocities);
    TT_RUN(cpu_test_default_steps_leave_the_patch_at_its_defaults);
    TT_RUN(cpu_test_full_patch_step_turns_on_every_modulation);
    TT_RUN(cpu_test_render_adds_engine_audio_into_host_buffer);
    TT_RUN(cpu_test_voice_request_clamps_and_applies_at_next_block);
    TT_RUN(cpu_test_keeps_worst_block_time_per_voice_count);
    TT_RUN(cpu_test_sweep_steps_up_one_voice_per_hold_then_the_burst_then_the_full_patch);
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
    TT_RUN(cpu_test_full_patch_ends_the_sweep_after_its_hold);
}
