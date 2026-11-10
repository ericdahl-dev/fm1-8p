/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "play.h"

/* A compiler barrier: the stores (or loads) before it are done before those after it. The main loop
 * and the audio interrupt share one core, so this is all the queue needs: an entry is written before
 * the head that publishes it, and read after the head that says it is there. */
#define BARRIER() __asm__ volatile("" ::: "memory")

const patch *play_patch(const play *p) { return p->patch; }

/* Every field of t the engine has a param for; the others wait for their features (patches.h). */
static int use_patch(play *p, const patch *t)
{
    uint32_t f;
    for (f = 0; f < PATCH_FIELD_COUNT; f++) {
        int32_t id = patch_param((patch_field)f);
        if (id == PATCH_NONE)
            continue;
        if (fm_engine_set_param(&p->engine, (uint16_t)id, patch_value(t, (patch_field)f)) != FM_OK)
            return -1;
    }
    p->patch = t;
    return 0;
}

int play_init(play *p)
{
    p->head = p->tail = 0;
    p->lost = p->lost_seen = 0;
    p->last_note = -1;
    p->oct = 0;
    if (fm_engine_init(&p->engine, PLAY_FS, PLAY_VOICES) != FM_OK)
        return -1;
    return use_patch(p, &PATCHES[PATCH_SOUNDTRACK]);
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
