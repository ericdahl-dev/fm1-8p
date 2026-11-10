/*
 * Canary for the engine check: dynamic allocation. malloc is declared by
 * hand because <stdlib.h> mentions `double` (strtod), which layer 2 of
 * the check would reject before the symbol check is reached.
 */
extern void *malloc(__SIZE_TYPE__ size);
void *fm_canary_alloc(void);
void *fm_canary_alloc(void) { return malloc(16); }
