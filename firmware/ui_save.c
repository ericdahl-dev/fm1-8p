/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p SAVE: the destination, the name and the write (interaction.md, SAVE). */
#include "ui_save.h"

void ui_save_open(ui *u, uint32_t ms)
{
    uint32_t q = play_prog(u->p), s;
    (void)ms;
    u->save_from = u->state;
    if (q < PLAY_PROG_P) {
        u->save_dest = q;                       /* the slot it came from */
    } else {                                    /* a P patch: the first empty user slot */
        for (s = 0; s < STORE_USER_SLOTS && u->p->lib && store_slot_used(u->p->lib, s); s++)
            ;
        u->save_dest = s < STORE_USER_SLOTS ? s : 0u;
    }
    play_prog_name(u->p, u->save_name);
    for (s = 0; s < STORE_NAME_LEN; s++)        /* all 12 places editable: spaces past the end */
        if (!u->save_name[s])
            u->save_name[s] = ' ';
    u->save_name[STORE_NAME_LEN] = 0;
    u->save_cursor = 0;
    u->save_step = UI_SAVE_EDITING;
    u->state = UI_SAVE;
    u->dirty |= UI_D_FULL;
}

/* the characters a name takes (interaction.md, SAVE: KNOB1) */
static const char CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
#define N_CHARS (sizeof CHARS - 1u)

static store_patch snap;                        /* the patch a confirming tap took (main loop) */

/* "SAVED 23" and the like into u->save_msg, as the banner */
static void say(ui *u, const char *what, uint32_t slot, uint32_t ms)
{
    char *s = u->save_msg;
    while (*what)
        *s++ = *what++;
    if (slot < STORE_USER_SLOTS) {
        *s++ = ' ';
        ui_prog_number(slot, s);
    } else {
        *s = 0;
    }
    u->banner = u->save_msg;
    u->banner_ms = ms;
    u->dirty |= UI_D_FOOT;
}

/* "REPLACE 23 NAME?" into u->save_msg: the screen asks it, with SAVE AGAIN under it */
static void replace_prompt(ui *u)
{
    static store_patch there;                   /* main loop */
    const char *w = "REPLACE ";
    char *s = u->save_msg;
    uint32_t i;
    while (*w)
        *s++ = *w++;
    ui_prog_number(u->save_dest, s);
    while (*s)
        s++;
    *s++ = ' ';
    if (store_slot_read(u->p->lib, u->save_dest, &there) == 0)
        for (i = 0; i < STORE_NAME_LEN && there.name[i]; i++)
            *s++ = there.name[i];
    *s++ = '?';
    *s = 0;
    u->save_step = UI_SAVE_CONFIRM;
    u->dirty |= UI_D_ALL & ~UI_D_FULL;
}

/* the edit buffer and the name, trailing spaces trimmed, into snap */
static void take_snapshot(ui *u)
{
    char name[STORE_NAME_LEN + 1u];
    int32_t i;
    for (i = 0; i <= (int32_t)STORE_NAME_LEN; i++)
        name[i] = u->save_name[i];
    for (i = (int32_t)STORE_NAME_LEN - 1; i >= 0 && name[i] == ' '; i--)
        name[i] = 0;
    play_snapshot(u->p, name, &snap);
}

/* writes the snapshot to its slot and says how it went */
static void write(ui *u, uint32_t ms)
{
    u->save_waiting = 0;
    if (play_save(u->p, u->save_dest, &snap) == 0)
        say(u, "SAVED", u->save_dest, ms);
    else
        say(u, "SAVE FAILED", STORE_USER_SLOTS, ms);
}

static const char WAITING[] = "SAVING WHEN SILENT";

void ui_save_cancel(ui *u, uint32_t ms)
{
    u->save_waiting = 0;
    say(u, "SAVE CANCELED", STORE_USER_SLOTS, ms);
}

void ui_save_tick(ui *u, uint32_t ms)
{
    if (!u->save_waiting)
        return;
    if (play_silent(u->p) ||
        (u->held >> UI_BTN_SAVE & 1u && ms - u->down_ms[UI_BTN_SAVE] >= UI_FORCE_MS)) {
        write(u, ms);                           /* silent, or forced: SAVE held (the sound stalls) */
    } else if (!*u->banner || u->banner == WAITING) {   /* (another banner shows first) */
        u->banner = WAITING;
        u->banner_ms = ms;
        u->dirty |= UI_D_FOOT;
    }
}

void ui_save_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms)
{
    uint32_t id = ev & 0xFFu;
    uint32_t tap = (ev & 0xFF000000u) == UI_EV_BTN_UP && id < UI_BUTTONS &&
                   ms - u->down_ms[id] < UI_TAP_MS;
    if (ev == (UI_EV_ENC | UI_ENC_PRESETS) && arg) {   /* the destination: a user slot, wrapping */
        int32_t clicks = arg >= 3 || arg <= -3 ? (arg > 0 ? 4 : -4) : arg;
        u->save_dest = (uint32_t)((int32_t)u->save_dest + clicks + (int32_t)STORE_USER_SLOTS) %
                       STORE_USER_SLOTS;
        u->save_step = UI_SAVE_EDITING;
        u->dirty |= UI_D_ALL & ~UI_D_FULL;
    }
    if (ev == (UI_EV_ENC | UI_ENC_SELECT)) {   /* the cursor, clamped */
        int32_t c = (int32_t)u->save_cursor + arg;
        u->save_cursor = c < 0 ? 0u : c > (int32_t)STORE_NAME_LEN - 1 ? STORE_NAME_LEN - 1u : (uint32_t)c;
        u->dirty |= UI_D_ALL & ~UI_D_FULL;
    } else if (ev == (UI_EV_ENC | UI_ENC_KNOB1)) {   /* the character at the cursor */
        char *c = &u->save_name[u->save_cursor];
        uint32_t i;
        for (i = 0; i < N_CHARS && CHARS[i] != *c; i++)
            ;
        if (i == N_CHARS)                       /* one SAVE can't type ('.' in E. BASS): from space */
            i = 0;
        *c = CHARS[(uint32_t)((int32_t)i + arg % (int32_t)N_CHARS + (int32_t)N_CHARS) % N_CHARS];
        u->dirty |= UI_D_ALL & ~UI_D_FULL;
    } else if (ev == (UI_EV_ENC | UI_ENC_KNOB2) && arg) {   /* insert a space, or delete */
        uint32_t i;
        if (arg > 0) {
            for (i = STORE_NAME_LEN - 1u; i > u->save_cursor; i--)
                u->save_name[i] = u->save_name[i - 1u];
            u->save_name[u->save_cursor] = ' ';
        } else {
            for (i = u->save_cursor; i < STORE_NAME_LEN - 1u; i++)
                u->save_name[i] = u->save_name[i + 1u];
            u->save_name[STORE_NAME_LEN - 1u] = ' ';
        }
        u->dirty |= UI_D_ALL & ~UI_D_FULL;
    }
    if ((ev == (UI_EV_BTN_UP | UI_BTN_HOME) || ev == (UI_EV_BTN_UP | UI_BTN_EDIT)) && tap) {
        u->state = u->save_from;                /* canceled: nothing written */
        u->dirty |= UI_D_FULL;
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_SAVE) && tap) {
        if (u->save_step == UI_SAVE_EDITING && u->save_dest != play_prog(u->p) &&
            store_slot_used(u->p->lib, u->save_dest)) {   /* another patch is there: ask first */
            replace_prompt(u);
            return;
        }
        take_snapshot(u);                       /* what is written, whatever happens next */
        u->state = u->save_from;
        u->dirty |= UI_D_FULL;
        u->save_waiting = 1;
        ui_save_tick(u, ms);                    /* silent now: written now */
    }
}
