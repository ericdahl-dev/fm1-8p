/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Canary: float arithmetic in the render path. The compiler and linker accept it (soft-float helpers from
 * the toolchain's compiler_rt); fm1-build.sh's symbol check must refuse it. */
#include "hb_module.h"

static volatile float gain = 0.5f;

static int init(const hb_host_t *h) { (void)h; return 0; }
static void release(void) {}
static void note(uint32_t ch, uint32_t key, uint32_t vel) { (void)ch; (void)key; (void)vel; }
static void cc(uint32_t ch, uint32_t num, uint32_t val) { (void)ch; (void)num; (void)val; }
static void render(int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        out[i] = (int32_t)((float)out[i] * gain / 3.0f);
}
static void ui(uint32_t ev, int32_t arg) { (void)ev; (void)arg; }
static void draw(uint32_t full) { (void)full; }

HB_DESC = {HB_ABI, "canary float", init, release, note, cc, render, ui, draw, 0, 0, 0, 0};
