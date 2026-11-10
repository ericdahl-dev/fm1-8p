/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Canary: reads the chip ROM (0xFFC00000..0xFFCFFFFF). fm1-dock must refuse to build it. */
#include "hb_module.h"

static volatile uint32_t sink;

static int init(const hb_host_t *h) { (void)h; return 0; }
static void release(void) {}
static void note(uint32_t ch, uint32_t key, uint32_t vel) { (void)ch; (void)key; (void)vel; }
static void cc(uint32_t ch, uint32_t num, uint32_t val) { (void)ch; (void)num; (void)val; }
static void render(int32_t *out, uint32_t n)
{
    (void)out;
    (void)n;
    sink = *(volatile const uint32_t *)0xFFC01000u;
}
static void ui(uint32_t ev, int32_t arg) { (void)ev; (void)arg; }
static void draw(uint32_t full) { (void)full; }

HB_DESC = {HB_ABI, "canary rom", init, release, note, cc, render, ui, draw, 0, 0, 0, 0};
