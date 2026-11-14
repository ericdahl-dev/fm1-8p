/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM1-8P: fm1-8p's engine as a playable synth under fm1-dock, on its default patch (firmware/play.c).
 *   keys   play notes with their velocity; the module owns the panel (HB_F_PANEL), so it keeps the
 *          octave (OCT- / OCT+, their LEDs lit off center as fm1-dock's own modules do), lights each
 *          key while it is held and sends the keys' notes out on MIDI channel 1
 *   MIDI   whatever the host takes in (USB, Bluetooth or the jack) comes through midi(), all alike:
 *          note on and off, sustain (CC 64), all sound / notes off (CC 120 / 123), pitch bend; any
 *          channel. With LOCAL OFF the host plays the keys through note(), on its own octave.
 *   pages  ENV opens page 5A VCF (docs/ui/interaction.md), HPF, CUTOFF, RES and LFO on KNOB1-4, and
 *          again 5B, ENV, KEY, DYNAMICS and MODE; a tap of EDIT or HOME goes back to PLAY. The keys
 *          play on every page.
 *   PRESETS steps through the 32 factory patches, P1 .. P32, on any page; with edits the first turn
 *          warns and the next one the same way within 2 s discards them (firmware/ui.c).
 * The screen is the module's (HB_F_SCREEN), drawn in a docs/ui/visual.md theme (its style table,
 * firmware/ui_map.c: Membrane until Night Panel's table is added), and
 * only what changed is redrawn (firmware/ui.c says what), never once per audio block. */
#include "hb_module.h"
#include "play.h"
#include "ui.h"
#include "draw.h"

static const hb_host_t *H;
static play P;                                  /* static: the engine's 5 KiB must not sit on a stack */
static ui U;

static void render(int32_t *out, uint32_t n) { play_render(&P, out, n); }

static void note(uint32_t ch, uint32_t key, uint32_t vel)
{
    (void)ch;
    play_note(&P, key, vel);
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2) { play_midi(&P, st, d1, d2); }

/* ---------------------------------------------------------------- panel */
/* OCT- / OCT+: lit off center; an SMK-37's outer keys go on from key 0's note */
static void octave_shown(void)
{
    H->led(HB_BTN_OCT_DOWN, play_oct_led(&P, PLAY_OCT_DOWN));
    H->led(HB_BTN_OCT_UP, play_oct_led(&P, PLAY_OCT_UP));
    if (HB_HOST_HAS(H, key_base))
        H->key_base((uint32_t)((int32_t)PLAY_KEY0 + 12 * P.oct));
}

/* the section buttons and EDIT, as ui_led says (sent when one changes, and all on draw(full)) */
static uint8_t led_sent[16];
static void page_leds(uint32_t all)
{
    static const uint8_t BTN[] = {HB_BTN_SEL, HB_BTN_ENV, HB_BTN_LFO, HB_BTN_FX, HB_BTN_EDIT, HB_BTN_GLO};
    uint32_t i;
    for (i = 0; i < sizeof BTN; i++) {
        uint32_t v = ui_led(&U, BTN[i]);
        if (all || led_sent[BTN[i]] != v) {
            led_sent[BTN[i]] = (uint8_t)v;
            H->led(BTN[i], v);
        }
    }
}

/* the keys' LEDs: the keypad's glow while SEL is held, else lit while a key is held */
static uint32_t keys_down;                      /* bit k: key k is down (and playing) */
static void keypad_leds(void)
{
    uint32_t k;
    for (k = 0; k < PLAY_KEYS; k++)
        H->led(HB_LED_KEY(k), ui_keypad_led(&U, k) == UI_LED_GLOW ? UI_LED_GLOW : keys_down >> k & 1u);
}

static void ui_ev(uint32_t ev, int32_t arg)
{
    uint32_t id = HB_EV_ID(ev), kind = HB_EV_KIND(ev);
    if ((kind == HB_EV_KEY_DOWN || kind == HB_EV_KEY_UP) && id >= PLAY_KEYS)
        return;
    if ((kind == HB_EV_KEY_DOWN || kind == HB_EV_KEY_UP) && ui_key(&U, id, kind == HB_EV_KEY_DOWN, H->ms()))
        return;                                 /* the number jump's keypad (SEL held): no sound */
    switch (kind) {
    case HB_EV_KEY_DOWN:                        /* arg: the velocity, 0 an unsensed key */
        play_key_down(&P, id, (uint32_t)arg);
        keys_down |= 1u << id;
        H->led(HB_LED_KEY(id), 1);
        H->midi_send(0x90u, P.held[id], arg > 0 ? (uint32_t)arg : 100u);
        return;
    case HB_EV_KEY_UP:
        play_key_up(&P, id);
        keys_down &= ~(1u << id);
        H->led(HB_LED_KEY(id), 0);
        H->midi_send(0x80u, P.held[id], 0);
        return;
    case HB_EV_BTN_DOWN:
        if (id == HB_BTN_OCT_DOWN || id == HB_BTN_OCT_UP) {
            play_octave(&P, id == HB_BTN_OCT_UP ? 1 : -1);
            octave_shown();                     /* then to the pages too: it cancels the PRESETS warning */
        }
        break;
    case HB_EV_BTN_UP:
    case HB_EV_ENC:
        break;
    default:                                    /* a key's pressure, ...: nothing for the pages */
        return;
    }
#ifdef FM1_DEBUG
    H->log_u32("fm1-8p TRAIL ev kind/arg/id", (ev & 0xFF0000FFu) | ((uint32_t)arg & 0xFFFFu) << 8);
#endif
    ui_event(&U, ev, arg, H->ms());             /* pages and knobs */
    page_leds(0);
    if (id == HB_BTN_SEL && (kind == HB_EV_BTN_DOWN || kind == HB_EV_BTN_UP))
        keypad_leds();                          /* the keypad glows while SEL is held */
}

/* The host calls draw() from its main loop: the LEDs on a full redraw, then the screen (draw.c) */
static void draw(uint32_t full)
{
    play_store_tick(&P);                        /* the first: the user slots from flash */
    if (full) {
        octave_shown();                         /* (back from the picker, which had the LEDs) */
        page_leds(1);
    }
    ui_draw_frame(full);
    if (!full)
        page_leds(0);                           /* a change ui_take made: compare, the EDIT blink */
}

/* The user slots (patch_store.c): fm1-dock's banks 1 and 2, found by this module's name. */
static store_lib LIB;
static int bank_load(void *ctx, uint32_t idx, void *p, uint32_t n)
{
    (void)ctx;
    return H->bank_load_n(idx, p, n);
}
static int bank_save(void *ctx, uint32_t idx, const void *p, uint32_t n)
{
    (void)ctx;
    return H->bank_save_n(idx, p, n);
}
static const store_io FLASH = { bank_load, bank_save, 0 };

static int init(const hb_host_t *h)
{
    H = h;
    keys_down = 0;
    ui_draw_init(h, &P, &U);
    if (play_init(&P) != 0) {
        H->log("fm1-8p: engine init failed");
        return 1;                               /* the host falls back to the sine */
    }
    if (HB_HOST_HAS(H, bank_free))              /* (a host before the banks: no user slots) */
        play_attach_store(&P, &LIB, &FLASH);    /* read on the first draw: init stays fixed work */
    ui_init(&U, &P);
    octave_shown();
    page_leds(1);
    H->log_u32("fm1-8p: ready, voices", PLAY_VOICES);
    return 0;
}

/* ui.h's values are hb_module.h's (ui.c is plain C, built and tested without fm1-dock) */
_Static_assert(UI_EV_BTN_DOWN == HB_EV_BTN_DOWN && UI_EV_BTN_UP == HB_EV_BTN_UP && UI_EV_ENC == HB_EV_ENC,
               "ui.h's events are hb_module.h's");
_Static_assert(UI_BTN_FX == HB_BTN_FX && UI_BTN_SEL == HB_BTN_SEL && UI_BTN_ENV == HB_BTN_ENV &&
               UI_BTN_LFO == HB_BTN_LFO && UI_BTN_EDIT == HB_BTN_EDIT && UI_BTN_GLO == HB_BTN_GLO && UI_BTN_HOME == HB_BTN_HOME && UI_BTN_SAVE == HB_BTN_SAVE,
               "ui.h's buttons are hb_module.h's");
_Static_assert(UI_ENC_SELECT == HB_ENC_SELECT && UI_ENC_ALGORITHM == HB_ENC_ALGORITHM &&
               UI_ENC_KNOB1 == HB_ENC_KNOB1 && UI_ENC_KNOB2 == HB_ENC_KNOB2 &&
               UI_ENC_KNOB3 == HB_ENC_KNOB3 && UI_ENC_KNOB4 == HB_ENC_KNOB4 &&
               UI_ENC_PRESETS == HB_ENC_PRESETS,
               "ui.h's encoders are hb_module.h's");

/* no release(): init() starts afresh; no cc(): midi() takes every channel message */
HB_DESC = {HB_ABI, "FM1-8P", init, 0, note, 0, render, ui_ev, draw, 0, 0, HB_F_SCREEN | HB_F_PANEL, midi};
