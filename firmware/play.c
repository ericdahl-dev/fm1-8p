/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "play.h"

/* A compiler barrier: the stores (or loads) before it are done before those after it. The main loop
 * and the audio interrupt share one core, so this is all the queue needs: an entry is written before
 * the head that publishes it, and read after the head that says it is there. */
#define BARRIER() __asm__ volatile("" ::: "memory")

static void load_stored(play *p);

/* the program's name, at most STORE_NAME_LEN chars */
static void set_name(play *p, const char *s)
{
    uint32_t i;
    for (i = 0; i < STORE_NAME_LEN && s[i]; i++)
        p->name[i] = s[i];
    for (; i <= STORE_NAME_LEN; i++)
        p->name[i] = 0;
}

const patch *play_patch(const play *p) { return p->patch; }

uint32_t play_prog(const play *p) { return p->prog; }

void play_prog_name(const play *p, char *name)
{
    uint32_t i;
    for (i = 0; i <= STORE_NAME_LEN; i++)
        name[i] = p->name[i];
}

int play_load_prog(play *p, uint32_t q)
{
    static store_patch s;                       /* main loop only */
    uint32_t f;
    if (q >= PLAY_PROGS)
        return -1;
    if (q >= PLAY_PROG_P) {
        play_load_patch(p, q - PLAY_PROG_P);
        return 0;
    }
    if (!p->lib || store_slot_read(p->lib, q, &s) != 0)
        return -1;
    p->loading = 1;
    BARRIER();
    for (f = 0; f < FM_PARAM_COUNT; f++)
        p->stored[f] = s.v[f];
    load_stored(p);
    p->prog = q;
    set_name(p, s.name);
    play_load_end(p);
    return 0;
}

uint32_t play_prog_step(const play *p, uint32_t from, int32_t clicks)
{
    int32_t way = clicks > 0 ? 1 : -1;
    uint32_t q = from, n;
    for (; clicks; clicks -= way)
        for (n = 0; n < PLAY_PROGS; n++) {      /* the next program that holds a patch */
            q = (q + (way > 0 ? 1u : PLAY_PROGS - 1u)) % PLAY_PROGS;
            if (q >= PLAY_PROG_P || (p->lib && store_slot_used(p->lib, q)))
                break;
        }
    return q;
}

int play_init(play *p)
{
    uint32_t i;
    p->head = p->tail = 0;
    p->lost = p->lost_seen = 0;
    p->last_note = -1;
    p->oct = 0;
    p->loading = p->loads = p->loads_seen = 0;
    p->lib = 0;
    if (fm_engine_init(&p->engine, PLAY_FS, PLAY_VOICES) != FM_OK)
        return -1;
    p->patch = &PATCHES[PATCH_SOUNDTRACK];
    p->prog = PLAY_PROG_P + PATCH_SOUNDTRACK;
    set_name(p, p->patch->name);
    patch_values(p->patch, p->stored);
    for (i = 0; i < FM_PARAM_COUNT; i++) {
        if (fm_engine_set_param(&p->engine, (uint16_t)i, p->stored[i]) != FM_OK)
            return -1;
        p->target[i] = p->stored[i];
    }
    for (i = 0; i < PLAY_PARAM_WORDS; i++)
        p->asked[i] = 0;
    return 0;
}

/* Main loop: one event into the queue. When it is full the event is dropped and counted: the next
 * render then lets the pedal up, lets every note go and centers the bend, so a lost pedal-up, note
 * off or bend center never leaves the synth stuck. */
static void put(play *p, uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t h = p->head;
    if (d1 > 127u || d2 > 127u)                 /* not MIDI data */
        return;
    if (h - p->tail >= PLAY_QUEUE) {
        p->lost = p->lost + 1u;
        return;
    }
    p->queue[h % PLAY_QUEUE] = st << 16 | d1 << 8 | d2;
    BARRIER();
    p->head = h + 1u;                           /* after the entry: the ISR never reads it early */
    if (st == 0x90u && d2 != 0u)                /* queued: it will sound */
        p->last_note = (int32_t)d1;
}

/* Audio interrupt: one event into the engine. */
static void apply(play *p, uint32_t ev)
{
    uint8_t d1 = (uint8_t)(ev >> 8 & 0x7Fu), d2 = (uint8_t)(ev & 0x7Fu);
    switch (ev >> 16) {
    case 0x90u: fm_engine_note_on(&p->engine, d1, d2); break;
    case 0x80u: fm_engine_note_off(&p->engine, d1); break;
    case 0xB0u: fm_engine_controller(&p->engine, d1, d2); break;
    case 0xE0u: fm_engine_pitch_bend(&p->engine, (uint16_t)(d2 << 7 | d1)); break;
    default: break;
    }
}

void play_note(play *p, uint32_t note, uint32_t vel)
{
    put(p, 0x90u, note, vel);
}

void play_midi(play *p, uint32_t st, uint32_t d1, uint32_t d2)
{
    switch (st & 0xF0u) {
    case 0x90u:
    case 0x80u:
    case 0xB0u:
    case 0xE0u: put(p, st & 0xF0u, d1, d2); break;
    default: break;
    }
}

void play_note_name(int32_t note, char *s)
{
    static const char NAME[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char *p;
    int32_t octave;
    if (note < 0 || note > 127) {
        s[0] = '-', s[1] = '-', s[2] = 0;
        return;
    }
    for (p = NAME[note % 12]; *p; p++)
        *s++ = *p;
    octave = note / 12 - 1;
    if (octave < 0)
        *s++ = '-', octave = -octave;
    *s++ = (char)('0' + octave);
    *s = 0;
}

void play_set_param(play *p, uint16_t id, int32_t value)
{
    const fm_param_info *in = fm_param_info_get(id);
    if (!in)
        return;
    p->target[id] = value < in->min ? in->min : value > in->max ? in->max : value;
    BARRIER();                                  /* the value before its bit: the ISR never sees an old one */
    p->asked[id >> 5] = p->asked[id >> 5] | 1u << (id & 31u);
}

int32_t play_get_param(const play *p, uint16_t id)
{
    return id < FM_PARAM_COUNT ? p->target[id] : 0;
}

/* Audio interrupt: the params the main loop set, into the engine: only those whose bit is set. (The
 * main loop's |= can put back a bit cleared here mid-way: that param is set again, the same value.) */
static void apply_params(play *p)
{
    uint32_t w, b;
    for (w = 0; w < PLAY_PARAM_WORDS; w++) {
        uint32_t bits = p->asked[w];
        p->asked[w] = 0;
        BARRIER();                              /* the values are read after their bits */
        for (b = w * 32u; bits; bits >>= 1, b++)
            if (bits & 1u)
                (void)fm_engine_set_param(&p->engine, (uint16_t)b, p->target[b]);
    }
}

/* A load in two steps, so a render between them applies none of it: begin writes the values while
 * `loading` keeps the ISR off the params; end publishes them, and the ISR's next block applies them
 * all, with the all notes off, before that block's note events. */
/* begin: p->stored holds the new patch; its changes into target, with `loading` keeping the ISR off */
static void load_stored(play *p)
{
    uint32_t f;
    for (f = 0; f < FM_PARAM_COUNT; f++)
        if (p->stored[f] != p->target[f]) {     /* only what changes: each set can retime an envelope */
            p->target[f] = p->stored[f];
            p->asked[f >> 5] = p->asked[f >> 5] | 1u << (f & 31u);
        }
}

void play_load_begin(play *p, uint32_t i)
{
    const patch *t = &PATCHES[i % PATCH_COUNT];
    p->loading = 1;
    BARRIER();                                  /* before any value: the ISR leaves the params alone */
    patch_values(t, p->stored);                 /* the stored patch is now this one */
    load_stored(p);
    p->patch = t;
    p->prog = PLAY_PROG_P + i % PATCH_COUNT;
    set_name(p, t->name);
}

void play_load_end(play *p)
{
    BARRIER();                                  /* the values before the load that publishes them */
    p->loads = p->loads + 1u;
    BARRIER();
    p->loading = 0;
}

void play_load_patch(play *p, uint32_t i)
{
    play_load_begin(p, i);
    play_load_end(p);
}

int32_t play_last_note(const play *p) { return p->last_note; }

void play_octave(play *p, int32_t step)
{
    int32_t o = p->oct + step;
    p->oct = o < -2 ? -2 : o > 2 ? 2 : o;
}

void play_key_down(play *p, uint32_t k, uint32_t vel)
{
    if (k >= PLAY_KEYS)
        return;
    p->held[k] = (uint8_t)((int32_t)PLAY_KEY0 + 12 * p->oct + (int32_t)k);
    play_note(p, p->held[k], vel ? vel : 100u);
}

void play_key_up(play *p, uint32_t k)
{
    if (k < PLAY_KEYS)
        play_note(p, p->held[k], 0);
}

uint32_t play_oct_led(const play *p, uint32_t button)
{
    return button == PLAY_OCT_DOWN ? p->oct < 0 : p->oct > 0;
}

void play_render(play *p, int32_t *out, uint32_t n)
{
    uint32_t i;
    uint32_t h = p->head, t = p->tail;          /* one read: the main loop may write it any time */
    if (n > PLAY_BLOCK)
        n = PLAY_BLOCK;
    if (!p->loading) {                          /* not part way through a load: its values are whole */
        if (p->loads != p->loads_seen) {        /* a load: all notes off with its params, one block */
            p->loads_seen = p->loads;
            fm_engine_controller(&p->engine, 123, 0);   /* they release, on the new patch's times */
        }
        apply_params(p);                        /* before the notes: a note played after a load sounds it */
    }
    BARRIER();                                  /* the entries are read after the head */
    for (; t != h; t++)
        apply(p, p->queue[t % PLAY_QUEUE]);
    BARRIER();                                  /* and done with before the tail frees them */
    p->tail = t;
    if (p->lost != p->lost_seen) {              /* some events were dropped: back to a safe state */
        p->lost_seen = p->lost;
        fm_engine_controller(&p->engine, 64, 0);        /* a lost pedal-up would hold every note */
        fm_engine_controller(&p->engine, 123, 0);       /* a lost note off would hang one */
        fm_engine_pitch_bend(&p->engine, 8192);         /* a lost center would leave it out of tune */
    }
    fm_engine_render(&p->engine, p->buf, n);
    for (i = 0; i < 2u * n; i++) {
        int32_t v = (int32_t)p->buf[i] * (1 << PLAY_GAIN_SHIFT);
        out[i] += v > 32767 ? 32767 : v < -32767 ? -32767 : v;     /* saturate, never wrap */
    }
}

uint32_t play_edited(const play *p)
{
    uint32_t i;
    for (i = 0; i < FM_PARAM_COUNT; i++)
        if (p->target[i] != p->stored[i])
            return 1;
    return 0;
}
