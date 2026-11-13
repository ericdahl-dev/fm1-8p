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
 * osc2_level (mix 500, depth 500) and one pushing at the clamp (mix 0, depth
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
        fm_engine_set_param(&beng, FM_PARAM_OSC2_LEVEL, cases[k][0]);
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

/* Queued release: the new note's key comes up during the steal fade, so
 * its release waits as long as the fade did (132 samples after it starts
 * at 132). Busy while it waits; at rest once the release has begun. */
TT_TEST(busy_queued_release_ends_at_rest)
{
    const fm_voice *v = busy_hold(64);
    fm_engine_note_on(&beng, 60, 100);
    fm_engine_note_off(&beng, 60);
    fm_engine_render(&beng, bbuf, 200);
    TT_CHECK(fm_voice_busy(v) != 0);
    fm_engine_render(&beng, bbuf, 400);
    TT_CHECK_EQ(fm_voice_busy(v), 0);
}

/* The filter decides when it is a wire (fully open, highpass off): reset,
 * closed, open again, highpass on, and off. */
TT_TEST(busy_filter_says_when_it_is_a_wire)
{
    fm_filter f;
    int32_t rc = fm_filter_rate_cents(44100u);
    fm_filter_reset(&f);
    TT_CHECK(fm_filter_is_wire(&f));
    fm_filter_set_cutoff(&f, 6000, rc);
    TT_CHECK(!fm_filter_is_wire(&f));
    fm_filter_set_cutoff(&f, FM_FILTER_OPEN_CENTS, rc);
    TT_CHECK(fm_filter_is_wire(&f));
    fm_filter_set_highpass(&f, 1, rc);
    TT_CHECK(!fm_filter_is_wire(&f));
    fm_filter_set_highpass(&f, 0, rc);
    TT_CHECK(fm_filter_is_wire(&f));
    /* The bass boost (step 4) is not a wire; back at 0 the filter is a wire
     * again, and its output is its input even after the boost held state. */
    fm_filter_set_highpass(&f, FM_FILTER_HIGHPASS_BOOST, rc);
    TT_CHECK(!fm_filter_is_wire(&f));
    for (int i = 0; i < 64; i++)
        (void)fm_filter_process(&f, (i & 8) ? (1 << 22) : -(1 << 22));
    fm_filter_set_highpass(&f, 0, rc);
    TT_CHECK(fm_filter_is_wire(&f));
    for (int i = 0; i < 64; i++) {
        int32_t x = (i & 8) ? (1 << 22) : -(1 << 22);
        TT_CHECK(fm_filter_process(&f, x) == x);
    }
}

/*
 * The busy word is a cache: it must never say "at rest" while a processor
 * is away from rest (fm_voice_busy_derived reads them directly). Random
 * notes (steals, take backs, same-note restrikes), the sustain pedal, poly
 * and unison, the filter and mix settings and render lengths; checked on
 * every voice after every step.
 */
static uint32_t busy_rng;
static uint32_t busy_pick(uint32_t n)
{
    busy_rng = busy_rng * 1664525u + 1013904223u;
    return (busy_rng >> 8) % n;
}

static void busy_random_param(fm_engine *e)
{
    static const struct { uint16_t id; int32_t v[4]; } p[] = {
        {FM_PARAM_FILTER_CUTOFF_CENTS, {3000, 6000, 9000, 20000}},
        {FM_PARAM_HIGHPASS_STEP, {0, 0, 1, 4}},
        {FM_PARAM_FILTER_ENV_AMOUNT_CENTS, {0, 0, 2400, -1200}},
        {FM_PARAM_FILTER_KEY_TRACK, {0, 0, 500, 1000}},
        {FM_PARAM_FILTER_RESONANCE, {0, 0, 400, 900}},
        {FM_PARAM_OSC2_LEVEL, {0, 500, 500, 1000}},
        {FM_PARAM_OSC_MIX_ENV, {0, 0, 500, -700}},
        {FM_PARAM_OSC_MIX_ENV_SOURCE, {0, 0, 1, 1}},
        {FM_PARAM_VOICE_MODE, {FM_VOICE_MODE_POLY, FM_VOICE_MODE_POLY,
                               FM_VOICE_MODE_POLY, FM_VOICE_MODE_UNISON}},
        {FM_PARAM_AMP_RELEASE_MS, {5, 20, 200, 800}},
        {FM_PARAM_AMP_ATTACK_MS, {0, 2, 30, 300}},
        {FM_PARAM_FILTER_ENV_ATTACK_MS, {0, 10, 100, 1000}},
        /* the gate (#63): envelope 2 apart from the VCA, on and off mid-note */
        {FM_PARAM_AMP_GATE, {0, 0, 1, 1}},
        {FM_PARAM_FILTER_ENV_SOURCE, {0, 0, 1, 1}},
        {FM_PARAM_AMP_SUSTAIN, {0, 300, 1000, 1000}},
    };
    uint32_t k = busy_pick(sizeof p / sizeof p[0]);
    fm_engine_set_param(e, p[k].id, p[k].v[busy_pick(4)]);
}

/* Steps engine `e` once at random. */
static void busy_random_step(fm_engine *e, int16_t *buf)
{
    static const uint8_t notes[] = {48, 52, 55, 57, 60, 64};
    uint8_t note = notes[busy_pick(sizeof notes)];
    switch (busy_pick(8)) {
    case 0: case 1:
        fm_engine_note_on(e, note, (uint8_t)(1 + busy_pick(127)));
        break;
    case 2: case 3:
        fm_engine_note_off(e, note);
        break;
    case 4:
        fm_engine_controller(e, 64, busy_pick(2) ? 127 : 0);
        break;
    case 5:
        busy_random_param(e);
        break;
    default:
        fm_engine_render(e, buf, 1 + busy_pick(300));
        break;
    }
}

TT_TEST(busy_word_never_hides_a_busy_processor)
{
    uint32_t seed, step, i, bad = 0;
    for (seed = 1; seed <= 4; seed++) {
        busy_rng = seed;
        fm_engine_init(&beng, 44100u, (uint8_t)(1 + seed));
        for (step = 0; step < 3000; step++) {
            busy_random_step(&beng, bbuf);
            for (i = 0; i < beng.voice_count; i++) {
                const fm_voice *v = &beng.voices[i];
                if (!fm_voice_busy(v) && fm_voice_busy_derived(v, &beng.mod)) bad++;
            }
        }
    }
    TT_CHECK_EQ(bad, 0);
}

static void run_busy_tests(void)
{
    TT_RUN(busy_default_patch_is_at_rest);
    TT_RUN(busy_filter_env_amount_back_to_zero_rests);
    TT_RUN(busy_filter_back_to_a_wire_rests);
    TT_RUN(busy_mix_env_back_to_zero_rests);
    TT_RUN(busy_steal_fade_ends_at_rest);
    TT_RUN(busy_queued_release_ends_at_rest);
    TT_RUN(busy_filter_says_when_it_is_a_wire);
    TT_RUN(busy_word_never_hides_a_busy_processor);
}
