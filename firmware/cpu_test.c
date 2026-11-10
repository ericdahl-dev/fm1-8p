/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cpu_test.h"

static const uint8_t CHORD[FM_MAX_VOICES] = {48, 52, 55, 59, 62, 66, 69, 72};   /* C3 up in thirds */

uint32_t cpu_test_chord(uint32_t k, uint8_t *notes)
{
    uint32_t i;
    if (k > FM_MAX_VOICES)
        k = FM_MAX_VOICES;
    for (i = 0; i < k; i++)
        notes[i] = CHORD[i];
    return k;
}

static int start(cpu_test *t, uint8_t voices)
{
    uint8_t notes[FM_MAX_VOICES];
    uint32_t i, k;
    uint8_t engine_voices = voices == CT_BURST ? FM_MAX_VOICES : voices;
    if (fm_engine_init(&t->engine, CT_FS, engine_voices) != FM_OK)
        return -1;
    t->held = 0;
    t->worst[voices] = 0;                       /* a count started again measures afresh */
    t->voices = voices;
    if (voices == CT_BURST) {
        (void)fm_engine_set_param(&t->engine, FM_PARAM_VOICE_MODE, FM_VOICE_MODE_UNISON);
        fm_engine_note_on(&t->engine, CHORD[0], 100);
        return 0;
    }
    k = cpu_test_chord(voices, notes);
    for (i = 0; i < k; i++)
        fm_engine_note_on(&t->engine, notes[i], 100);
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
    for (i = 0; i <= CT_BURST; i++)
        t->worst[i] = 0;
    t->want = 1;
    t->asks = t->seen = 0;
    t->hold = 0;
    t->done_seq = t->taken = 0;
    t->fresh = 0;
    return start(t, 1);                         /* here, outside the ISR: the first block is clean */
}

/* A sweep's last step (U) has run its hold: its row is final. */
static int over(const cpu_test *t)
{
    return t->hold != 0u && t->held > t->hold;
}

void cpu_test_request_voices(cpu_test *t, int32_t n)
{
    t->want = (uint8_t)(n < 1 ? 1 : n > (int32_t)CT_BURST ? (int32_t)CT_BURST : n); /* 1..8, then U */
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
        (void)start(t, t->want);                /* want is 1..CT_BURST: cannot fail */
        t->fresh = 1;                           /* this block's time includes setup */
    } else if (t->hold && t->held >= t->hold && t->voices < CT_BURST) {
        (void)start(t, (uint8_t)(t->voices + 1u));
        t->fresh = 1;
    }
    t->held++;
    if (t->voices == CT_BURST && !t->fresh && !over(t))
        burst(t);                               /* after a sweep's hold the last chord just rings */
    fm_engine_render(&t->engine, t->buf, n);
    for (i = 0; i < 2u * n; i++)
        out[i] += t->buf[i];                    /* int16 is already Q15 */
}

void cpu_test_record(cpu_test *t, uint32_t ticks)
{
    uint8_t v = t->voices;
    if (!t->fresh && !over(t) && ticks > t->worst[v])
        t->worst[v] = ticks;
    if (t->hold != 0u && t->held == t->hold) {  /* this count's hold is complete (once: held only grows) */
        t->done[t->done_seq % CT_BURST] = v;
        t->done_seq = t->done_seq + 1u;         /* after the entry: the main loop never reads it early */
    }
}

int cpu_test_take_completed(cpu_test *t, uint32_t *voices, uint32_t *permille)
{
    if (t->taken == t->done_seq)
        return 0;
    *voices = t->done[t->taken % CT_BURST];
    *permille = cpu_test_worst_permille(t, *voices);
    t->taken++;
    return 1;
}

uint32_t cpu_test_worst_permille(const cpu_test *t, uint32_t voices)
{
    if (voices < 1u || voices > CT_BURST)
        return 0;
    return (uint32_t)(((uint64_t)t->worst[voices] * 1000u) / CT_BLOCK_TICKS);  /* main loop: 64/32 */
}

void cpu_test_sweep(cpu_test *t, uint32_t hold) { t->hold = hold; }

