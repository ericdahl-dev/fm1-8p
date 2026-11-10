/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Canary: 300 KiB of static RAM, more than a module's 272 KiB. fm1-dock must refuse to build it. */
#include "hb_module.h"

static volatile uint8_t big[300u * 1024u];

static int init(const hb_host_t *h) { (void)h; return 0; }
static void release(void) {}
static void note(uint32_t ch, uint32_t key, uint32_t vel) { (void)ch; (void)key; (void)vel; }
static void cc(uint32_t ch, uint32_t num, uint32_t val) { (void)ch; (void)num; (void)val; }
static void render(int32_t *out, uint32_t n)
{
    (void)out;
    big[n % sizeof big] = 1;
}
static void ui(uint32_t ev, int32_t arg) { (void)ev; (void)arg; }
static void draw(uint32_t full) { (void)full; }

HB_DESC = {HB_ABI, "canary ram", init, release, note, cc, render, ui, draw, 0, 0, 0, 0};
