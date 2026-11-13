/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cpu_test.h"

static const uint8_t CHORD[FM_MAX_VOICES] = {48, 52, 55, 59, 62, 66, 69, 72};   /* C3 up in thirds */

/* Step F's velocities, one per chord note: spread over the curve, none at full (127 skips the curve). */
static const uint8_t FULL_VEL[FM_MAX_VOICES] = {112, 64, 96, 40, 120, 80, 104, 56};

uint8_t cpu_test_full_velocity(uint32_t i) { return FULL_VEL[i % FM_MAX_VOICES]; }

/* Step F's sound: both oscillators, vibrato, pulse-width modulation, amp velocity, the filter, the chorus,
 * and the onset thump every JX-8P tone has (#146): each voice's first note after the step's init. */
static const cpu_test_param FULL_PATCH[] = {
    {FM_PARAM_OSC1_WAVE, FM_WAVE_PULSE},
    {FM_PARAM_OSC2_WAVE, FM_WAVE_SAW},
    {FM_PARAM_OSC2_LEVEL, 500},
    {FM_PARAM_CHORUS_MODE, FM_CHORUS_DEEP},
    {FM_PARAM_LFO_PITCH_CENTS, 30},
    {FM_PARAM_LFO_PW_PCT, 25},
    {FM_PARAM_AMP_VELOCITY, 100},
    /* the filter never on fm_filter_run's wide-open fast path: a resonant lowpass whose envelope sweeps
     * it from 10500 up to 14100 cents, into the fade at the top of its range (the costlier general path),
     * and the first highpass step */
    {FM_PARAM_FILTER_CUTOFF_CENTS, 10500},
    {FM_PARAM_FILTER_RESONANCE, 700},
    {FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 3600},
    {FM_PARAM_HIGHPASS_STEP, 1},
    {FM_PARAM_ONSET_THUMP, 1},
};

void cpu_test_set_full_patch(cpu_test *t, const cpu_test_param *p, uint32_t n)
{
    t->patch = p ? p : FULL_PATCH;
    t->patch_n = p ? n : (uint32_t)(sizeof FULL_PATCH / sizeof FULL_PATCH[0]);
}

uint32_t cpu_test_chord(uint32_t k, uint8_t *notes)
{
    uint32_t i;
    if (k > FM_MAX_VOICES)
        k = FM_MAX_VOICES;
    for (i = 0; i < k; i++)
        notes[i] = CHORD[i];
    return k;
}

/* What a count plays. */
typedef enum {
    PATCH_DEFAULT,      /* the default patch, one note per voice at velocity 100 */
    PATCH_UNISON,       /* U: all voices in unison on one note (burst() adds the events) */
    PATCH_FULL          /* the full patch, one note per voice at FULL_VEL */
} patch_kind;

static patch_kind patch_of(const cpu_test *t, uint8_t voices)
{
    if (voices == CT_BURST)
        return PATCH_UNISON;
    if (voices == CT_FULL || t->chord_full)
        return PATCH_FULL;
    return PATCH_DEFAULT;
}

/* Starts count `voices` playing `patch`. On a param the engine refuses it marks the count failed
 * (cpu_test_failed) and returns -1; the engine keeps playing what was set before it. */
static int start(cpu_test *t, uint8_t voices, patch_kind patch)
{
    uint8_t notes[FM_MAX_VOICES];
    uint32_t i, k;
    fm_status rc = FM_OK;
    uint8_t engine_voices = voices == CT_FULL ? CT_FULL_VOICES
                            : voices > FM_MAX_VOICES ? FM_MAX_VOICES : voices;
    t->held = 0;
    t->worst[voices] = 0;                       /* a count started again measures afresh */
    t->voices = voices;
    if (fm_engine_init(&t->engine, CT_FS, engine_voices) != FM_OK)
        rc = FM_ERR_ARG;
    else if (patch == PATCH_UNISON)
        rc = fm_engine_set_param(&t->engine, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
    else if (patch == PATCH_FULL)
        for (i = 0; i < t->patch_n && rc == FM_OK; i++)
            rc = fm_engine_set_param(&t->engine, t->patch[i].id, t->patch[i].value);
    if (rc != FM_OK) {
        t->failed = t->failed | (1u << voices);
        return -1;
    }
    t->failed = t->failed & ~(1u << voices);
    k = cpu_test_chord(patch == PATCH_UNISON ? 1u : engine_voices, notes);
    for (i = 0; i < k; i++)
        fm_engine_note_on(&t->engine, notes[i], patch == PATCH_FULL ? FULL_VEL[i] : 100);
    return 0;
}

/* One burst block's events: the last block's keys up, the chord down, a new detune. */
static void burst(cpu_test *t)
{
    uint32_t i;
    for (i = 0; i < FM_MAX_VOICES; i++)
        fm_engine_note_off(&t->engine, CHORD[i]);
    for (i = 0; i < FM_MAX_VOICES; i++)
        fm_engine_note_on(&t->engine, CHORD[i], 100);
    (void)fm_engine_set_param(&t->engine, FM_PARAM_UNISON_DETUNE_CENTS, (t->held & 1u) ? 30 : 10);
}

int cpu_test_init(cpu_test *t)
{
    uint32_t i;
    for (i = 0; i <= CT_LAST; i++)
        t->worst[i] = 0;
    t->want = 1;
    t->asks = t->seen = 0;
    t->hold = 0;
    t->done_seq = t->taken = 0;
    t->fresh = 0;
    t->chord_full = 0;
    t->failed = 0;
    cpu_test_set_full_patch(t, NULL, 0);
    return start(t, 1, PATCH_DEFAULT);                         /* here, outside the ISR: the first block is clean */
}

/* A sweep's last step (CT_LAST) has run its hold: its row is final. */
static int over(const cpu_test *t)
{
    return t->hold != 0u && t->held > t->hold;
}

void cpu_test_request_voices(cpu_test *t, int32_t n)
{
    t->want = (uint8_t)(n < 1 ? 1 : n > (int32_t)CT_LAST ? (int32_t)CT_LAST : n); /* 1..8, U, F */
    t->asks = t->asks + 1u;                     /* after the count: the ISR never sees a new ask with an old count */
}

uint8_t cpu_test_voices(const cpu_test *t) { return t->voices; }

void cpu_test_render(cpu_test *t, int32_t *out, uint32_t n)
{
    uint32_t i;
    uint32_t asks = t->asks;                    /* one read: the main loop may write it any time */
    if (n > CT_BLOCK)
        n = CT_BLOCK;
    t->fresh = 0;
    if (asks != t->seen) {                      /* a new request from the main loop wins */
        t->seen = asks;
        (void)start(t, t->want, patch_of(t, t->want));     /* a failure marks the count: cpu_test_failed */
        t->fresh = 1;                           /* this block's time includes setup */
    } else if (t->hold && t->held >= t->hold && t->voices < CT_LAST) {
        uint8_t next = (uint8_t)(t->voices + 1u);
        (void)start(t, next, patch_of(t, next));
        t->fresh = 1;
    }
    t->held++;
    if (t->voices == CT_BURST && !t->fresh)
        burst(t);                               /* U is never the last step: no hold to stop it */
    fm_engine_render(&t->engine, t->buf, n);
    for (i = 0; i < 2u * n; i++)
        out[i] += t->buf[i];                    /* int16 is already Q15 */
}

void cpu_test_record(cpu_test *t, uint32_t ticks)
{
    uint8_t v = t->voices;
    if (!t->fresh && !over(t) && !(t->failed & (1u << v)) && ticks > t->worst[v])
        t->worst[v] = ticks;
    if (t->hold != 0u && t->held == t->hold) {  /* this count's hold is complete (once: held only grows) */
        t->done[t->done_seq % CT_LAST] = v;
        t->done_seq = t->done_seq + 1u;         /* after the entry: the main loop never reads it early */
    }
}

int cpu_test_take_completed(cpu_test *t, uint32_t *voices, uint32_t *permille)
{
    if (t->taken == t->done_seq)
        return 0;
    *voices = t->done[t->taken % CT_LAST];
    *permille = cpu_test_worst_permille(t, *voices);
    t->taken++;
    return 1;
}

uint32_t cpu_test_worst_permille(const cpu_test *t, uint32_t voices)
{
    if (voices < 1u || voices > CT_LAST)
        return 0;
    return (uint32_t)(((uint64_t)t->worst[voices] * 1000u) / CT_BLOCK_TICKS);  /* main loop: 64/32 */
}

void cpu_test_sweep(cpu_test *t, uint32_t hold) { t->hold = hold; }

void cpu_test_use_full_patch(cpu_test *t, int on) { t->chord_full = (uint8_t)(on != 0); }

int cpu_test_failed(const cpu_test *t, uint32_t voices)
{
    return voices >= 1u && voices <= CT_LAST && (t->failed & (1u << voices)) != 0;
}

