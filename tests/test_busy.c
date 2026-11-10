/*
 * Voice busy flags (engine/voice.h, fm_voice_busy): a voice whose
 * processors are all at rest renders on the fast path. Each test moves one
 * processor away from rest and back, and checks the voice settles.
 */
#include "../engine/voice.h"

static fm_engine beng;
static int16_t bbuf[2 * 4410];

/* A fresh engine at 44.1 kHz holding note 57 for `frames`. */
static const fm_voice *busy_hold(uint32_t frames)
{
    fm_engine_init(&beng, 44100u, 1);
    fm_engine_note_on(&beng, 57, 100);
    fm_engine_render(&beng, bbuf, frames);
    return &beng.voices[0];
}

/* The default patch, once its first control tick has set the cutoff. */
TT_TEST(busy_default_patch_is_at_rest)
{
    TT_CHECK_EQ(fm_voice_busy(busy_hold(64)), 0);
}

/* Filter envelope: an amount makes the voice busy; back to 0, it rests. */
TT_TEST(busy_filter_env_amount_back_to_zero_rests)
{
    const fm_voice *v = busy_hold(64);
    fm_engine_set_param(&beng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 2400);
    fm_engine_render(&beng, bbuf, 64);
    TT_CHECK(fm_voice_busy(v) != 0);
    fm_engine_set_param(&beng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 0);
    fm_engine_render(&beng, bbuf, 64);
    TT_CHECK_EQ(fm_voice_busy(v), 0);
}

/* Filter: the highpass on, or the cutoff closed below open, makes the voice
 * busy; back to off and fully open, it rests. */
TT_TEST(busy_filter_back_to_a_wire_rests)
{
    const fm_voice *v = busy_hold(64);
    fm_engine_set_param(&beng, FM_PARAM_HIGHPASS_STEP, 2);
    fm_engine_set_param(&beng, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    fm_engine_render(&beng, bbuf, 64);
    TT_CHECK(fm_voice_busy(v) != 0);
    fm_engine_set_param(&beng, FM_PARAM_HIGHPASS_STEP, 0);
    fm_engine_render(&beng, bbuf, 64);
    TT_CHECK(fm_voice_busy(v) != 0); /* still closed */
    fm_engine_set_param(&beng, FM_PARAM_FILTER_CUTOFF_CENTS,
                        fm_param_info_get(FM_PARAM_FILTER_CUTOFF_CENTS)->def);
    fm_engine_render(&beng, bbuf, 64);
    TT_CHECK_EQ(fm_voice_busy(v), 0);
}

/* Mix glide: the mix envelope on makes the voice busy; depth back to 0, it
 * rests once the glide lands. Both #65 cases: a glide landing exactly on
 * osc_mix (mix 500, depth 500) and one pushing at the clamp (mix 0, depth
 * +1000 plays osc2 alone). */
TT_TEST(busy_mix_env_back_to_zero_rests)
{
    static const int32_t cases[][2] = { { 500, 500 }, { 0, 1000 } };
    size_t k;
    for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        const fm_voice *v;
        fm_engine_init(&beng, 44100u, 1);
        fm_engine_set_param(&beng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
        fm_engine_set_param(&beng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
        fm_engine_set_param(&beng, FM_PARAM_OSC_MIX, cases[k][0]);
        fm_engine_set_param(&beng, FM_PARAM_OSC_MIX_ENV, cases[k][1]);
        fm_engine_note_on(&beng, 57, 100);
        fm_engine_render(&beng, bbuf, 4410);
        v = &beng.voices[0];
        TT_CHECK(fm_voice_busy(v) != 0);
        fm_engine_set_param(&beng, FM_PARAM_OSC_MIX_ENV, 0);
        fm_engine_render(&beng, bbuf, 64);
        TT_CHECK_EQ(fm_voice_busy(v), 0);
    }
}

/* Steal fade: with one voice a second note steals it; while the old note
 * fades the voice is busy, and once the new one starts it rests (3 ms
 * fade, 132 samples at 44.1 kHz). */
TT_TEST(busy_steal_fade_ends_at_rest)
{
    const fm_voice *v = busy_hold(64);
    fm_engine_note_on(&beng, 60, 100);
    fm_engine_render(&beng, bbuf, 16);
    TT_CHECK(fm_voice_busy(v) != 0);
    fm_engine_render(&beng, bbuf, 256);
    TT_CHECK_EQ(fm_voice_busy(v), 0);
}

static void run_busy_tests(void)
{
    TT_RUN(busy_default_patch_is_at_rest);
    TT_RUN(busy_filter_env_amount_back_to_zero_rests);
    TT_RUN(busy_filter_back_to_a_wire_rests);
    TT_RUN(busy_mix_env_back_to_zero_rests);
    TT_RUN(busy_steal_fade_ends_at_rest);
}
