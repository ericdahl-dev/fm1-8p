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
    if (u->save_draft_prog == q)                /* a canceled SAVE of this patch: its name again */
        for (s = 0; s <= STORE_NAME_LEN; s++)
            u->save_name[s] = u->save_draft[s];
    else
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
    s += 2;                                     /* a user slot: two digits (a scan here is strlen to clang) */
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
    u->save_home_armed = 0;
    if (play_save(u->p, u->save_dest, &snap) == 0) {
        u->save_draft_prog = UI_SAVE_NO_DRAFT;  /* written: no draft to offer */
        say(u, "SAVED", u->save_dest, ms);
    } else
        say(u, "SAVE FAILED", STORE_USER_SLOTS, ms);
}

static const char HOLD[] = "HOLD SAVE: NOW";
static const char HOME_AGAIN[] = "HOME AGAIN TO CANCEL SAVE";

void ui_save_home(ui *u, uint32_t ms)
{
    if (u->save_home_armed && ms - u->save_home_ms < UI_BANNER_MS) {
        u->save_waiting = 0;
        u->save_home_armed = 0;
        say(u, "SAVE CANCELED", STORE_USER_SLOTS, ms);
        return;
    }
    u->save_home_armed = 1;                     /* the first tap only asks */
    u->save_home_ms = ms;
    u->banner = HOME_AGAIN;
    u->banner_ms = ms;
    u->dirty |= UI_D_FOOT;
}

void ui_save_tick(ui *u, uint32_t ms)
{
    const char *b;
    if (!u->save_waiting)
        return;
    if (play_silent(u->p) ||
        (u->held >> UI_BTN_SAVE & 1u && ms - u->down_ms[UI_BTN_SAVE] >= UI_FORCE_MS)) {
        write(u, ms);                           /* silent, or forced: SAVE held (the sound stalls) */
        return;
    }
    if (!(!*u->banner || u->banner == u->save_wait_msg || u->banner == HOLD ||
          ms - u->banner_ms >= UI_BANNER_MS))
        return;                                 /* another banner shows first */
    b = (ms - u->save_wait_ms) / UI_BANNER_MS & 1u ? HOLD : u->save_wait_msg;   /* by turns */
    if (b != u->banner)
        u->dirty |= UI_D_FOOT;
    u->banner = b;
    u->banner_ms = ms;
}

void ui_save_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms)
{
    uint32_t id = ev & 0xFFu;
    uint32_t tap = (ev & 0xFF000000u) == UI_EV_BTN_UP && id < UI_BUTTONS &&
                   ms - u->down_ms[id] < UI_TAP_MS;
    if (ev == (UI_EV_ENC | UI_ENC_PRESETS) && arg) {   /* the destination: a user slot, wrapping */
        int32_t clicks = arg >= 3 || arg <= -3 ? (arg > 0 ? 8 : -8) : arg;   /* fast: a row, 11 to 21 */
        u->save_dest = (uint32_t)((int32_t)u->save_dest + clicks + (int32_t)STORE_USER_SLOTS) %
                       STORE_USER_SLOTS;
        u->save_step = UI_SAVE_EDITING;
        u->dirty |= UI_D_ALL & ~UI_D_FULL;
    }
    if (ev == (UI_EV_ENC | UI_ENC_ALGORITHM) && arg) {   /* the next (previous) empty slot, a click */
        int32_t way = arg > 0 ? 1 : -1, clicks = arg;
        uint32_t q = u->save_dest, n;
        for (; clicks; clicks -= way) {
            for (n = 0; n < STORE_USER_SLOTS; n++) {
                q = (q + (way > 0 ? 1u : STORE_USER_SLOTS - 1u)) % STORE_USER_SLOTS;
                if (!u->p->lib || !store_slot_used(u->p->lib, q))
                    break;
            }
            if (n == STORE_USER_SLOTS)          /* every slot used: stay */
                return;
            u->save_dest = q;
        }
        u->save_step = UI_SAVE_EDITING;
        u->dirty |= UI_D_ALL & ~UI_D_FULL;
    }
    if (ev == (UI_EV_ENC | UI_ENC_SELECT) || ev == (UI_EV_ENC | UI_ENC_KNOB2)) {   /* the cursor, clamped */
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
    } else if (ev == (UI_EV_ENC | UI_ENC_KNOB3) && arg) {   /* a space inserted, or one deleted, a click */
        uint32_t i, n = (uint32_t)(arg < 0 ? -arg : arg);
        if (n > STORE_NAME_LEN - u->save_cursor)  /* (more is the same: all past the cursor) */
            n = STORE_NAME_LEN - u->save_cursor;
        for (; n; n--) {
            if (arg > 0) {
                for (i = STORE_NAME_LEN - 1u; i > u->save_cursor; i--)
                    u->save_name[i] = u->save_name[i - 1u];
                u->save_name[u->save_cursor] = ' ';
            } else {
                for (i = u->save_cursor; i < STORE_NAME_LEN - 1u; i++)
                    u->save_name[i] = u->save_name[i + 1u];
                u->save_name[STORE_NAME_LEN - 1u] = ' ';
            }
        }
        u->dirty |= UI_D_ALL & ~UI_D_FULL;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_HOME) && tap) {   /* canceled: nothing written, the name kept */
        uint32_t i;
        for (i = 0; i <= STORE_NAME_LEN; i++)
            u->save_draft[i] = u->save_name[i];
        u->save_draft_prog = play_prog(u->p);
        u->state = u->save_from;
        u->dirty |= UI_D_FULL;
        say(u, "SAVE CANCELED", STORE_USER_SLOTS, ms);
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
        u->save_wait_ms = ms;
        u->save_home_armed = 0;
        {
            char *w = u->save_wait_msg;         /* SAVING 23 WHEN SILENT */
            const char *t = "SAVING ";
            while (*t)
                *w++ = *t++;
            ui_prog_number(u->save_dest, w);
            w += 2;                             /* a user slot: two digits (a scan is strlen to clang) */
            for (t = " WHEN SILENT"; *t; )
                *w++ = *t++;
            *w = 0;
        }
        ui_save_tick(u, ms);                    /* silent now: written now */
    }
}
