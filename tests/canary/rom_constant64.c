/*
 * Canary for the engine check: a 64-bit constant whose top word is in
 * 0xFFC00000..0xFFCFFFFF. On the 32-bit FM-1 that word lands in the image
 * on its own; arm64 builds it with movk xN, #0xffc0, lsl #48, x86-64 with
 * movabs.
 */
#include <stdint.h>
uint64_t fm_canary_rom64(uint64_t x);
uint64_t fm_canary_rom64(uint64_t x) { return x ^ 0xFFC0000000000080u; }
