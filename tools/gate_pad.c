/* gate_pad.c - never-run code ahead of the engine, for tools/gate_cpu.sh's layouts: it moves every engine
 * function, so a row's time can be read over several layouts, not one (#304). The size below is the default;
 * gate_cpu.sh writes each layout's own into the copy it stages. */
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
