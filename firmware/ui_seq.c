/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_seq.h"

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
