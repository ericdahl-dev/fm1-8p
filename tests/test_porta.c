/*
 * Portamento (#62), as PG-8X 2.0.13's (measured 2026-11-18, three passes): a glide is exponential in pitch,
 * the distance left falling by 1/e every tau, the same tau for any interval, up or down; each voice glides
 * from the last note played (also one let go). portamento_ms is that tau; 0 is off. Through the public
 * interface: the voices' sounding pitch (Q16.16 semitones) as the engine sets it.
 */
#include "../engine/fm1_8p.h"

#define PT_RATE 48000u

static fm_engine pt_eng;
static int16_t pt_buf[PT_RATE / 10 * 2];

/* render `ms` of audio (in 1 ms blocks) */
static void pt_run(double ms)
{
    uint32_t n = (uint32_t)(ms * PT_RATE / 1000.0), k;
    while (n) {
        k = n < PT_RATE / 1000u ? n : PT_RATE / 1000u;
        fm_engine_render(&pt_eng, pt_buf, k);
        n -= k;
    }
}

/* the sounding pitch of the voice playing `note`, semitones (-1000 if none) */
static double pt_pitch(uint8_t note)
{
    uint8_t i;
    for (i = 0; i < pt_eng.voice_count; i++)
        if (fm_voice_active(&pt_eng.voices[i]) && fm_voice_note(&pt_eng.voices[i]) == note)
            return pt_eng.voices[i].pitch / 65536.0;
    return -1000.0;
}

/* tau 50 ms, C3 then C4: the octave left falls as exp(-t / 50 ms), within a control tick's worth */
TT_TEST(porta_glides_exponentially_with_its_tau)
{
    double t, left, want;
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, 50), FM_OK);
    fm_engine_note_on(&pt_eng, 48, 100);
    pt_run(100);
    fm_engine_note_off(&pt_eng, 48);
    fm_engine_note_on(&pt_eng, 60, 100);
    for (t = 10; t <= 200; t += 10) {
        pt_run(10);
        left = 60.0 - pt_pitch(60);
        want = 12.0 * exp(-t / 50.0);
        if (fabs(left - want) > 0.02 + want * 0.02)
            printf("    at %.0f ms: %.3f st left, want %.3f\n", t, left, want);
        TT_CHECK(fabs(left - want) <= 0.02 + want * 0.02);
    }
}

/* a glide down ends on its note exactly, and stops (no voice left moving), at a short tau and a long one */
static void pt_down_lands(int32_t tau_ms)
{
    TT_CHECK_EQ(fm_engine_init(&pt_eng, PT_RATE, 6), FM_OK);
    TT_CHECK_EQ(fm_engine_set_param(&pt_eng, FM_PARAM_PORTAMENTO_MS, tau_ms), FM_OK);
    fm_engine_note_on(&pt_eng, 72, 100);
    pt_run(20);
    fm_engine_note_off(&pt_eng, 72);
    fm_engine_note_on(&pt_eng, 60, 100);
    pt_run(tau_ms * 14.0);                      /* 12 octave-tau's to under 1/1000 st, and some */
    if (pt_pitch(60) != 60.0)
        printf("    tau %d ms: at %.6f st after 14 tau\n", (int)tau_ms, pt_pitch(60));
    TT_CHECK(pt_pitch(60) == 60.0);
    TT_CHECK_EQ(pt_eng.porta_moving, 0);
}
TT_TEST(porta_down_lands_on_its_note)
{
    pt_down_lands(50);
    pt_down_lands(2000);
}

static void run_porta_tests(void)
{
    TT_RUN(porta_glides_exponentially_with_its_tau);
    TT_RUN(porta_down_lands_on_its_note);
}
