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

static void run_busy_tests(void)
{
    TT_RUN(busy_default_patch_is_at_rest);
}
