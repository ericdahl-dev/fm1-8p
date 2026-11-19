/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p pattern storage (pattern_store.h): the record format (version 1).
 *
 *   [0]        length of the record in bytes (header and steps); 0 or 0xFF: an empty slot
 *   [1]        version, 1
 *   [2..13]    the name, STORE_NAME_LEN bytes, '\0'-padded
 *   [14]       LENGTH, 1..16
 *   [15..16]   TEMPO, tenths of a BPM, low byte first
 *   [17]       SWING, %
 *   [18]       GATE, %
 *   [19]       step count
 *   [20]       bytes per step: 13 here, at least 9 (the first records, before #42); newer firmware may
 *              make steps longer, and this one reads what it knows of each
 *   [21..]     steps: kind (seq_kind), note count, velocity, SEQ_NOTES notes; then (from 13 bytes, #42)
 *              the bend, int16 low byte first, the mod wheel and the move (0 JUMP, 1 GLIDE)
 *
 * The version changes only when a record can't be read this way; adding to the steps doesn't. Out-of-range
 * values load clamped, as seq.c's setters would have them. */
#include "pattern_store.h"

#define HEADER 21u
#define STEP_V1 9u                      /* kind, n, vel, SEQ_NOTES notes: the least a step has */
#define STEP 13u                        /* and bend (2), mod, move (#42) */
#define VERSION 1u

typedef char pat_step_is_v1s[STEP_V1 == 3u + SEQ_NOTES ? 1 : -1];
typedef char pat_record_fits_a_slot[HEADER + STEP * SEQ_STEPS <= STORE_SLOT_BYTES ? 1 : -1];

#define BANKS (PAT_SLOTS / STORE_BANK_SLOTS)
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

static int is_record(const uint8_t *rec, uint32_t n)
{
    return n >= HEADER && rec[1] == VERSION && rec[20] >= STEP_V1 && rec[0] <= n &&
           (uint32_t)rec[0] == HEADER + (uint32_t)rec[19] * rec[20];
}

uint32_t pat_encode(const seq_pattern *p, const char *name, uint8_t *rec)
{
    uint32_t i, k, n = HEADER;
    for (i = 0; i < STORE_NAME_LEN; i++)
        rec[2u + i] = 0;
    for (i = 0; name && i < STORE_NAME_LEN && name[i]; i++)
        rec[2u + i] = (uint8_t)name[i];
    rec[14] = p->length;
    rec[15] = (uint8_t)(p->tempo & 0xFFu);
    rec[16] = (uint8_t)(p->tempo >> 8);
    rec[17] = p->swing_pct;
    rec[18] = p->gate_pct;
    rec[19] = (uint8_t)SEQ_STEPS;
    rec[20] = (uint8_t)STEP;
    for (i = 0; i < SEQ_STEPS; i++) {
        const seq_step *st = &p->step[i];
        rec[n] = st->kind;
        rec[n + 1u] = st->n;
        rec[n + 2u] = st->vel;
        for (k = 0; k < SEQ_NOTES; k++)
            rec[n + 3u + k] = st->note[k];
        rec[n + 9u] = (uint8_t)((uint16_t)st->bend & 0xFFu);
        rec[n + 10u] = (uint8_t)((uint16_t)st->bend >> 8);
        rec[n + 11u] = st->mod;
        rec[n + 12u] = st->glide;
        n += STEP;
    }
    rec[0] = (uint8_t)n;
    rec[1] = VERSION;
    return n;
}

int pat_decode(const uint8_t *rec, uint32_t n, seq_pattern *p, char *name)
{
    uint32_t i, k, count, size, v;
    seq_pattern_empty(p);
    name[0] = '\0';
    if (!is_record(rec, n))
        return -1;
    for (i = 0; i < STORE_NAME_LEN; i++)
        name[i] = (char)rec[2u + i];
    name[STORE_NAME_LEN] = '\0';
    p->length = (uint8_t)CLAMP(rec[14], 1u, SEQ_STEPS);
    v = rec[15] | (uint32_t)rec[16] << 8;
    p->tempo = (uint16_t)CLAMP(v, SEQ_TEMPO_MIN, SEQ_TEMPO_MAX);
    p->swing_pct = (uint8_t)CLAMP(rec[17], 50u, 75u);
    p->gate_pct = (uint8_t)CLAMP(rec[18], SEQ_GATE_MIN, 100u);
    count = rec[19] < SEQ_STEPS ? rec[19] : SEQ_STEPS;
    size = rec[20];
    for (i = 0; i < count; i++) {
        const uint8_t *e = rec + HEADER + size * i;
        seq_step *st = &p->step[i];
        st->kind = e[0] == SEQ_NOTES_ON || e[0] == SEQ_TIE ? e[0] : (uint8_t)SEQ_REST;
        st->n = (uint8_t)(st->kind == SEQ_NOTES_ON ? CLAMP(e[1], 0u, SEQ_NOTES) : 0u);
        st->vel = (uint8_t)CLAMP(e[2], 1u, 127u);
        for (k = 0; k < SEQ_NOTES; k++)
            st->note[k] = (uint8_t)CLAMP(e[3u + k], 0u, 127u);
        if (st->kind == SEQ_NOTES_ON && !st->n)
            st->kind = SEQ_REST;                /* notes with none: a rest, as seq_set_step makes it */
        if (size >= STEP) {                     /* bend, mod and move (#42); before them: center, 0, JUMP */
            int32_t b = (int16_t)(uint16_t)(e[9] | (uint32_t)e[10] << 8);
            st->bend = (int16_t)CLAMP(b, -8192, 8191);
            st->mod = (uint8_t)CLAMP(e[11], 0u, 127u);
            st->glide = (uint8_t)(e[12] != 0);
        }
    }
    return 0;
}

void pat_lib_load_bank(pat_lib *l, const store_io *io, uint32_t b)
{
    uint32_t i;
    if (b >= BANKS)
        return;
    for (i = 0; i < STORE_BANK_BYTES; i++)      /* no bank, or a short one: empty slots */
        l->img[b][i] = 0;
    (void)io->load(io->ctx, PAT_BANK0 + b, l->img[b], STORE_BANK_BYTES);
}

void pat_lib_load(pat_lib *l, const store_io *io)
{
    uint32_t b;
    for (b = 0; b < BANKS; b++)
        pat_lib_load_bank(l, io, b);
}

static const uint8_t *slot(const pat_lib *l, uint32_t s)
{
    return l->img[s / STORE_BANK_SLOTS] + STORE_SLOT_BYTES * (s % STORE_BANK_SLOTS);
}

int pat_slot_read(const pat_lib *l, uint32_t s, seq_pattern *p, char *name)
{
    if (s >= PAT_SLOTS) {
        seq_pattern_empty(p);
        name[0] = '\0';
        return -1;
    }
    return pat_decode(slot(l, s), STORE_SLOT_BYTES, p, name);
}

int pat_slot_used(const pat_lib *l, uint32_t s)
{
    return s < PAT_SLOTS && is_record(slot(l, s), STORE_SLOT_BYTES);
}

/* Slot s becomes p's record, or (p NULL) empty: zeros, as a bank never saved reads; then its bank to
 * flash */
static int slot_set(pat_lib *l, const store_io *io, uint32_t s, const seq_pattern *p, const char *name)
{
    uint32_t b, i;
    uint8_t *rec, was[STORE_SLOT_BYTES];
    if (s >= PAT_SLOTS)
        return -1;
    b = s / STORE_BANK_SLOTS;
    rec = l->img[b] + STORE_SLOT_BYTES * (s % STORE_BANK_SLOTS);
    for (i = 0; i < STORE_SLOT_BYTES; i++)
        was[i] = rec[i];
    if (p)
        (void)pat_encode(p, name, rec);
    else
        for (i = 0; i < STORE_SLOT_BYTES; i++)
            rec[i] = 0;
    if (io->save(io->ctx, PAT_BANK0 + b, l->img[b], STORE_BANK_BYTES) == 0)
        return 0;
    for (i = 0; i < STORE_SLOT_BYTES; i++)      /* not written: RAM keeps what flash has */
        rec[i] = was[i];
    return -1;
}

int pat_slot_write(pat_lib *l, const store_io *io, uint32_t s, const seq_pattern *p, const char *name)
{
    return slot_set(l, io, s, p, name);
}

int pat_slot_clear(pat_lib *l, const store_io *io, uint32_t s)
{
    return slot_set(l, io, s, 0, 0);
}
