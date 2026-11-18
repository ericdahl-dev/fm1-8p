/*
 * The voice filter (engine/vfilter.h, #72): the filter of one voice with the
 * cutoff it composes from its settings, the note and the voice's modulation,
 * and the control cadence it updates at. Tested on a voice filter alone.
 */
#include "../engine/vfilter.h"

#define VF_RATE 48000u

static fm_vfilter vf;

static fm_vfilter *vf_reset(void)
{
    fm_vfilter_reset(&vf, fm_filter_rate_cents(VF_RATE));
    return &vf;
}

/* 1. A reset voice filter is a wire: open, the highpass off, every sample
 * through unchanged. */
TT_TEST(vfilter_reset_is_a_wire)
{
    fm_vfilter *f = vf_reset();
    TT_CHECK(fm_vfilter_is_wire(f));
    TT_CHECK_EQ(fm_vfilter_run(f, 1 << 24), 1 << 24);
    TT_CHECK_EQ(fm_vfilter_run(f, -12345), -12345);
}

/* 2. A base cutoff below open: the next update sets the filter there (no
 * key tracking, no modulation), and it is no longer a wire. */
TT_TEST(vfilter_base_below_open_closes_it)
{
    fm_vfilter *f = vf_reset();
    int32_t base = fm_filter_linear_cents(6000);
    fm_vfilter_set_base(f, base);
    TT_CHECK(fm_vfilter_update(f, 0));
    TT_CHECK(!fm_vfilter_is_wire(f));
    TT_CHECK_EQ(fm_vfilter_cutoff(f), fm_filter_voice_cents(base, 0, 0, 0));
}

/* 3. Key tracking: the cutoff follows the note it is given, by the track
 * setting (fm_filter_voice_cents' pivot and scale). */
TT_TEST(vfilter_cutoff_follows_the_key)
{
    fm_vfilter *f = vf_reset();
    int32_t base = fm_filter_linear_cents(6000), at60, at84;
    fm_vfilter_set_base(f, base);
    fm_vfilter_set_key_track(f, 1000);
    fm_vfilter_set_key(f, 60);
    fm_vfilter_update(f, 0);
    at60 = fm_vfilter_cutoff(f);
    fm_vfilter_set_key(f, 84);
    TT_CHECK(fm_vfilter_update(f, 0));
    at84 = fm_vfilter_cutoff(f);
    TT_CHECK_EQ(at60, fm_filter_voice_cents(base, 1000, 60, 0));
    TT_CHECK_EQ(at84, fm_filter_voice_cents(base, 1000, 84, 0));
    TT_CHECK(at84 > at60 + 2000);
}

/* 4. The voice's modulation adds into the composition, before the curve
 * (fm_filter_voice_cents), and may close it below the base. */
TT_TEST(vfilter_modulation_adds_into_the_cutoff)
{
    fm_vfilter *f = vf_reset();
    int32_t base = fm_filter_linear_cents(7000);
    fm_vfilter_set_base(f, base);
    fm_vfilter_update(f, 1200);
    TT_CHECK_EQ(fm_vfilter_cutoff(f), fm_filter_voice_cents(base, 0, 0, 1200));
    fm_vfilter_update(f, -2400);
    TT_CHECK_EQ(fm_vfilter_cutoff(f), fm_filter_voice_cents(base, 0, 0, -2400));
    TT_CHECK(fm_vfilter_cutoff(f) < fm_filter_voice_cents(base, 0, 0, 0));
}

/* 5. An update that composes the same cutoff leaves the filter as it is
 * (no coefficients recomputed). */
TT_TEST(vfilter_same_cutoff_keeps_the_coefficients)
{
    fm_vfilter *f = vf_reset();
    fm_vfilter_set_base(f, fm_filter_linear_cents(6000));
    TT_CHECK(fm_vfilter_update(f, 300));
    TT_CHECK(!fm_vfilter_update(f, 300));
    TT_CHECK(fm_vfilter_update(f, 301));
}

/* 6. The cadence: a control point (an update is due) on the first sample
 * and then every FM_VFILTER_CTRL_SAMPLES (16); passing n samples at once
 * (the fast path) leaves it where n samples one at a time would; and a
 * note start makes the next sample a control point. */
TT_TEST(vfilter_control_points_every_16_samples)
{
    fm_vfilter *f = vf_reset();
    static fm_vfilter g;
    uint32_t k, n, due = 0, at[3], seed = 1;
    for (k = 0; k < 40; k++)
        if (fm_vfilter_due(f)) { if (due < 3) at[due] = k; due++; }
    TT_CHECK_EQ(due, 3u);
    TT_CHECK_EQ(at[0], 0u);
    TT_CHECK_EQ(at[1], 16u);
    TT_CHECK_EQ(at[2], 32u);
    for (k = 0; k < 50; k++) { /* pass(n) = n samples */
        seed = seed * 1103515245u + 12345u;
        n = (seed >> 16) % 300u;
        g = *f;
        fm_vfilter_pass(f, 0, n);
        while (n--) fm_vfilter_due(&g);
        TT_CHECK_EQ(fm_vfilter_due(f), fm_vfilter_due(&g));
    }
    fm_vfilter_due(f); fm_vfilter_due(f);
    fm_vfilter_due_next(f);
    TT_CHECK(fm_vfilter_due(f));
    TT_CHECK(!fm_vfilter_due(f));
}

/* 7. A wire passed n samples at once keeps the last of them, so a filter
 * that closes after it carries on from there exactly as one that ran
 * every sample. */
TT_TEST(vfilter_pass_keeps_the_last_sample_for_closing)
{
    static fm_vfilter a, b;
    int32_t x[64];
    uint32_t i;
    int same = 1;
    for (i = 0; i < 64; i++) x[i] = (int32_t)((i * 2654435761u) >> 8) - (1 << 23);
    fm_vfilter_reset(&a, fm_filter_rate_cents(VF_RATE));
    fm_vfilter_reset(&b, fm_filter_rate_cents(VF_RATE));
    for (i = 0; i < 32; i++) fm_vfilter_run(&a, x[i]);
    fm_vfilter_pass(&b, x[31], 32);
    fm_vfilter_pass(&b, 12345, 0); /* no samples: nothing to keep */
    fm_vfilter_set_base(&a, fm_filter_linear_cents(5000));
    fm_vfilter_set_base(&b, fm_filter_linear_cents(5000));
    fm_vfilter_update(&a, 0);
    fm_vfilter_update(&b, 0);
    for (i = 32; i < 64; i++) same &= fm_vfilter_run(&a, x[i]) == fm_vfilter_run(&b, x[i]);
    TT_CHECK(same);
}

static void run_vfilter_tests(void)
{
    TT_RUN(vfilter_reset_is_a_wire);
    TT_RUN(vfilter_base_below_open_closes_it);
    TT_RUN(vfilter_cutoff_follows_the_key);
    TT_RUN(vfilter_modulation_adds_into_the_cutoff);
    TT_RUN(vfilter_same_cutoff_keeps_the_coefficients);
    TT_RUN(vfilter_control_points_every_16_samples);
    TT_RUN(vfilter_pass_keeps_the_last_sample_for_closing);
}
