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
    uint64_t d, inc, r;
    unsigned n;
    if (rate == 0 || per_hz == 0 || value == 0) return 0;
    d = (uint64_t)rate * per_hz;
    if (shift < 32 && (uint64_t)value >> (32 - shift) == 0) { /* fits 64 bits */
        inc = ((uint64_t)value << (32 + shift)) / d;
        return inc > FM_PHASE_INC_MAX ? FM_PHASE_INC_MAX : (uint32_t)inc;
    }
    /* Wider: long division, value * 2^(32 + shift) / d a bit at a time,
     * stopping at the cap. value >= 1 and d < 2^64, so by 95 doublings it
     * is past the cap: a larger shift needs no more. */
    if (value >= d) return FM_PHASE_INC_MAX; /* a cycle a step or more */
    if (shift > 63) shift = 63;
    inc = 0;
    r = value; /* r < d throughout */
    for (n = 0; n < 32 + shift; n++) {
        inc <<= 1;
        if (r >= d - r) { r -= d - r; inc |= 1; }
        else r <<= 1;
        if (inc > FM_PHASE_INC_MAX) return FM_PHASE_INC_MAX;
    }
    return (uint32_t)inc;
}
