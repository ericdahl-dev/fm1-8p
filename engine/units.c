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

uint64_t fm_us_to_samples_q16(uint32_t us, uint32_t rate)
{
    return ((uint64_t)us * rate << 16) / 1000000u;
}

uint32_t fm_rate_to_phase_inc(uint32_t value, uint32_t per_hz, uint32_t rate,
                              unsigned shift)
{
    uint64_t inc;
    if (rate == 0 || per_hz == 0) return 0;
    inc = ((uint64_t)value << (32 + shift)) / ((uint64_t)rate * per_hz);
    return inc > FM_PHASE_INC_MAX ? FM_PHASE_INC_MAX : (uint32_t)inc;
}
