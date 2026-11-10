/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "play.h"

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
    if (fm_engine_init(&p->engine, PLAY_FS, PLAY_VOICES) != FM_OK)
        return -1;
    return use_patch(p, &PATCHES[PATCH_SOUNDTRACK]);
}

/* Main loop: one event into the queue. When it is full the event is dropped and counted: the next
 * render then lets every note go (CC 123), so a lost note off never leaves a note hanging. */
static void put(play *p, uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t h = p->head;
    if (d1 > 127u || d2 > 127u)                 /* not MIDI data */
        return;
    if (st == 0x90u && d2 != 0u)
        p->last_note = (int32_t)d1;
    if (h - p->tail >= PLAY_QUEUE) {
        p->lost = p->lost + 1u;
        return;
    }
    p->queue[h % PLAY_QUEUE] = st << 16 | d1 << 8 | d2;
    p->head = h + 1u;                           /* after the entry: the ISR never reads it early */
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

void play_render(play *p, int32_t *out, uint32_t n)
{
    uint32_t i;
    uint32_t h = p->head, t = p->tail;          /* one read: the main loop may write it any time */
    if (n > PLAY_BLOCK)
        n = PLAY_BLOCK;
    for (; t != h; t++)
        apply(p, p->queue[t % PLAY_QUEUE]);
    p->tail = t;
    if (p->lost != p->lost_seen) {              /* some events were dropped: let every note go */
        p->lost_seen = p->lost;
        fm_engine_controller(&p->engine, 123, 0);
    }
    fm_engine_render(&p->engine, p->buf, n);
    for (i = 0; i < 2u * n; i++)
        out[i] += p->buf[i];
}
