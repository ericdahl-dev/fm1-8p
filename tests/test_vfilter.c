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
    TT_CHECK(fm_vfilter_update(f, 0, 0));
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
    fm_vfilter_update(f, 0, 0);
    at60 = fm_vfilter_cutoff(f);
    fm_vfilter_set_key(f, 84);
    TT_CHECK(fm_vfilter_update(f, 0, 0));
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
    fm_vfilter_update(f, 1200, 0);
    TT_CHECK_EQ(fm_vfilter_cutoff(f), fm_filter_voice_cents(base, 0, 0, 1200));
    fm_vfilter_update(f, -2400, 0);
    TT_CHECK_EQ(fm_vfilter_cutoff(f), fm_filter_voice_cents(base, 0, 0, -2400));
    TT_CHECK(fm_vfilter_cutoff(f) < fm_filter_voice_cents(base, 0, 0, 0));
}

/* 5. An update that composes the same cutoff leaves the filter as it is
 * (no coefficients recomputed). */
TT_TEST(vfilter_same_cutoff_keeps_the_coefficients)
{
    fm_vfilter *f = vf_reset();
    fm_vfilter_set_base(f, fm_filter_linear_cents(6000));
    TT_CHECK(fm_vfilter_update(f, 300, 0));
    TT_CHECK(!fm_vfilter_update(f, 300, 0));
    TT_CHECK(fm_vfilter_update(f, 301, 0));
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
    fm_vfilter_update(&a, 0, 0);
    fm_vfilter_update(&b, 0, 0);
    for (i = 32; i < 64; i++) same &= fm_vfilter_run(&a, x[i]) == fm_vfilter_run(&b, x[i]);
    TT_CHECK(same);
}

/* 8. At rest: the filter is set to what its settings and key compose with
 * no modulation. Not before its first update, not while modulation moves
 * it, nor after a setting or key changes until the next update. */
TT_TEST(vfilter_at_rest_means_set_to_its_settings)
{
    fm_vfilter *f = vf_reset();
    TT_CHECK(!fm_vfilter_at_rest(f));
    fm_vfilter_update(f, 0, 0);
    TT_CHECK(fm_vfilter_at_rest(f));
    fm_vfilter_set_base(f, fm_filter_linear_cents(6000));
    TT_CHECK(!fm_vfilter_at_rest(f));
    fm_vfilter_update(f, 500, 0);
    TT_CHECK(!fm_vfilter_at_rest(f));
    fm_vfilter_update(f, 0, 0);
    TT_CHECK(fm_vfilter_at_rest(f));
    fm_vfilter_set_key_track(f, 500);
    fm_vfilter_set_key(f, 90);
    TT_CHECK(!fm_vfilter_at_rest(f));
    fm_vfilter_update(f, 0, 0);
    TT_CHECK(fm_vfilter_at_rest(f));
}

/* 9. Resonance takes effect at once, as on the filter itself; the
 * highpass on makes the filter no wire, and off a wire again. */
TT_TEST(vfilter_resonance_and_highpass_pass_through)
{
    fm_vfilter *f = vf_reset();
    static fm_filter want;
    int32_t rate = fm_filter_rate_cents(VF_RATE), x;
    uint32_t i;
    int same = 1;
    fm_vfilter_set_base(f, fm_filter_linear_cents(6000));
    fm_vfilter_update(f, 0, 0);
    fm_vfilter_set_resonance(f, 700);
    fm_filter_reset(&want);
    fm_filter_set_cutoff(&want, fm_vfilter_cutoff(f), rate);
    fm_filter_set_resonance(&want, 700);
    for (i = 0; i < 64; i++) {
        x = (int32_t)((i * 2654435761u) >> 8) - (1 << 23);
        same &= fm_vfilter_run(f, x) == fm_filter_run(&want, x);
    }
    TT_CHECK(same);
    f = vf_reset();
    fm_vfilter_set_highpass(f, 1);
    TT_CHECK(!fm_vfilter_is_wire(f));
    fm_vfilter_set_highpass(f, 0);
    TT_CHECK(fm_vfilter_is_wire(f));
}

/* 12. Clear (all sound off): no state left to ring into the next note,
 * the settings and the cutoff kept. A closed filter that was ringing
 * then answers silence with silence. */
TT_TEST(vfilter_clear_drops_the_state_keeps_the_settings)
{
    fm_vfilter *f = vf_reset();
    int32_t cut;
    uint32_t i;
    int silent = 1;
    fm_vfilter_set_base(f, fm_filter_linear_cents(5000));
    fm_vfilter_set_resonance(f, 900);
    fm_vfilter_update(f, 0, 0);
    cut = fm_vfilter_cutoff(f);
    for (i = 0; i < 64; i++) fm_vfilter_run(f, (i & 8) ? 1 << 24 : -(1 << 24));
    fm_vfilter_clear(f);
    for (i = 0; i < 64; i++) silent &= fm_vfilter_run(f, 0) == 0;
    TT_CHECK(silent);
    TT_CHECK_EQ(fm_vfilter_cutoff(f), cut);
    TT_CHECK(fm_vfilter_at_rest(f));
}

/* The key-tracking setting fits the voice filter's 16 bits (it keeps it
 * as int16 so the struct packs): a wider range must widen the field. */
TT_TEST(vfilter_key_track_range_fits_16_bits)
{
    const fm_param_info *p = fm_param_info_get(FM_PARAM_FILTER_KEY_TRACK);
    TT_CHECK(p != 0);
    TT_CHECK(p->min >= INT16_MIN && p->max <= INT16_MAX);
}

static fm_engine vfe;
static int16_t vfbuf[2 * 64];

/* An engine of one voice at 48 kHz, the cutoff at 6000 cents with full key
 * tracking, playing `note`, rendered `frames` samples. */
static fm_voice *vf_engine(uint8_t note, uint32_t frames)
{
    fm_engine_init(&vfe, VF_RATE, 1);
    fm_engine_set_param(&vfe, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    fm_engine_set_param(&vfe, FM_PARAM_FILTER_KEY_TRACK, 1000);
    fm_engine_note_on(&vfe, note, 100);
    fm_engine_render(&vfe, vfbuf, frames);
    return &vfe.voices[0];
}

/* 10. A legato key change (unison: the voice changes note, no new start)
 * recomposes the cutoff at once, before the next sample runs, and the
 * cadence carries on: the next control point comes when it would have. */
TT_TEST(vfilter_legato_key_change_recomposes_at_once)
{
    fm_voice *v;
    int32_t base = fm_filter_linear_cents(6000);
    uint8_t phase;
    fm_engine_init(&vfe, VF_RATE, 1);
    fm_engine_set_param(&vfe, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    fm_engine_set_param(&vfe, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    fm_engine_set_param(&vfe, FM_PARAM_FILTER_KEY_TRACK, 1000);
    fm_engine_note_on(&vfe, 48, 100);
    fm_engine_render(&vfe, vfbuf, 7); /* mid-cadence */
    v = &vfe.voices[0];
    TT_CHECK_EQ(fm_vfilter_cutoff(fm_voice_filter(v)), fm_filter_voice_cents(base, 1000, 48, 0));
    phase = fm_vfilter_phase(fm_voice_filter(v));
    fm_engine_note_on(&vfe, 72, 100); /* the newest note: the voice changes to it */
    TT_CHECK_EQ(fm_vfilter_cutoff(fm_voice_filter(v)), fm_filter_voice_cents(base, 1000, 72, 0));
    TT_CHECK_EQ(fm_vfilter_phase(fm_voice_filter(v)), phase);
}

/* 11. A cutoff or key-track setting changed mid-note takes effect at the
 * voice's next control point, not before: on the sample `phase` + 1 from
 * now. */
TT_TEST(vfilter_setting_takes_effect_at_the_next_control_point)
{
    static const uint16_t ids[2] = { FM_PARAM_FILTER_CUTOFF_CENTS, FM_PARAM_FILTER_KEY_TRACK };
    static const int32_t vals[2] = { 5000, 500 };
    int k;
    for (k = 0; k < 2; k++) {
        fm_voice *v = vf_engine(72, 21); /* mid-cadence, off the tracking pivot (C4) */
        int32_t before = fm_vfilter_cutoff(fm_voice_filter(v));
        uint32_t n = 0, want = fm_vfilter_phase(fm_voice_filter(v)) + 1u;
        fm_engine_set_param(&vfe, ids[k], vals[k]);
        TT_CHECK_EQ(fm_vfilter_cutoff(fm_voice_filter(v)), before);
        while (fm_vfilter_cutoff(fm_voice_filter(v)) == before && n < 64) {
            fm_engine_render(&vfe, vfbuf, 1);
            n++;
        }
        TT_CHECK_EQ(n, want);
    }
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
    TT_RUN(vfilter_at_rest_means_set_to_its_settings);
    TT_RUN(vfilter_resonance_and_highpass_pass_through);
    TT_RUN(vfilter_clear_drops_the_state_keeps_the_settings);
    TT_RUN(vfilter_key_track_range_fits_16_bits);
    TT_RUN(vfilter_legato_key_change_recomposes_at_once);
    TT_RUN(vfilter_setting_takes_effect_at_the_next_control_point);
}
