/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM1-8P: fm1-8p's engine as a playable synth under fm1-dock, on its default patch (firmware/play.c).
 *   keys   play notes with their velocity; the module owns the panel (HB_F_PANEL), so it keeps the
 *          octave (OCT- / OCT+, their LEDs lit off center as fm1-dock's own modules do), lights each
 *          key while it is held and sends the keys' notes out on MIDI channel 1
 *   MIDI   whatever the host takes in (USB, Bluetooth or the jack) comes through midi(), all alike:
 *          note on and off, sustain (CC 64), all sound / notes off (CC 120 / 123), pitch bend; any
 *          channel. With LOCAL OFF the host plays the keys through note(), on its own octave.
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

/* OCT- / OCT+: lit off center; an SMK-37's outer keys go on from key 0's note */
static void octave_shown(void)
{
    H->led(HB_BTN_OCT_DOWN, play_oct_led(&P, PLAY_OCT_DOWN));
    H->led(HB_BTN_OCT_UP, play_oct_led(&P, PLAY_OCT_UP));
    if (HB_HOST_HAS(H, key_base))
        H->key_base((uint32_t)((int32_t)PLAY_KEY0 + 12 * P.oct));
}

static void ui(uint32_t ev, int32_t arg)
{
    uint32_t id = HB_EV_ID(ev), kind = HB_EV_KIND(ev);
    if ((kind == HB_EV_KEY_DOWN || kind == HB_EV_KEY_UP) && id >= PLAY_KEYS)
        return;
    switch (kind) {
    case HB_EV_KEY_DOWN:                        /* arg: the velocity, 0 an unsensed key */
        play_key_down(&P, id, (uint32_t)arg);
        H->led(HB_LED_KEY(id), 1);
        H->midi_send(0x90u, P.held[id], arg > 0 ? (uint32_t)arg : 100u);
        break;
    case HB_EV_KEY_UP:
        play_key_up(&P, id);
        H->led(HB_LED_KEY(id), 0);
        H->midi_send(0x80u, P.held[id], 0);
        break;
    case HB_EV_BTN_DOWN:
        if (id == HB_BTN_OCT_DOWN || id == HB_BTN_OCT_UP) {
            play_octave(&P, id == HB_BTN_OCT_UP ? 1 : -1);
            octave_shown();
        }
        break;
    default: break;
    }
}

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
        octave_shown();                         /* (back from the picker, which had the LEDs) */
    }
    if (last != drawn_note) {                   /* only when it changed: never per audio block */
        drawn_note = last;
        play_note_name(last, s);
        H->fill(0, 164, 240, 36, 0);
        H->text(0, 164, 240, 1, s, 0xFFFFu, 1);
    }
}

static int init(const hb_host_t *h)
{
    H = h;
    drawn_note = NOT_DRAWN;
    if (play_init(&P) != 0) {
        H->log("fm1-8p: engine init failed");
        return 1;                               /* the host falls back to the sine */
    }
    octave_shown();
    H->log_u32("fm1-8p: ready, voices", PLAY_VOICES);
    return 0;
}

/* no release(): init() starts afresh; no cc(): midi() takes every channel message */
HB_DESC = {HB_ABI, "FM1-8P", init, 0, note, 0, render, ui, draw, 0, 0, HB_F_PANEL, midi};
