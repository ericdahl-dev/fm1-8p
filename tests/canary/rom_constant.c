/*
 * Canary for the engine check: a constant in 0xFFC00000..0xFFCFFFFF.
 * The fm1-dock build rejects words in that range because they look like
 * ROM addresses; -32767 << 7 (0xFFC00080) is an easy way to make one.
 */
#include <stdint.h>
int32_t fm_canary_rom(int32_t x);
int32_t fm_canary_rom(int32_t x) { return x ^ (int32_t)0xFFC00080u; }
