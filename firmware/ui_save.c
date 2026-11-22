/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p SAVE: the destination, the name and the write (interaction.md, SAVE). */
#include "ui_save.h"

static store_patch snap;                        /* the patch a confirming tap took (main loop) */
static seq_pattern psnap;                       /* or the pattern, as SAVE opened on it, and its name */
static char psnap_name[STORE_NAME_LEN + 1u];
static uint32_t psnap_token;                   /* pattern_snapshot's: a takeover since keeps what runs */
static uint32_t psnap_slot;                    /* the pattern's slot, from the same look */

/* the slot playing: the patch's, or the pattern SAVE opened on (Eric on #324: a takeover while SAVE is
 * open doesn't change what it saves) */
static uint32_t current(const ui *u)
{
    return u->save_pattern ? psnap_slot : play_prog(u->p);
}

/* the draft's key for the slot playing (a pattern's has UI_SAVE_PATTERN_DRAFT on) */
static uint32_t draft_key(const ui *u)
{
    return current(u) | (u->save_pattern ? UI_SAVE_PATTERN_DRAFT : 0u);
}

int ui_save_slot_used(const ui *u, uint32_t s, char *name)
{
    static store_patch there;                   /* main loop */
    static seq_pattern pat;
    uint32_t i;
    if (name)
        name[0] = 0;
    if (u->save_pattern) {
        if (!u->p->plib || !pat_slot_used(u->p->plib, s))
            return 0;
        if (name)
            (void)pat_slot_read(u->p->plib, s, &pat, name);
        return 1;
    }
    if (!u->p->lib || !store_slot_used(u->p->lib, s))
        return 0;
    if (name && store_slot_read(u->p->lib, s, &there) == 0)
        for (i = 0; i <= STORE_NAME_LEN; i++)
            name[i] = there.name[i];
    return 1;
}

uint32_t ui_save_edited(const ui *u)
{
    return u->save_pattern ? pattern_edited(play_patterns(u->p)) : play_edited(u->p);
}

void ui_save_open(ui *u, uint32_t ms)
{
    uint32_t q, s;
    u->save_open_ms = ms;
    u->save_from = u->state;
    u->save_pattern = u->state == UI_SEQ;
    if (u->save_pattern)                        /* the pattern, its slot and its name: one look */
        psnap_token = pattern_snapshot(play_patterns(u->p), &psnap, &psnap_slot, u->save_name);
    q = current(u);
    if (u->save_pattern) {
        u->save_dest = q;                       /* the pattern slot playing (always one: 11..88) */
    } else if (q < PLAY_PROG_P) {
        u->save_dest = q;                       /* the slot it came from */
    } else {                                    /* a P patch: the first empty user slot */
        for (s = 0; s < STORE_USER_SLOTS && ui_save_slot_used(u, s, NULL); s++)
            ;
        u->save_dest = s < STORE_USER_SLOTS ? s : 0u;
    }
    if (u->save_draft_prog == draft_key(u))     /* a canceled SAVE of this patch (pattern): its name again */
        for (s = 0; s <= STORE_NAME_LEN; s++)
            u->save_name[s] = u->save_draft[s];
    else if (!u->save_pattern)                  /* (a pattern's name came with its snapshot) */
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


/* "SAVED 23" and the like into u->save_msg, as the banner; "SAVED PATTERN 23" with pattern */
static void say(ui *u, const char *what, uint32_t slot, uint32_t ms, uint32_t pattern)
{
    char *s = u->save_msg;
    const char *pt = " PATTERN";
    while (*what)
        *s++ = *what++;
    if (pattern)
        while (*pt)
            *s++ = *pt++;
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

/* "REPLACE 23 NAME?" (or, `what` "DELETE ", "DELETE 23 NAME?") into u->save_msg: the screen asks it, at
 * step `step` */
static void ask(ui *u, const char *what, uint32_t step)
{
    char there[STORE_NAME_LEN + 1u];
    const char *w = what;
    char *s = u->save_msg;
    uint32_t i;
    while (*w)                                  /* (no PATTERN: SEQ's SAVE says it, and 30 characters fit) */
        *s++ = *w++;
    ui_prog_number(u->save_dest, s);
    s += 2;                                     /* a user slot: two digits (a scan here is strlen to clang) */
    *s++ = ' ';
    (void)ui_save_slot_used(u, u->save_dest, there);
    for (i = 0; i < STORE_NAME_LEN && there[i]; i++)
        *s++ = there[i];
    *s++ = '?';
    *s = 0;
    u->save_step = step;
    u->dirty |= UI_D_ALL & ~UI_D_FULL;
}

static void replace_prompt(ui *u)
{
    ask(u, "REPLACE ", UI_SAVE_CONFIRM);
}

/* the edit buffer and the name, trailing spaces trimmed, into snap; or the name only into psnap_name (the
 * pattern is the one SAVE opened on) */
static void take_snapshot(ui *u)
{
    char name[STORE_NAME_LEN + 1u];
    int32_t i;
    for (i = 0; i <= (int32_t)STORE_NAME_LEN; i++)
        name[i] = u->save_name[i];
    for (i = (int32_t)STORE_NAME_LEN - 1; i >= 0 && name[i] == ' '; i--)
        name[i] = 0;
    if (u->save_pattern) {
        for (i = 0; i <= (int32_t)STORE_NAME_LEN; i++)
            psnap_name[i] = name[i];
    } else {
        play_snapshot(u->p, name, &snap);
    }
}

/* writes the snapshot to its slot and says how it went */
static void write_slot(ui *u, uint32_t ms)
{
    u->save_waiting = 0;
    u->save_home_armed = 0;
    if (u->save_delete) {                       /* a delete (#303) */
        u->save_delete = 0;
        if ((u->save_pattern ? pattern_delete(play_patterns(u->p), u->save_dest) : play_delete(u->p, u->save_dest)) == 0)
            say(u, "DELETED", u->save_dest, ms, u->save_pattern);
        else
            say(u, "DELETE FAILED", STORE_USER_SLOTS, ms, 0);
        return;
    }
    if ((u->save_pattern ? pattern_save(play_patterns(u->p), u->save_dest, &psnap, psnap_name, psnap_token)
                         : play_save(u->p, u->save_dest, &snap)) == 0) {
        u->save_draft_prog = UI_SAVE_NO_DRAFT;  /* written: no draft to offer */
        say(u, "SAVED", u->save_dest, ms, u->save_pattern);
    } else
        say(u, "SAVE FAILED", STORE_USER_SLOTS, ms, 0);
}

/* the longest wait message, with its NUL, fits save_wait_msg (Codex P3 on #314: a longer verb would
 * overflow it) */
#define WAIT_SAVING "SAVING "
#define WAIT_DELETING "DELETING "
#define WAIT_WHEN " WHEN SILENT"
typedef char ui_require_wait_msg_fits[sizeof WAIT_DELETING "23" WAIT_WHEN <= sizeof ((ui *)0)->save_wait_msg &&
                                      sizeof WAIT_SAVING "23" WAIT_WHEN <= sizeof ((ui *)0)->save_wait_msg ? 1 : -1];

/* "SAVING 23 WHEN SILENT" (or DELETING) into u->save_wait_msg: the verbs are SAVING and DELETING only */
static void wait_msg(ui *u, const char *what)
{
    char *w = u->save_wait_msg;
    const char *t = what;
    while (*t)
        *w++ = *t++;
    ui_prog_number(u->save_dest, w);
    w += 2;                                     /* a user slot: two digits (a scan is strlen to clang) */
    for (t = WAIT_WHEN; *t; )
        *w++ = *t++;
    *w = 0;
}

static const char HOLD[] = "HOLD SAVE: NOW";
static const char HOME_AGAIN[] = "HOME AGAIN TO CANCEL SAVE";
static const char HOME_AGAIN_DELETE[] = "HOME AGAIN TO CANCEL DELETE";   /* a waiting delete's (#303) */

void ui_save_home(ui *u, uint32_t ms)
{
    if (u->save_home_armed && ms - u->save_home_ms < UI_BANNER_MS) {
        say(u, u->save_delete ? "DELETE CANCELED" : "SAVE CANCELED", STORE_USER_SLOTS, ms, 0);
        u->save_waiting = 0;
        u->save_delete = 0;                     /* (a waiting delete too, #303) */
        u->save_home_armed = 0;
        return;
    }
    u->save_home_armed = 1;                     /* the first tap only asks */
    u->save_home_ms = ms;
    u->banner = u->save_delete ? HOME_AGAIN_DELETE : HOME_AGAIN;
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
        write_slot(u, ms);                      /* silent, or forced: SAVE held (the sound stalls) */
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
                if (!ui_save_slot_used(u, q, NULL))
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
    if (ev == (UI_EV_BTN_UP | UI_BTN_HOME) && tap && u->save_step == UI_SAVE_DELETE) {   /* #303: the question only */
        u->save_step = UI_SAVE_EDITING;
        u->dirty |= UI_D_ALL & ~UI_D_FULL;
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_HOME) && tap) {   /* canceled: nothing written, the name kept */
        uint32_t i;
        for (i = 0; i <= STORE_NAME_LEN; i++)
            u->save_draft[i] = u->save_name[i];
        u->save_draft_prog = draft_key(u);
        u->state = u->save_from;
        u->dirty |= UI_D_FULL;
        say(u, "SAVE CANCELED", STORE_USER_SLOTS, ms, 0);
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_SEL) && tap && u->save_step == UI_SAVE_DELETE) {   /* the delete (#303) */
        u->save_step = UI_SAVE_EDITING;
        u->save_delete = 1;
        u->dirty |= UI_D_ALL & ~UI_D_FULL;
        if (play_silent(u->p)) {                /* now: SAVE stays, on the slot now EMPTY */
            write_slot(u, ms);
            return;
        }
        u->state = u->save_from;                /* or when silent, as a save waits */
        u->dirty |= UI_D_FULL;
        u->save_waiting = 1;
        u->save_wait_ms = ms;
        u->save_home_armed = 0;
        wait_msg(u, WAIT_DELETING);
        ui_save_tick(u, ms);
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_SEL) && ms - u->down_ms[UI_BTN_SEL] >= UI_DELETE_MS &&
        u->down_ms[UI_BTN_SEL] - u->save_open_ms < 0x80000000u &&   /* pressed in SAVE (wrap-safe) */
        ui_save_slot_used(u, u->save_dest, NULL)) {   /* held, on a slot that holds one: ask (#303) */
        ask(u, "DELETE ", UI_SAVE_DELETE);
        return;
    }
    if (ev == (UI_EV_BTN_UP | UI_BTN_SAVE) && tap) {
        if (u->save_step != UI_SAVE_CONFIRM && u->save_dest != current(u) &&
            ui_save_slot_used(u, u->save_dest, NULL)) {   /* another patch (pattern) is there: ask first */
            replace_prompt(u);
            return;
        }
        take_snapshot(u);                       /* what is written, whatever happens next */
        u->state = u->save_from;
        u->dirty |= UI_D_FULL;
        u->save_waiting = 1;
        u->save_wait_ms = ms;
        u->save_home_armed = 0;
        wait_msg(u, WAIT_SAVING);
        ui_save_tick(u, ms);                    /* silent now: written now */
    }
}
