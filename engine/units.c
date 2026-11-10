/* Units: see units.h. */
#include "units.h"

uint32_t fm_ms_to_samples(int32_t ms, uint32_t rate)
{
    return (uint32_t)(((uint64_t)(uint32_t)ms * rate) / 1000u);
}

int32_t fm_permille_to_q30(int32_t permille)
{
    return (int32_t)((int64_t)permille * (1 << 30) / 1000);
}
