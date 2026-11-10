/* Canary for the engine check: floating-point arithmetic, no float type. */
#include <stdint.h>
int32_t fm_canary_half(int32_t x);
int32_t fm_canary_half(int32_t x) { return (int32_t)(x * 0.5); }
