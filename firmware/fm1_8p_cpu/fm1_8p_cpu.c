/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FM1-8P CPU: fm1-8p's engine under fm1-dock, sweeping 1..8 voices (5 s each) on a held chord, then
 * the unison burst ("U", count 9: 8 voices in unison, 8 note-ons and a detune change every block),
 * then the full patch ("F", count 10: a 6-voice chord with both oscillators, vibrato, pulse-width
 * modulation, amp velocity, the filter and the chorus; cpu_test.h), with the worst render time per count on
 * screen and in the log (tools/dock.py logs):
 * one line "fm1-8p voices/worst permille" = count << 16 | permille for each count a sweep held in
 * full. The block that starts a count (engine init, note on) is not timed.
 *   KNOB1     pick a step by hand, 1..8, U, F (stops the sweep; a hand-picked U keeps bursting)
 *   PLAY      sweep again from 1 (after a sweep's F, the chord rings and F's row is final)
 * With CPU_UI (firmware/fm1_8p_cpu_ui), the screen is FM1-8P's VCF page instead of the table, its
 * CUTOFF turning a click each way every frame, so the counts are timed with the UI drawing (#47). */
#include "hb_module.h"
#include "cpu_test.h"
#ifdef CPU_UI
#include "draw.h"
static play UP;                                 /* the page's values: never rendered */
static ui UU;
static uint32_t turned_ms;
static int32_t way = 1;
#endif

#ifndef CPU_HOLD_BLOCKS
#define CPU_HOLD_BLOCKS 861u                    /* 5 s: 861 * 256 / 44100 */
#endif

static const hb_host_t *H;
static cpu_test T;                              /* static: 2 KiB of buffer must not sit on the ISR stack */
static uint32_t drawn[CT_LAST + 1], drawn_v;
/* Rows from HB_TOP + 4 to the screen's bottom (240), one per step: (240 - 24 - 4) / 10 = 21 px for
 * 16 px text, so all CT_LAST rows fit. */
#define ROW_PITCH ((240u - HB_TOP - 4u) / CT_LAST)
#define SHOWN_ERR 0x80000000u                   /* drawn[]: the row shows ERR (not a per mille) */

static void render(int32_t *out, uint32_t n)
{
    uint32_t t0 = H->ticks();
    cpu_test_render(&T, out, n);
    cpu_test_record(&T, H->ticks() - t0);
}

static void panel(uint32_t ev, int32_t arg)
{
    if (HB_EV_KIND(ev) == HB_EV_ENC && HB_EV_ID(ev) == HB_ENC_KNOB1) {
        cpu_test_sweep(&T, 0);
        cpu_test_request_voices(&T, (int32_t)cpu_test_voices(&T) + arg);
    } else if (HB_EV_KIND(ev) == HB_EV_BTN_DOWN && HB_EV_ID(ev) == HB_BTN_PLAY) {
        cpu_test_request_voices(&T, 1);
        cpu_test_sweep(&T, CPU_HOLD_BLOCKS);
    }
}

/* "N  NN.N %"; U for the burst, F for the full patch; "N  ERR" when the step failed to start */
static void line(uint32_t v, uint32_t pm)
{
    char s[12] = "N  000.0 %";
    s[0] = (char)(v == CT_FULL ? 'F' : v == CT_BURST ? 'U' : '0' + v);
    if (pm == SHOWN_ERR) {
        s[3] = 'E', s[4] = 'R', s[5] = 'R', s[6] = ' ', s[7] = ' ', s[8] = ' ', s[9] = ' ';
        H->text(0, (int32_t)(HB_TOP + 4u + ROW_PITCH * (v - 1u)), 240, 0, s, HB_RGB(255, 60, 60), 1);
        return;
    }
    s[3] = (char)(pm >= 1000u ? '0' + pm / 1000u % 10u : ' ');
    s[4] = (char)(pm >= 100u ? '0' + pm / 100u % 10u : ' ');
    s[5] = (char)('0' + pm / 10u % 10u);
    s[7] = (char)('0' + pm % 10u);
    H->text(0, (int32_t)(HB_TOP + 4u + ROW_PITCH * (v - 1u)), 240, 0, s,
            v == cpu_test_voices(&T) ? 0xFFFFu : HB_RGB(150, 150, 150), 1);
}

static void draw(uint32_t full)
{
    uint32_t v, pm, cur = cpu_test_voices(&T);
#ifdef CPU_UI
    while (cpu_test_take_completed(&T, &v, &pm))   /* (the log as below; the screen is the page) */
        if (cpu_test_failed(&T, v))
            H->log_u32("fm1-8p ERROR: step failed to start, not timed; count", v);
        else
            H->log_u32("fm1-8p voices/worst permille", v << 16 | pm);
    if (H->ms() - turned_ms >= UI_FRAME_MS) {   /* a turn a frame: CUTOFF up, then down */
        turned_ms = H->ms();
        ui_event(&UU, UI_EV_ENC | UI_ENC_KNOB2, way, turned_ms);
        way = -way;
    }
    ui_draw_frame(full);
    (void)cur;
    return;
#endif
    if (full) {
        H->fill(0, HB_TOP, 240, 240 - HB_TOP, 0);
        for (v = 0; v <= CT_LAST; v++)
            drawn[v] = ~0u;
        drawn_v = ~0u;
    }
    while (cpu_test_take_completed(&T, &v, &pm)) { /* each count a sweep held in full, once */
        if (cpu_test_failed(&T, v))
            H->log_u32("fm1-8p ERROR: step failed to start, not timed; count", v);
        else
            H->log_u32("fm1-8p voices/worst permille", v << 16 | pm);
    }
    for (v = 1; v <= CT_LAST; v++) {
        pm = cpu_test_failed(&T, v) ? SHOWN_ERR : cpu_test_worst_permille(&T, v);
        if (pm != drawn[v] || (cur != drawn_v && (v == cur || v == drawn_v))) {
            drawn[v] = pm;
            line(v, pm);
        }
    }
    drawn_v = cur;
}

static void note(uint32_t ch, uint32_t key, uint32_t vel) { (void)ch; (void)key; (void)vel; }
static void cc(uint32_t ch, uint32_t num, uint32_t val) { (void)ch; (void)num; (void)val; }
static void release(void) {}

static int init(const hb_host_t *h)
{
    H = h;
    if (cpu_test_init(&T) != 0) {
        H->log("fm1-8p: engine init failed");
        return 1;                               /* the host falls back to the sine */
    }
    cpu_test_sweep(&T, CPU_HOLD_BLOCKS);
#ifdef CPU_UI
    if (play_init(&UP) != 0)
        return 1;
    ui_init(&UU, &UP);
    ui_draw_init(h, &UP, &UU);
    ui_event(&UU, UI_EV_BTN_DOWN | UI_BTN_ENV, 0, H->ms());   /* page 5A */
    turned_ms = H->ms();
    H->log("fm1-8p cpu: with VCF page 5A redrawing every frame");
#endif
    H->log("fm1-8p cpu: sweeping 1..8 voices, then the unison burst, then the full patch");
    return 0;
}

#ifdef CPU_UI
HB_DESC = {HB_ABI, "FM1-8P CPU", init, release, note, cc, render, panel, draw, 0, 0, HB_F_SCREEN, 0};
#else
HB_DESC = {HB_ABI, "FM1-8P CPU", init, release, note, cc, render, panel, draw, 0, 0, 0, 0};
#endif
