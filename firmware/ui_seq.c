/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_seq.h"

static void banner(ui *u, const char *s, uint32_t ms)
{
    u->banner = s;
    u->banner_ms = ms;
    u->dirty |= UI_D_FOOT;
}

void ui_chord_note(ui *u, uint32_t note, uint32_t vel, int on)
{
    uint32_t i, j;
    for (i = 0; i < u->chord_n && u->chord[i] != note; i++)
        ;
    if (on && i == u->chord_n && u->chord_n < UI_CHORD_MAX) {
        u->chord[u->chord_n] = (uint8_t)note;
        u->chord_vel[u->chord_n++] = (uint8_t)vel;
    } else if (!on && i < u->chord_n) {        /* (let go: the later ones move up) */
        for (j = i; j + 1u < u->chord_n; j++)
            u->chord[j] = u->chord[j + 1u], u->chord_vel[j] = u->chord_vel[j + 1u];
        u->chord_n--;
    }
}

void ui_seq_step_key(ui *u, uint32_t k, int down, uint32_t ms)
{
    seq *s = play_seq(u->p);
    uint32_t i, n = u->chord_n < PLAY_VOICES ? u->chord_n : PLAY_VOICES, vel = 0;
    if (!down)
        return;
    if (n) {                                    /* a chord held: written there (the first notes pressed) */
        for (i = 0; i < n; i++)
            vel = u->chord_vel[i] > vel ? u->chord_vel[i] : vel;
        seq_set_step(s, k, u->chord, n, vel);
        if (u->chord_n > PLAY_VOICES) {
            static const char MAX[] = "MAX 0 NOTES";    /* (the voice count: one digit) */
            for (i = 0; i < sizeof MAX; i++)
                u->seq_msg[i] = MAX[i];
            u->seq_msg[4] = (char)('0' + PLAY_VOICES % 10u);
            banner(u, u->seq_msg, ms);
        }
    }
    u->seq_cursor = k;
    u->dirty |= UI_D_SLOT | UI_D_FOCUS;
}

void ui_seq_open(ui *u)
{
    u->seq_from = u->state;
    u->state = UI_SEQ;
    u->readout = 0;
    u->warned = 0;
    u->focus = 4u;
    u->dirty |= UI_D_FULL;
}

void ui_seq_transport(ui *u)
{
    seq *s = play_seq(u->p);
    if (s->want)
        seq_stop(s);
    else
        seq_play(s);
    u->dirty |= UI_D_HEAD;
}

void ui_seq_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms)
{
    uint32_t id = ev & 0xFFu;
    int tap = (ev & 0xFF000000u) == UI_EV_BTN_UP && id < UI_BUTTONS && ms - u->down_ms[id] < UI_TAP_MS;
    (void)arg;
    if (ev == (UI_EV_BTN_UP | UI_BTN_SEQ) && tap) {          /* back, on the page it left */
        u->state = u->seq_from;
        u->dirty |= UI_D_FULL;
    }
}
