/* Canary for the engine check: a floating-point type, even if unused. */
#include <stdint.h>
int32_t fm_canary_type(int32_t x);
int32_t fm_canary_type(int32_t x) { float unused = 0; (void)unused; return x; }
