#include <stdlib.h>
/* Engine behavior tests, through the public interface only. */
#include "../engine/fm1_8p.h"

#define RATE 44100u

static fm_engine eng;
static int16_t buf[RATE * 2 * 4]; /* up to 4 s of stereo */

TT_TEST(note_on_plays_saw_at_note_pitch)
{
    double period;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_note_on(&eng, 69, 100);
    fm_engine_render(&eng, buf, RATE);
    period = period_from_crossings(buf + 2 * 4800, RATE - 4800, 0);
    /* within 1 cent */
    TT_CHECK_NEAR(period, RATE / note_hz(69), RATE / note_hz(69) * 0.0006);
}

/*
 * Aliasing threshold for a high saw, as documented in docs/fixed-point.md:
 * energy away from the harmonics of C7 (2093 Hz) at 44.1 kHz must be at
 * least ALIAS_LIMIT_DB below the total. On this measurement a naive
 * (non-band-limited) saw gives -12.6 dB and the 2-point polyBLEP saw
 * -27.9 dB, matching a double-precision polyBLEP model.
 */
#define ALIAS_LIMIT_DB (-25.0)

TT_TEST(high_saw_note_has_low_aliasing)
{
    double db;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_note_on(&eng, 96, 100);
    fm_engine_render(&eng, buf, 4800 + 32768);
    db = aliasing_db(buf + 2 * 4800, 32768, 0, note_hz(96), RATE, 6);
    printf("    aliasing for note 96 at 44.1 kHz: %.1f dB\n", db);
    TT_CHECK(db < ALIAS_LIMIT_DB);
}

/* Envelope level at time `ms`: peak over the preceding 3 ms (> 1 period). */
static double amp_at(const int16_t *stereo, uint32_t rate, double ms)
{
    size_t end = (size_t)(ms * rate / 1000.0);
    return peak_abs(stereo, end - rate * 3 / 1000, end);
}

TT_TEST(amp_envelope_follows_adsr_settings)
{
    double full;
    size_t i, silent_from;
    int loud_after = 0;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 100), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 200), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 500), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 30), FM_OK);
    fm_engine_note_on(&eng, 69, 100);
    fm_engine_render(&eng, buf, RATE);           /* 0..1 s held */
    fm_engine_note_off(&eng, 69);
    fm_engine_render(&eng, buf + 2 * RATE, RATE); /* 1..2 s released */

    full = amp_at(buf, RATE, 101);
    TT_CHECK(full > 5000); /* -12 dB mix gain */
    /* attack: an RC charge toward 1.6 x full, on full at 100 ms (62 % half way) */
    TT_CHECK_NEAR(amp_at(buf, RATE, 50) / full, 0.620, 0.04);
    TT_CHECK(amp_at(buf, RATE, 85) / full < 0.95);
    /* decay: exponential toward 50 %, tau 200 ms: 50 % + 50 % x e^(-t / tau) */
    TT_CHECK_NEAR(amp_at(buf, RATE, 203) / full, 0.803, 0.03);
    TT_CHECK_NEAR(amp_at(buf, RATE, 303) / full, 0.684, 0.03);
    /* sustain holds */
    TT_CHECK_NEAR(amp_at(buf, RATE, 990) / full, 0.5, 0.03);
    /* release: exponential toward 0, tau 30 ms (50 % x e^(-0.5) at 15 ms),
     * then idle below -80 dB of full: ln(0.5 / 1e-4) x tau = 256 ms */
    TT_CHECK_NEAR(amp_at(buf, RATE, 1015) / full, 0.303, 0.03);
    TT_CHECK(amp_at(buf, RATE, 1200) > 0);
    silent_from = (size_t)RATE * 1270 / 1000;
    for (i = silent_from * 2; i < (size_t)RATE * 4; i++)
        if (buf[i] != 0) loud_after = 1;
    TT_CHECK(!loud_after);
}

TT_TEST(set_param_clamps_to_range_and_rejects_unknown_ids)
{
    const fm_param_info *info = fm_param_info_get(FM_PARAM_AMP_SUSTAIN);
    uint16_t id;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        const fm_param_info *p = fm_param_info_get(id);
        TT_CHECK(p != NULL && p->name != NULL);
        TT_CHECK(p->min <= p->def && p->def <= p->max);
        TT_CHECK_EQ(fm_engine_get_param(&eng, id), p->def);
    }
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 5000), FM_OK);
    TT_CHECK_EQ(fm_engine_get_param(&eng, FM_PARAM_AMP_SUSTAIN), info->max);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, -7), FM_OK);
    TT_CHECK_EQ(fm_engine_get_param(&eng, FM_PARAM_AMP_SUSTAIN), info->min);
    TT_CHECK(fm_param_info_get(FM_PARAM_COUNT) == NULL);
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_COUNT, 1), FM_ERR_ARG);
}

TT_TEST(init_rejects_bad_arguments)
{
    TT_CHECK_EQ(fm_engine_init(NULL, RATE, 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, 0, 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, 7999, 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, 192001, 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, 8000, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_init(&eng, 192000, 1), FM_OK);
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 0), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, FM_MAX_VOICES + 1), FM_ERR_ARG);
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, FM_MAX_VOICES), FM_OK);
}

TT_TEST(all_notes_off_controller_releases_the_note)
{
    size_t i;
    int loud_after = 0;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    /* tau 1 ms: the release is below -80 dB and idle within 9.2 ms */
    TT_CHECK_EQ(fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 1), FM_OK);
    fm_engine_note_on(&eng, 60, 100);
    fm_engine_render(&eng, buf, 4800);
    TT_CHECK(peak_abs(buf, 2400, 4800) > 1000);
    fm_engine_controller(&eng, 123, 0); /* all notes off */
    fm_engine_render(&eng, buf, 4800);
    for (i = 2 * 480; i < 2 * 4800; i++)
        if (buf[i] != 0) loud_after = 1;
    TT_CHECK(!loud_after);
}

/* Plays a short phrase, rendering in blocks of `block` frames. */
static void render_phrase_in_blocks(int16_t *out, uint32_t frames,
                                    uint32_t block, int loud)
{
    uint32_t done = 0;
    fm_engine_init(&eng, RATE, 1);
    if (loud) { /* both oscillators full and the chorus: past the output's knee */
        fm_engine_set_param(&eng, FM_PARAM_OSC2_LEVEL, 1000);
        fm_engine_set_param(&eng, FM_PARAM_CHORUS_MODE, 1);
    }
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 3);
    fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 40);
    while (done < frames) {
        uint32_t n = frames - done < block ? frames - done : block;
        /* events land on fixed frames, whatever the block size */
        if (done == 1000) fm_engine_note_on(&eng, 57, 100);
        if (done == 6000) fm_engine_note_off(&eng, 57);
        if (done < 1000 && done + n > 1000) n = 1000 - done;
        if (done < 6000 && done + n > 6000) n = 6000 - done;
        fm_engine_render(&eng, out + 2 * done, n);
        done += n;
    }
}

TT_TEST(block_size_does_not_change_output)
{
    static int16_t whole[2 * 9000], blocked[2 * 9000];
    static const uint32_t sizes[] = { 256, 1, 255, 4096 };
    size_t k;
    int loud;
    for (loud = 0; loud <= 1; loud++) {
        render_phrase_in_blocks(whole, 9000, 9000, loud);
        /* the loud phrase goes past the soft clip's knee (#24) */
        TT_CHECK(peak_abs(whole, 2000, 5000) > (loud ? 16384 : 1000));
        for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
            render_phrase_in_blocks(blocked, 9000, sizes[k], loud);
            TT_CHECK(memcmp(whole, blocked, sizeof whole) == 0);
        }
    }
}

/*
 * Division by zero does not trap on the FM-1's CPU (it returns garbage),
 * so every divisor must be non-zero by construction. Here the desktop
 * would trap (SIGFPE) on any division by zero, and the output must stay
 * bounded and fall silent after release at every extreme.
 */
TT_TEST(extreme_settings_stay_bounded_and_release)
{
    static const uint32_t rates[] = { 8000, 11025, 44100, 192000 };
    static const uint8_t notes[] = { 0, 1, 60, 126, 127 };
    size_t r, n, i;
    for (r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        for (n = 0; n < sizeof notes / sizeof notes[0]; n++) {
            int loud_after = 0;
            TT_CHECK_EQ(fm_engine_init(&eng, rates[r], 1), FM_OK);
            fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
            fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
            fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
            fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 0);
            fm_engine_note_on(&eng, notes[n], 127);
            fm_engine_render(&eng, buf, 2000);
            TT_CHECK(peak_abs(buf, 0, 2000) <= 16384 + 64);
            fm_engine_note_off(&eng, notes[n]);
            fm_engine_render(&eng, buf, 2000);
            for (i = 2; i < 2 * 2000; i++)
                if (buf[i] != 0) loud_after = 1;
            TT_CHECK(!loud_after);
        }
    }
    /* longest times at the highest rate: steps must not round to zero */
    TT_CHECK_EQ(fm_engine_init(&eng, 192000, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 30000);
    fm_engine_note_on(&eng, 60, 100);
    fm_engine_render(&eng, buf, RATE);
    TT_CHECK(peak_abs(buf, RATE - 1000, RATE) > 0);
}

/* JX-8P attacks run to about 25 s (PG-8X 2.0.13's ATTACK 127, docs/patches.md):
 * a 20 s attack is at 62 % at 10 s (the RC charge: 1.6 x (1 - e^(-0.49))),
 * not clamped short. Reads the amp
 * envelope's level, so the wave's phase does not matter. */
TT_TEST(amp_attack_runs_to_25_seconds)
{
    const uint32_t r = 8000;
    int32_t peak;
    TT_CHECK_EQ(fm_engine_init(&eng, r, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 20000);
    fm_engine_note_on(&eng, 60, 127);
    peak = eng.voices[0].amp_env.peak;
    fm_engine_render(&eng, buf, r * 10);
    TT_CHECK_EQ(eng.voices[0].amp_env.stage, FM_ENV_ATTACK);
    TT_CHECK(eng.voices[0].amp_env.level > peak / 100 * 61 &&
             eng.voices[0].amp_env.level < peak / 100 * 63);
}

/*
 * VCA DYNAMICS, as PG-8X 2.0.13 plays it: the note's level is the DYNAMICS
 * share at its velocity (engine/dyna.c, the curve measured for #8), so at
 * position 3 velocity 40 is 15.6 dB under velocity 127 (PG-8X: 15.5).
 */
TT_TEST(amp_dyna_sets_the_level_by_the_dynamics_curve)
{
    int32_t p40, p127;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_DYNA, 3);
    fm_engine_note_on(&eng, 60, 40);
    p40 = eng.voices[0].amp_env.peak;
    fm_engine_note_off(&eng, 60);
    fm_engine_render(&eng, buf, RATE);
    fm_engine_note_on(&eng, 60, 127);
    p127 = eng.voices[0].amp_env.peak;
    printf("    amp_dyna 3: v40 %.4f, v127 %.4f of full (%.2f dB apart)\n",
           (double)p40 / FM_ENV_FULL, (double)p127 / FM_ENV_FULL,
           20.0 * log10((double)p40 / p127));
    TT_CHECK_EQ(p40, (int32_t)(((int64_t)FM_ENV_FULL * 5322) >> 15));
    TT_CHECK_EQ(p127, (int32_t)(((int64_t)FM_ENV_FULL * 31891) >> 15));
}

/*
 * MIX DYNAMICS, as PG-8X 2.0.13 plays it: the MIX ENV depth times the
 * DYNAMICS share at the note's velocity (engine/dyna.c). osc2 at 0 with
 * MIX ENV at full depth on the filter envelope, held at full: at position
 * 3 velocity 40 it settles at the share, 0.162 of full.
 */
TT_TEST(mix_env_dyna_scales_the_mix_envelope_by_the_dynamics_curve)
{
    int32_t got, want = (int32_t)(((int64_t)FM_OSC_LEVEL_FULL * 5322) >> 15);
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_LEVEL, 0);
    fm_engine_set_param(&eng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
    fm_engine_set_param(&eng, FM_PARAM_OSC_MIX_ENV, 1000);
    fm_engine_set_param(&eng, FM_PARAM_OSC_MIX_ENV_DYNA, 3);
    fm_engine_note_on(&eng, 60, 40);
    fm_engine_render(&eng, buf, RATE / 10);
    got = eng.voices[0].osc.level[1];
    printf("    mix env dyna 3, v40: osc2 at %.4f of full (want %.4f)\n",
           (double)got / FM_OSC_LEVEL_FULL, (double)want / FM_OSC_LEVEL_FULL);
    TT_CHECK(got > want - FM_OSC_LEVEL_FULL / 1000 && got < want + FM_OSC_LEVEL_FULL / 1000);
}

/* Release is exponential, as PG-8X's: the release time is its time constant
 * tau, so the level falls 20 dB every tau x ln 10 (here 2.303 s for 1 s) and
 * 40 dB in twice that. A ramp of the same time would be silent by then. */
TT_TEST(amp_release_falls_exponentially_with_its_time_constant)
{
    const uint32_t r = 8000;
    int32_t peak;
    TT_CHECK_EQ(fm_engine_init(&eng, r, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 1000);
    fm_engine_note_on(&eng, 60, 127);
    fm_engine_render(&eng, buf, r / 10);
    peak = eng.voices[0].amp_env.level;
    fm_engine_note_off(&eng, 60);
    fm_engine_render(&eng, buf, r * 2303 / 1000);
    TT_CHECK(eng.voices[0].amp_env.level > peak / 1000 * 98 &&
             eng.voices[0].amp_env.level < peak / 1000 * 102);
    fm_engine_render(&eng, buf, r * 2303 / 1000);
    TT_CHECK(eng.voices[0].amp_env.level > peak / 10000 * 98 &&
             eng.voices[0].amp_env.level < peak / 10000 * 102);
}

/* Decay is exponential toward the sustain level, not toward 0: the distance
 * above sustain falls by 1/e every tau, it gets within 1 % of sustain, and
 * the level never goes below it. Sustain 50 %, tau 100 ms. */
TT_TEST(amp_decay_heads_exponentially_to_sustain_without_undershoot)
{
    const uint32_t r = 8000;
    int32_t peak, sus, d, low;
    uint32_t i;
    TT_CHECK_EQ(fm_engine_init(&eng, r, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 100);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 500);
    fm_engine_note_on(&eng, 60, 127);
    fm_engine_render(&eng, buf, 1); /* the attack lands on the peak */
    peak = eng.voices[0].amp_env.peak;
    sus = eng.voices[0].amp_env.sus_level;
    fm_engine_render(&eng, buf, r / 10); /* one tau */
    d = eng.voices[0].amp_env.level - sus;
    TT_CHECK(d > (int32_t)((int64_t)(peak - sus) * 360 / 1000) &&
             d < (int32_t)((int64_t)(peak - sus) * 376 / 1000)); /* 1/e = 0.368 */
    low = eng.voices[0].amp_env.level;
    for (i = 0; i < r * 2; i++) { /* 20 tau */
        fm_engine_render(&eng, buf, 1);
        if (eng.voices[0].amp_env.level < low) low = eng.voices[0].amp_env.level;
    }
    TT_CHECK(low >= sus);
    TT_CHECK(eng.voices[0].amp_env.level - sus <= sus / 100);
}

/* Every envelope time, both envelopes, has one range: 0 to 32 s, as long as
 * the patch table's int16 holds (PG-8X's ATTACK 127 takes 34 s to its peak,
 * docs/patches.md). */
TT_TEST(envelope_times_share_one_32_second_range)
{
    static const uint16_t ids[] = {
        FM_PARAM_AMP_ATTACK_MS, FM_PARAM_AMP_DECAY_MS, FM_PARAM_AMP_RELEASE_MS,
        FM_PARAM_FILTER_ENV_ATTACK_MS, FM_PARAM_FILTER_ENV_DECAY_MS,
        FM_PARAM_FILTER_ENV_RELEASE_MS};
    size_t i;
    for (i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        TT_CHECK_EQ(fm_param_info_get(ids[i])->min, 0);
        TT_CHECK_EQ(fm_param_info_get(ids[i])->max, 32000);
    }
}

/* A zeroed engine (static storage before, or after a failed, init) must
 * not divide by its zero sample rate. */
TT_TEST(uninitialized_engine_is_silent_and_does_not_divide_by_zero)
{
    static fm_engine zero;
    size_t i;
    int loud = 0;
    memset(&zero, 0, sizeof zero);
    fm_engine_note_on(&zero, 60, 100);
    fm_engine_set_param(&zero, FM_PARAM_AMP_RELEASE_MS, 100);
    fm_engine_render(&zero, buf, 256);
    for (i = 0; i < 2 * 256; i++)
        if (buf[i] != 0) loud = 1;
    TT_CHECK(!loud);
}

/*
 * Raising sustain while a 0 ms decay is in progress once overflowed a
 * signed comparison (UBSan: sustain + step = 2^31). The level must end
 * up at the new sustain (full), never wrap negative or past full.
 */
TT_TEST(raising_sustain_mid_decay_does_not_overflow)
{
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 0);
    fm_engine_note_on(&eng, 45, 100);
    fm_engine_render(&eng, buf, 1);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_render(&eng, buf, 4410);
    TT_CHECK(peak_abs(buf, 2000, 4410) > 7500); /* -12 dB mix gain */
    TT_CHECK(peak_abs(buf, 0, 4410) <= 16384 + 64);
}

TT_TEST(shortening_release_mid_release_takes_effect)
{
    size_t i;
    int loud_after = 0;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 10000);
    fm_engine_note_on(&eng, 57, 100);
    fm_engine_render(&eng, buf, 4410);
    fm_engine_note_off(&eng, 57);
    fm_engine_render(&eng, buf, 441);
    TT_CHECK(peak_abs(buf, 0, 441) > 1000);
    fm_engine_set_param(&eng, FM_PARAM_AMP_RELEASE_MS, 0);
    fm_engine_render(&eng, buf, 441);
    for (i = 2 * 2; i < 2 * 441; i++)
        if (buf[i] != 0) loud_after = 1;
    TT_CHECK(!loud_after);
}

/*
 * Raising sustain above the current level mid-decay moves the level
 * there with the decay's tau instead of jumping (a click). At 0.5 tau of a
 * decay to 0 the level is e^(-0.5) = 0.607 of full.
 */
TT_TEST(raising_sustain_mid_decay_glides_instead_of_jumping)
{
    int32_t peak, before, just_after;
    TT_CHECK_EQ(fm_engine_init(&eng, RATE, 1), FM_OK);
    fm_engine_set_param(&eng, FM_PARAM_AMP_ATTACK_MS, 0);
    fm_engine_set_param(&eng, FM_PARAM_AMP_DECAY_MS, 1000);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 0);
    fm_engine_note_on(&eng, 69, 100);
    fm_engine_render(&eng, buf, RATE / 2); /* 0.5 tau */
    peak = eng.voices[0].amp_env.peak;
    before = eng.voices[0].amp_env.level;
    TT_CHECK_NEAR((double)before / peak, 0.607, 0.01);
    fm_engine_set_param(&eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_render(&eng, buf, RATE / 100); /* 10 ms on: no jump */
    just_after = eng.voices[0].amp_env.level;
    TT_CHECK(just_after > before && (double)(just_after - before) / peak < 0.01);
    fm_engine_render(&eng, buf, 4 * RATE); /* 4 tau: nearly there */
    TT_CHECK_NEAR((double)eng.voices[0].amp_env.level / peak, 1.0, 0.02);
}

/* One voice at osc 1 level `osc1` and osc 2 level `osc2` (per mille),
 * velocity 127, filter open, chorus off: its peak over 0.2..0.5 s. */
static int saws_peak(int32_t osc1, int32_t osc2)
{
    fm_engine_init(&eng, RATE, 1);
    fm_engine_set_param(&eng, FM_PARAM_OSC1_LEVEL, osc1);
    fm_engine_set_param(&eng, FM_PARAM_OSC2_LEVEL, osc2);
    fm_engine_note_on(&eng, 57, 127);
    fm_engine_render(&eng, buf, RATE / 2);
    return peak_abs(buf, RATE / 5, RATE / 2);
}

/*
 * Output makeup (#24): the mix gain leaves 12 dB of headroom inside the
 * engine; +6 dB at the output brings it back. Below the soft clip's knee
 * (-6 dBFS) it is a pure gain: this saw peaked at 5558 with the trim
 * alone (1995429), so twice that, within the 1 LSB of rounding.
 */
TT_TEST(output_makeup_is_6_db_below_the_knee)
{
    int p = saws_peak(800, 0);
    printf("    one saw at level 800: peak %d (%.3f of full scale)\n", p, p / 32768.0);
    TT_CHECK(abs(p - 2 * 5558) <= 1);
}

/*
 * Through the knee and above it, more in is more out, and never full
 * scale: osc 1 full, osc 2 from 0 to full in steps of 100 takes one voice
 * from below the knee (0.42 of full scale) to past it (0.85 in).
 */
TT_TEST(output_soft_clip_is_monotonic_and_short_of_full_scale)
{
    int32_t osc2;
    int prev = 0, p = 0;
    for (osc2 = 0; osc2 <= 1000; osc2 += 100) {
        p = saws_peak(1000, osc2);
        TT_CHECK(p > prev);
        prev = p;
    }
    printf("    both saws full: peak %d (%.1f dBFS)\n", p, 20 * log10(p / 32768.0));
    TT_CHECK(p > 16384 && p < 32767);
}

static void run_engine_tests(void)
{
    TT_RUN(output_makeup_is_6_db_below_the_knee);
    TT_RUN(output_soft_clip_is_monotonic_and_short_of_full_scale);
    TT_RUN(note_on_plays_saw_at_note_pitch);
    TT_RUN(high_saw_note_has_low_aliasing);
    TT_RUN(amp_envelope_follows_adsr_settings);
    TT_RUN(amp_attack_runs_to_25_seconds);
    TT_RUN(amp_dyna_sets_the_level_by_the_dynamics_curve);
    TT_RUN(mix_env_dyna_scales_the_mix_envelope_by_the_dynamics_curve);
    TT_RUN(amp_release_falls_exponentially_with_its_time_constant);
    TT_RUN(amp_decay_heads_exponentially_to_sustain_without_undershoot);
    TT_RUN(envelope_times_share_one_32_second_range);
    TT_RUN(set_param_clamps_to_range_and_rejects_unknown_ids);
    TT_RUN(init_rejects_bad_arguments);
    TT_RUN(all_notes_off_controller_releases_the_note);
    TT_RUN(block_size_does_not_change_output);
    TT_RUN(extreme_settings_stay_bounded_and_release);
    TT_RUN(uninitialized_engine_is_silent_and_does_not_divide_by_zero);
    TT_RUN(raising_sustain_mid_decay_does_not_overflow);
    TT_RUN(shortening_release_mid_release_takes_effect);
    TT_RUN(raising_sustain_mid_decay_glides_instead_of_jumping);
}
