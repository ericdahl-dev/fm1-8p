/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Pattern identity (pattern_now.h, #324). */
#include "pattern_now.h"

static void pn_copy_name(char *to, const char *from)
{
    uint32_t i;
    for (i = 0; i < STORE_NAME_LEN && from[i]; i++)
        to[i] = from[i];
    for (; i <= STORE_NAME_LEN; i++)
        to[i] = '\0';
}

/* the queued pattern took over (seq adopted its ask, at a pass end or a stop): it is the running one */
static void pn_note(pattern_now *n)
{
    if (n->queued < 0 || seq_adopted(n->s) != n->queued_ask)
        return;
    n->running = (uint32_t)n->queued;
    pn_copy_name(n->running_name, n->queued_name);
    n->queued = -1;
    n->takeovers++;
}

static void pn_sync(pattern_now *n)
{
    pn_note(n);                                    /* (one seq took in already, in another of its calls) */
    (void)seq_main_tick(n->s);
    pn_note(n);
}

static void pn_read_slot(const pattern_now *n, uint32_t q, seq_pattern *pat, char *name)
{
    if (!n->lib || pat_slot_read(n->lib, q, pat, name) != 0) {
        seq_pattern_empty(pat);
        name[0] = '\0';
    }
}

static void pn_load(pattern_now *n, uint32_t q)
{
    static seq_pattern pat;                     /* (main loop only: kept off its stack) */
    char name[STORE_NAME_LEN + 1u];
    uint32_t ask;
    pn_read_slot(n, q, &pat, name);
    ask = seq_load_pattern(n->s, &pat);
    pn_note(n);                                    /* the one queued before took over just before */
    if (ask) {
        n->queued = (int32_t)q;
        pn_copy_name(n->queued_name, name);
        n->queued_ask = ask;
    } else {
        n->running = q;
        pn_copy_name(n->running_name, name);
        n->queued = -1;
    }
}

void pattern_init(pattern_now *n, seq *s)
{
    n->s = s;
    n->lib = 0;
    n->io = 0;
    n->running = 0;
    n->running_name[0] = n->queued_name[0] = '\0';
    n->queued = -1;
    n->queued_ask = 0;
    n->takeovers = 0;
}

void pattern_attach(pattern_now *n, pat_lib *lib, const store_io *io)
{
    n->lib = lib;
    n->io = io;
    pn_sync(n);
    pn_load(n, n->running);
}

uint32_t pattern_running(pattern_now *n, char *name)
{
    pn_sync(n);
    if (name)
        pn_copy_name(name, n->running_name);
    return n->running;
}

int32_t pattern_queued(pattern_now *n, char *name)
{
    pn_sync(n);
    if (name)
        pn_copy_name(name, n->queued >= 0 ? n->queued_name : "");
    return n->queued;
}

uint32_t pattern_edited(pattern_now *n)
{
    pn_sync(n);
    return seq_edited(n->s);
}

void pattern_queue(pattern_now *n, uint32_t q)
{
    if (q >= PAT_SLOTS)
        return;
    pn_sync(n);
    if (n->s->want && n->s->playing && q == n->running) {
        seq_cancel_next(n->s);                  /* back to the running one: nothing queued */
        pn_note(n);
        n->queued = -1;
        return;
    }
    pn_load(n, q);
}

uint32_t pattern_takeover(pattern_now *n)
{
    uint32_t t = n->takeovers;
    pn_sync(n);
    return n->takeovers != t;
}

uint32_t pattern_snapshot(pattern_now *n, seq_pattern *snap)
{
    pn_sync(n);
    *snap = n->s->edit;
    return n->takeovers;
}

int pattern_save(pattern_now *n, uint32_t dest, const seq_pattern *snap, const char *name, uint32_t token)
{
    uint32_t ask;
    if (!n->lib || !n->io || dest >= PAT_SLOTS || pat_slot_write(n->lib, n->io, dest, snap, name) != 0)
        return -1;
    pn_sync(n);
    if (n->queued >= 0 && dest == (uint32_t)n->queued) {   /* "slot B next": what takes over is this now */
        ask = seq_requeue(n->s, snap);
        pn_note(n);                                /* (or it took over just before) */
        if (ask) {
            pn_copy_name(n->queued_name, name);
            n->queued_ask = ask;
        }
    }
    if (token == n->takeovers)                  /* what was snapshotted plays: dest is its slot now */
        n->running = dest;
    if (dest == n->running) {                   /* edited now only by what differs from snap */
        pn_copy_name(n->running_name, name);
        n->s->stored = *snap;
    }
    return 0;
}

int pattern_delete(pattern_now *n, uint32_t dest)
{
    if (!n->lib || !n->io || dest >= PAT_SLOTS || pat_slot_clear(n->lib, n->io, dest) != 0)
        return -1;
    pn_sync(n);
    if (n->queued >= 0 && dest == (uint32_t)n->queued) {   /* nothing to take over: canceled */
        seq_cancel_next(n->s);
        pn_note(n);                                /* (or it took over just before) */
        n->queued = -1;
    }
    if (dest == n->running)                     /* the slot now loads empty: edited is against that */
        seq_pattern_empty(&n->s->stored);
    return 0;
}
