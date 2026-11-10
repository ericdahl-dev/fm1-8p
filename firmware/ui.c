/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui.h"

/* interaction.md section 3, the VCF section: page 4A (41-44) and 4B (45-48) */
static const ui_slot VCF_SLOTS[8] = {
    {41, "CUTOFF", "CUTOFF", FM_PARAM_FILTER_CUTOFF_CENTS, UI_LIVE, 100, 400},
    {42, "RESO", "RESONANCE", FM_PARAM_FILTER_RESONANCE, UI_LIVE, 10, 50},
    {43, "ENV", "ENV AMOUNT", FM_PARAM_FILTER_ENV_AMOUNT_CENTS, UI_LIVE, 100, 600},
    {44, "KEY", "KEY TRACK", FM_PARAM_FILTER_KEY_TRACK, UI_LIVE, 10, 100},
    {45, "HPF", "HIGHPASS", FM_PARAM_HIGHPASS_STEP, UI_LIVE, 1, 1},
    {46, "VEL", "VELOCITY", 0, UI_PENDING, 0, 0},
    {47, "D2 ENV", "DCO-2 ENV", 0, UI_PENDING, 0, 0},
    {48, "ENV SRC", "ENV SOURCE", 0, UI_PENDING, 0, 0},
};

const ui_slot *ui_page_slot(const ui *u, uint32_t k)
{
    (void)u;
    return k < 4u ? &VCF_SLOTS[k] : 0;
}

void ui_init(ui *u, play *p)
{
    u->p = p;
    u->state = UI_PLAY;
}

static void turn(ui *u, uint32_t k, int32_t clicks)
{
    const ui_slot *s = ui_page_slot(u, k);
    if (!s || s->kind != UI_LIVE)
        return;
    play_set_param(u->p, s->id, play_get_param(u->p, s->id) +
                   (clicks >= 3 || clicks <= -3 ? s->fast : s->step) * clicks);
}

void ui_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms)
{
    uint32_t id = ev & 0xFFu;
    (void)ms;
    if (ev == (UI_EV_BTN_DOWN | UI_BTN_ENV))
        u->state = UI_EDIT;
    else if ((ev & 0xFF000000u) == UI_EV_ENC && id >= UI_ENC_KNOB1 && id <= UI_ENC_KNOB4 &&
             u->state == UI_EDIT)
        turn(u, id - UI_ENC_KNOB1, arg);
}
