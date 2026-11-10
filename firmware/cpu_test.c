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
    uint32_t i;
    if (fm_engine_init(&t->engine, CT_FS, voices) != FM_OK)
        return -1;
    t->voices = voices;
    t->held = 0;
    for (i = 0; i < voices; i++)
        fm_engine_note_on(&t->engine, CHORD[i], 100);
    return 0;
}

int cpu_test_init(cpu_test *t)
{
    uint32_t i;
    for (i = 0; i <= FM_MAX_VOICES; i++)
        t->worst[i] = 0;
    t->want = 1;
    t->asks = t->seen = 0;
    t->hold = 0;
    return start(t, 1);
}

void cpu_test_request_voices(cpu_test *t, int32_t n)
{
    t->want = (uint8_t)(n < 1 ? 1 : n > FM_MAX_VOICES ? FM_MAX_VOICES : n);
    t->asks = t->asks + 1u;                     /* after the count: the ISR never sees a new ask with an old count */
}

uint8_t cpu_test_voices(const cpu_test *t) { return t->voices; }

void cpu_test_render(cpu_test *t, int32_t *out, uint32_t n)
{
    uint32_t i;
    uint32_t asks = t->asks;                    /* one read: the main loop may write it any time */
    if (n > CT_BLOCK)
        n = CT_BLOCK;
    if (asks != t->seen) {                      /* a new request from the main loop wins */
        t->seen = asks;
        (void)start(t, t->want);                /* want is 1..FM_MAX_VOICES: cannot fail */
    } else if (t->hold && t->held >= t->hold && t->voices < FM_MAX_VOICES) {
        (void)start(t, (uint8_t)(t->voices + 1u));
    }
    t->held++;
    fm_engine_render(&t->engine, t->buf, n);
    for (i = 0; i < 2u * n; i++)
        out[i] += t->buf[i];                    /* int16 is already Q15 */
}

void cpu_test_record(cpu_test *t, uint32_t ticks)
{
    if (ticks > t->worst[t->voices])
        t->worst[t->voices] = ticks;
}

uint32_t cpu_test_worst_permille(const cpu_test *t, uint32_t voices)
{
    if (voices < 1u || voices > FM_MAX_VOICES)
        return 0;
    return (uint32_t)(((uint64_t)t->worst[voices] * 1000u) / CT_BLOCK_TICKS);  /* main loop: 64/32 */
}

void cpu_test_sweep(cpu_test *t, uint32_t hold) { t->hold = hold; }

int cpu_test_finished(const cpu_test *t)
{
    return t->hold != 0u && t->voices == FM_MAX_VOICES && t->held >= t->hold;
}
