/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p patch storage: the record format (version 1).
 *
 *   [0]        length of the record in bytes (header and entries); 0 or 0xFF: an empty slot
 *   [1]        version, 1
 *   [2..13]    the name, STORE_NAME_LEN bytes, '\0'-padded
 *   [14]       entry count
 *   [15..]     entries of 3 bytes: a tag, then the value as int16, low byte first
 *
 * A tag names a param for good: TAG_PARAM below. A record from older firmware lacks the params added
 * since, which load at their engine default; one from newer firmware has tags this one doesn't know,
 * which are skipped. */
#include "patch_store.h"

#define HEADER 15u
#define ENTRY 3u
#define VERSION 1u

#include "patch_store_tags.h"   /* TAG_PARAM: generated from tools/params.py; tags are frozen */
#define TAGS (sizeof TAG_PARAM / sizeof TAG_PARAM[0])

typedef char store_every_record_fits_a_slot[HEADER + ENTRY * TAGS <= STORE_SLOT_BYTES ? 1 : -1];
typedef char store_tags_fit_a_byte[TAGS <= 255u ? 1 : -1];

/* 1 when rec (n bytes) is a record this firmware reads; 0, 0xFF and damaged ones are not */
static int is_record(const uint8_t *rec, uint32_t n)
{
    return n >= HEADER && rec[1] == VERSION && rec[0] <= n && rec[0] == HEADER + ENTRY * rec[14];
}

#define BANKS (STORE_USER_SLOTS / STORE_BANK_SLOTS)

void store_lib_load(store_lib *l, const store_io *io)
{
    uint32_t b;
    uint32_t i;
    for (b = 0; b < BANKS; b++) {
        for (i = 0; i < STORE_BANK_BYTES; i++)  /* no bank, or a short one: empty slots */
            l->img[b][i] = 0;
        (void)io->load(io->ctx, 1u + b, l->img[b], STORE_BANK_BYTES);
    }
}

int store_slot_read(const store_lib *l, uint32_t s, store_patch *p)
{
    return store_decode(l->img[s / STORE_BANK_SLOTS] + STORE_SLOT_BYTES * (s % STORE_BANK_SLOTS),
                        STORE_SLOT_BYTES, p);
}

int store_slot_used(const store_lib *l, uint32_t s)
{
    return is_record(l->img[s / STORE_BANK_SLOTS] + STORE_SLOT_BYTES * (s % STORE_BANK_SLOTS),
                     STORE_SLOT_BYTES);
}

int store_slot_write(store_lib *l, const store_io *io, uint32_t s, const store_patch *p)
{
    uint32_t b = s / STORE_BANK_SLOTS, i;
    uint8_t *rec = l->img[b] + STORE_SLOT_BYTES * (s % STORE_BANK_SLOTS);
    uint8_t was[STORE_SLOT_BYTES];
    for (i = 0; i < STORE_SLOT_BYTES; i++)
        was[i] = rec[i];
    (void)store_encode(p, rec);
    if (io->save(io->ctx, 1u + b, l->img[b], STORE_BANK_BYTES) == 0)
        return 0;
    for (i = 0; i < STORE_SLOT_BYTES; i++)      /* not written: RAM keeps what flash has */
        rec[i] = was[i];
    return -1;
}

uint16_t store_tag_param(uint32_t tag)
{
    return tag < TAGS ? TAG_PARAM[tag] : (uint16_t)FM_PARAM_COUNT;
}

uint32_t store_encode(const store_patch *p, uint8_t *rec)
{
    uint32_t i, n = HEADER;
    for (i = 0; i < STORE_NAME_LEN; i++)
        rec[2u + i] = 0;
    for (i = 0; i < STORE_NAME_LEN && p->name[i]; i++)
        rec[2u + i] = (uint8_t)p->name[i];
    for (i = 0; i < TAGS; i++) {
        int32_t v = p->v[TAG_PARAM[i]];
        rec[n] = (uint8_t)i;
        rec[n + 1u] = (uint8_t)((uint32_t)v & 0xFFu);
        rec[n + 2u] = (uint8_t)(((uint32_t)v >> 8) & 0xFFu);
        n += ENTRY;
    }
    rec[0] = (uint8_t)n;
    rec[1] = VERSION;
    rec[14] = (uint8_t)TAGS;
    return n;
}

int store_decode(const uint8_t *rec, uint32_t n, store_patch *p)
{
    uint32_t i, count;
    if (!is_record(rec, n)) return -1;
    count = rec[14];
    for (i = 0; i < FM_PARAM_COUNT; i++)       /* a param the record lacks: its default */
        p->v[i] = fm_param_info_get((uint16_t)i)->def;
    for (i = 0; i < STORE_NAME_LEN; i++)
        p->name[i] = (char)rec[2u + i];
    p->name[STORE_NAME_LEN] = '\0';
    for (i = 0; i < count; i++) {
        const uint8_t *e = rec + HEADER + ENTRY * i;
        int32_t v = (int16_t)(uint16_t)(e[1] | (uint32_t)e[2] << 8);
        const fm_param_info *in;
        if (e[0] >= TAGS) continue;             /* newer firmware's param */
        in = fm_param_info_get(TAG_PARAM[e[0]]);
        p->v[TAG_PARAM[e[0]]] = v < in->min ? in->min : v > in->max ? in->max : v;
    }
    return 0;
}
