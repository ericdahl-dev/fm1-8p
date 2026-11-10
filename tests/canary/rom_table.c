/* Canary for the engine check: a 0xFFCxxxxx word in a constant table. */
#include <stdint.h>
extern const int32_t fm_canary_table[2];
const int32_t fm_canary_table[2] = { 1, (int32_t)0xFFC00080u };
