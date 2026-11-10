/* Chorus tests, through the chorus module API and the engine API. */
#include "../engine/chorus.h"
#include "../engine/fm1_8p.h"

#define CH_RATE 44100u

static fm_engine ch_eng;
static int16_t ch_buf[CH_RATE * 2 * 4];

/* FNV-1a over a buffer of samples. */
static uint32_t ch_hash(const int16_t *s, size_t count)
{
    const unsigned char *p = (const unsigned char *)s;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < count * sizeof *s; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/*
 * Two overlapping notes and a release, 20000 frames. The golden hash was
 * taken from the engine before the chorus existed (main at 47680c8, with polyphony).
 */
#define CH_GOLDEN_HASH 0x8E24F4ADu

static uint32_t ch_render_golden_phrase(int set_mode, int32_t mode)
{
    memset(ch_buf, 0, 2 * 20000 * sizeof ch_buf[0]);
    fm_engine_init(&ch_eng, CH_RATE, 8);
    if (set_mode) fm_engine_set_param(&ch_eng, FM_PARAM_CHORUS_MODE, mode);
    fm_engine_set_param(&ch_eng, FM_PARAM_AMP_ATTACK_MS, 3);
    fm_engine_set_param(&ch_eng, FM_PARAM_AMP_RELEASE_MS, 40);
    fm_engine_note_on(&ch_eng, 57, 100);
    fm_engine_render(&ch_eng, ch_buf, 8000);
    fm_engine_note_on(&ch_eng, 64, 90);
    fm_engine_render(&ch_eng, ch_buf + 2 * 8000, 6000);
    fm_engine_note_off(&ch_eng, 64);
    fm_engine_render(&ch_eng, ch_buf + 2 * 14000, 6000);
    return ch_hash(ch_buf, 2 * 20000);
}

TT_TEST(chorus_off_engine_output_is_bit_exact_with_pre_chorus_engine)
{
    TT_CHECK_EQ(ch_render_golden_phrase(0, 0), CH_GOLDEN_HASH);
    TT_CHECK_EQ(fm_engine_get_param(&ch_eng, FM_PARAM_CHORUS_MODE), 0);
    TT_CHECK_EQ(fm_param_info_get(FM_PARAM_CHORUS_MODE)->def, 0);
    TT_CHECK_EQ(ch_render_golden_phrase(1, FM_CHORUS_OFF), CH_GOLDEN_HASH);
    /* back to off after a mode was on */
    fm_engine_set_param(&ch_eng, FM_PARAM_CHORUS_MODE, 2);
    fm_engine_set_param(&ch_eng, FM_PARAM_CHORUS_MODE, 0);
    TT_CHECK_EQ(fm_engine_get_param(&ch_eng, FM_PARAM_CHORUS_MODE), 0);
}

static void run_chorus_tests(void)
{
    TT_RUN(chorus_off_engine_output_is_bit_exact_with_pre_chorus_engine);
}
