/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM1-8P: fm1-8p's engine as a playable synth under fm1-dock, on its default patch (firmware/play.c).
 *   keys   play notes with their velocity; the host keeps OCT- / OCT+ and lights each key while it
 *          is held
 *   MIDI   whatever the host takes in (USB, Bluetooth or the jack) comes through midi(), all alike:
 *          note on and off, sustain (CC 64), all sound / notes off (CC 120 / 123), pitch bend; any
 *          channel
 * The screen shows the name, the patch (SOUNDTRACK at boot), the voice count and the last note
 * played. */
#include "hb_module.h"
#include "play.h"

static const hb_host_t *H;
static play P;                                  /* static: the engine's 5 KiB must not sit on a stack */
static int32_t drawn_note;
#define NOT_DRAWN (-2)                          /* drawn_note: the screen shows no note yet */
#define C_DIM HB_RGB(150, 150, 150)

static void render(int32_t *out, uint32_t n) { play_render(&P, out, n); }

static void note(uint32_t ch, uint32_t key, uint32_t vel)
{
    (void)ch;
    play_note(&P, key, vel);
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2) { play_midi(&P, st, d1, d2); }

/* Not called while midi() is set (the host hands that every channel message); kept for completeness. */
static void cc(uint32_t ch, uint32_t num, uint32_t val) { play_midi(&P, 0xB0u | (ch & 15u), num, val); }

static void ui(uint32_t ev, int32_t arg) { (void)ev; (void)arg; }

static void draw(uint32_t full)
{
    char s[PLAY_NOTE_NAME];
    int32_t last = play_last_note(&P);
    if (full) {
        char v[] = "N VOICES";
        v[0] = (char)('0' + PLAY_VOICES % 10u);
        H->fill(0, HB_TOP, 240, 240 - HB_TOP, 0);
        H->text(0, 48, 240, 1, "FM1-8P", HB_RGB(255, 176, 64), 1);
        H->text(0, 88, 240, 0, play_patch(&P)->name, 0xFFFFu, 1);
        H->text(0, 112, 240, 0, v, C_DIM, 1);
        H->text(0, 140, 240, 0, "LAST NOTE", C_DIM, 1);
        drawn_note = NOT_DRAWN;
    }
    if (last != drawn_note) {                   /* only when it changed: never per audio block */
        drawn_note = last;
        play_note_name(last, s);
        H->fill(0, 164, 240, 36, 0);
        H->text(0, 164, 240, 1, s, 0xFFFFu, 1);
    }
}

static void release(void) {}

static int init(const hb_host_t *h)
{
    H = h;
    drawn_note = NOT_DRAWN;
    if (play_init(&P) != 0) {
        H->log("fm1-8p: engine init failed");
        return 1;                               /* the host falls back to the sine */
    }
    H->log_u32("fm1-8p: ready, voices", PLAY_VOICES);
    return 0;
}

HB_DESC = {HB_ABI, "FM1-8P", init, release, note, cc, render, ui, draw, 0, 0, 0, midi};
