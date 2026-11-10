/*
 * smf.c - Standard MIDI File reader.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "smf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEMPO_EVENT 0xFF /* internal marker for a set-tempo meta event */
#define DEFAULT_TEMPO_US 500000u /* 120 BPM */

typedef struct {
    uint64_t tick;
    uint64_t order; /* track and position, keeps the sort stable */
    uint32_t tempo; /* for TEMPO_EVENT */
    uint8_t status, data1, data2;
} raw_event;

typedef struct {
    raw_event *ev;
    size_t count, cap;
    uint64_t end_tick;
} raw_list;

static int fail(char *err, size_t err_len, const char *msg)
{
    if (err && err_len) snprintf(err, err_len, "%s", msg);
    return -1;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
           (uint32_t)p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

/* Variable-length quantity, at most 4 bytes. Returns -1 if malformed. */
static int read_vlq(const uint8_t *p, size_t len, size_t *pos, uint32_t *out)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t b;
        if (*pos >= len) return -1;
        b = p[(*pos)++];
        v = (v << 7) | (b & 0x7Fu);
        if (!(b & 0x80)) {
            *out = v;
            return 0;
        }
    }
    return -1;
}

static int push(raw_list *l, raw_event e)
{
    if (l->count == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 256;
        raw_event *n = realloc(l->ev, cap * sizeof *n);
        if (!n) return -1;
        l->ev = n;
        l->cap = cap;
    }
    l->ev[l->count++] = e;
    return 0;
}

static int parse_track(const uint8_t *p, size_t len, uint64_t track,
                       raw_list *l, char *err, size_t err_len)
{
    size_t pos = 0;
    uint64_t tick = 0, n = 0;
    uint8_t running = 0;

    while (pos < len) {
        uint32_t delta;
        uint8_t status;
        raw_event e;

        if (read_vlq(p, len, &pos, &delta) != 0)
            return fail(err, err_len, "bad delta time");
        tick += delta;
        if (pos >= len) return fail(err, err_len, "truncated event");

        memset(&e, 0, sizeof e);
        e.tick = tick;
        e.order = (track << 40) | n++;

        status = p[pos];
        if (status == 0xFF) { /* meta event */
            uint8_t type;
            uint32_t mlen;
            if (pos + 1 >= len) return fail(err, err_len, "truncated meta");
            type = p[pos + 1];
            pos += 2;
            if (read_vlq(p, len, &pos, &mlen) != 0 || mlen > len - pos)
                return fail(err, err_len, "bad meta length");
            if (type == 0x51 && mlen == 3) {
                e.status = TEMPO_EVENT;
                e.tempo = (uint32_t)p[pos] << 16 | (uint32_t)p[pos + 1] << 8 |
                          p[pos + 2];
                if (e.tempo == 0) return fail(err, err_len, "zero tempo");
                if (push(l, e) != 0) return fail(err, err_len, "out of memory");
            }
            pos += mlen;
            if (type == 0x2F) break; /* end of track */
            continue;
        }
        if (status == 0xF0 || status == 0xF7) { /* sysex: skip */
            uint32_t slen;
            pos++;
            if (read_vlq(p, len, &pos, &slen) != 0 || slen > len - pos)
                return fail(err, err_len, "bad sysex length");
            pos += slen;
            running = 0;
            continue;
        }
        if (status & 0x80) {
            if (status > 0xEF) return fail(err, err_len, "unexpected status");
            running = status;
            pos++;
        } else if (!running) {
            return fail(err, err_len, "data byte without status");
        }
        e.status = running;
        {
            int two = (running & 0xF0) != 0xC0 && (running & 0xF0) != 0xD0;
            if (pos + (two ? 2u : 1u) > len)
                return fail(err, err_len, "truncated channel message");
            e.data1 = p[pos++] & 0x7F;
            if (two) e.data2 = p[pos++] & 0x7F;
        }
        if ((e.status & 0xF0) == 0x90 && e.data2 == 0)
            e.status = (uint8_t)(0x80 | (e.status & 0x0F));
        if (push(l, e) != 0) return fail(err, err_len, "out of memory");
    }
    if (tick > l->end_tick) l->end_tick = tick;
    return 0;
}

static int by_time(const void *a, const void *b)
{
    const raw_event *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    /* tempo changes at a tick apply before the notes at that tick */
    if ((x->status == TEMPO_EVENT) != (y->status == TEMPO_EVENT))
        return x->status == TEMPO_EVENT ? -1 : 1;
    return x->order < y->order ? -1 : (x->order > y->order);
}

int smf_parse(const uint8_t *data, size_t len, smf_song *song, char *err,
              size_t err_len)
{
    raw_list l = { NULL, 0, 0, 0 };
    uint16_t format, ntracks, division, t;
    size_t pos, i, out = 0;
    uint64_t base_tick = 0, base_us = 0;
    uint32_t tempo = DEFAULT_TEMPO_US;

    memset(song, 0, sizeof *song);
    if (len < 14 || memcmp(data, "MThd", 4) != 0 || be32(data + 4) < 6)
        return fail(err, err_len, "not a Standard MIDI File");
    format = be16(data + 8);
    ntracks = be16(data + 10);
    division = be16(data + 12);
    if (format > 1) return fail(err, err_len, "only SMF format 0 and 1");
    if (division & 0x8000)
        return fail(err, err_len, "SMPTE time division not supported");
    if (division == 0) return fail(err, err_len, "zero time division");

    pos = 8 + (size_t)be32(data + 4);
    for (t = 0; t < ntracks && pos + 8 <= len; ) {
        uint32_t clen = be32(data + pos + 4);
        if (clen > len - pos - 8) {
            free(l.ev);
            return fail(err, err_len, "truncated track chunk");
        }
        if (memcmp(data + pos, "MTrk", 4) == 0) {
            if (parse_track(data + pos + 8, clen, t, &l, err, err_len) != 0) {
                free(l.ev);
                return -1;
            }
            t++;
        }
        pos += 8 + (size_t)clen;
    }

    qsort(l.ev, l.count, sizeof *l.ev, by_time);
    song->events = malloc((l.count ? l.count : 1) * sizeof *song->events);
    if (!song->events) {
        free(l.ev);
        return fail(err, err_len, "out of memory");
    }
    for (i = 0; i < l.count; i++) {
        const raw_event *e = &l.ev[i];
        uint64_t us = base_us + (e->tick - base_tick) * tempo / division;
        if (e->status == TEMPO_EVENT) {
            base_us = us;
            base_tick = e->tick;
            tempo = e->tempo;
            continue;
        }
        song->events[out].time_us = us;
        song->events[out].status = e->status;
        song->events[out].data1 = e->data1;
        song->events[out].data2 = e->data2;
        out++;
    }
    song->count = out;
    song->end_us = base_us + (l.end_tick - base_tick) * tempo / division;
    free(l.ev);
    return 0;
}

void smf_free(smf_song *song)
{
    free(song->events);
    song->events = NULL;
    song->count = 0;
}
