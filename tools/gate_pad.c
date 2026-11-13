/* gate_pad.c - 3 KB of code that is linked but never runs, ahead of the engine (tools/gate_cpu.sh's
 * "pad" layout): it moves every engine function, so a CPU change that is only alignment shows up. */
#include <stdint.h>
int32_t fm1_8p_gate_pad(int32_t x);
static void __attribute__((noinline)) fm1_8p_gate_pad_blob(void)
{
    __asm__ volatile(".space 3072");
}
int32_t fm1_8p_gate_pad(int32_t x)
{
    if (x == 12345) fm1_8p_gate_pad_blob(); /* never: init passes 3 */
    return x + 1;
}
