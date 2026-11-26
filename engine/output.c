/*
 * output.c - the engine's output stage (#342): makeup, soft clip, output
 * gain and clamp, from the mixed Q8.24 sum to int16.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "output.h"

/*
 * The source lowpass's weight by sample rate (output.h): the two fitted points, 44.1 and 48 kHz, and a line
 * through the points either side: 1.0 (a wire) at 32 kHz and below, 0.5 (the two taps' most) at 88.2 kHz and
 * above. Each slope is Q15 a Hz in Q16, so no divide; the fitted rates return their constants exactly.
 */
int32_t fm_out_source_lp_weight(uint32_t sample_rate)
{
    int32_t hz = (int32_t)sample_rate;
    if (hz <= 32000) return 32768;
    if (hz >= 88200) return 16384;
    if (hz < 44100)  /* 32768 at 32 kHz to 22774: -0.82595 a Hz */
        return FM_OUT_SOURCE_LP_A_44K1 + (int32_t)(((int64_t)(44100 - hz) * 54130) >> 16);
    if (hz <= 48000) /* 22774 to 21381 at 48 kHz: -0.35718 a Hz */
        return hz == 48000 ? FM_OUT_SOURCE_LP_A_48K
                           : FM_OUT_SOURCE_LP_A_44K1 - (int32_t)(((int64_t)(hz - 44100) * 23408) >> 16);
    /* 21381 to 16384 at 88.2 kHz: -0.12430 a Hz */
    return FM_OUT_SOURCE_LP_A_48K - (int32_t)(((int64_t)(hz - 48000) * 8146) >> 16);
}

void fm_out_mix_block(int32_t *mix, uint32_t n, int32_t gain, int32_t a, int32_t *last)
{
    int32_t prev = *last;
    uint32_t i;
    for (i = 0; i < n; i++)
        mix[i] = fm_out_source_lp((int32_t)(((int64_t)mix[i] * gain) >> 30), a, &prev);
    *last = prev;
}

/*
 * Q8.24 to int16 with +6 dB of output makeup (#24), then a soft clip.
 * The mix plays 3.17 dB over the old -12 dB of headroom, at PG-8X 2.0.13's
 * level (#102); the makeup gives 6 dB back. Up to the knee at -2.85 dBFS
 * it is a pure gain: the knee sat at -6 dBFS and moved up by the same
 * 3.17 dB, since PG-8X stays linear to -0.5 dBFS (a six-note two-DCO
 * chord), so what was linear here stays linear. Above it, d past the knee
 * becomes knee + h d / (h + d), h = full scale - knee: slope 1 at the
 * knee, monotonic, and short of full scale. That curve is read from a
 * table (tools/gen_softclip_table.py), linearly interpolated, so the render
 * path has no divide; past d = 2.0 it holds at 0.966 of full scale.
 */
#define OUT_KNEE 12083420 /* 0.7202 of full scale (-2.85 dBFS), after the makeup, in Q8.24 */
#define OUT_CLIP_SHIFT 18  /* table step: 2^18 in Q8.24 */
static const int32_t out_clip[129] = {
    0, 248278, 471610, 673576, 857103, 1024604,
    1178092, 1319254, 1449518, 1570099, 1682038, 1786232,
    1883457, 1974390, 2059624, 2139677, 2215007, 2286022,
    2353081, 2416505, 2476584, 2533573, 2587707, 2639194,
    2688223, 2734967, 2779582, 2822210, 2862980, 2902012,
    2939415, 2975288, 3009723, 3042806, 3074613, 3105218,
    3134688, 3163084, 3190465, 3216883, 3242389, 3267028,
    3290845, 3313880, 3336171, 3357753, 3378659, 3398922,
    3418569, 3437629, 3456128, 3474090, 3491538, 3508494,
    3524978, 3541010, 3556608, 3571790, 3586571, 3600969,
    3614996, 3628668, 3641998, 3654998, 3667681, 3680058,
    3692140, 3703937, 3715460, 3726718, 3737719, 3748473,
    3758987, 3769271, 3779330, 3789173, 3798807, 3808237,
    3817471, 3826514, 3835372, 3844051, 3852557, 3860894,
    3869067, 3877081, 3884942, 3892652, 3900217, 3907640,
    3914925, 3922077, 3929099, 3935994, 3942766, 3949418,
    3955953, 3962374, 3968685, 3974887, 3980984, 3986979,
    3992874, 3998671, 4004373, 4009983, 4015502, 4020932,
    4026276, 4031536, 4036714, 4041811, 4046830, 4051773,
    4056640, 4061434, 4066156, 4070809, 4075393, 4079910,
    4084361, 4088749, 4093073, 4097337, 4101540, 4105684,
    4109771, 4113801, 4117777,
};

static int16_t to_int16(int32_t q24)
{
    int64_t u, a, d;
    uint32_t k;
    int32_t s;
    /* below the knee (the common case): a pure gain, one unsigned compare
     * in 32 bits (|q24| <= knee / 2 before the makeup) */
    if ((uint32_t)q24 + (uint32_t)(OUT_KNEE / 2) <= (uint32_t)OUT_KNEE)
        return (int16_t)(q24 >> 8);
    u = (int64_t)q24 * 2;
    a = u < 0 ? -u : u;
    d = a - OUT_KNEE;
    k = (uint32_t)(d >> OUT_CLIP_SHIFT);
    if (k >= 128) {
        a = OUT_KNEE + out_clip[128];
    } else {
        int64_t frac = d & ((1 << OUT_CLIP_SHIFT) - 1);
        a = OUT_KNEE + out_clip[k] +
            (((int64_t)(out_clip[k + 1] - out_clip[k]) * frac) >> OUT_CLIP_SHIFT);
    }
    u = u < 0 ? -a : a;
    s = (int32_t)(u >> 9);
    if (s > 32767) return 32767;
    if (s < -32768) return -32768;
    return (int16_t)s;
}

/*
 * The output gain: `shift` doublings of the soft clip's int16, clamped at
 * +-32767 (it saturates, never wraps): the FM-1 module's gain
 * (FM_OUT_SHIFT_DEVICE), after the soft clip, as play_render applied it.
 *
 * At shift 1 and up the soft clip never shapes a sample: whatever reaches
 * its knee (23600, -2.85 dBFS) is at least 47200 after one doubling, past
 * the clamp. So there the stage is the makeup's plain shift, doubled and
 * clamped: clamp((q24 >> 8) * 2^shift), the same sample for sample with no
 * table and no 64-bit math. At shift 0 the soft clip is the output as it is
 * (it holds at 31642, so no clamp acts).
 */
static int16_t gained(int32_t q24, uint8_t shift)
{
    int32_t v = (q24 >> 8) * (1 << shift);   /* |q24 >> 8| < 2^23: no overflow at shift 4 */
    return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
}

void fm_out_block(int16_t *out, const int32_t *left, const int32_t *right, uint32_t n, uint8_t shift)
{
    uint32_t i;
    if (shift == 0) {           /* the engine's own level: the soft clip */
        if (right) {
            for (i = 0; i < n; i++) {
                out[2 * i] = to_int16(left[i]);
                out[2 * i + 1] = to_int16(right[i]);
            }
        } else {
            for (i = 0; i < n; i++) {
                int16_t s = to_int16(left[i]);
                out[2 * i] = s;
                out[2 * i + 1] = s;
            }
        }
    } else if (right) {         /* an output gain: past the knee is past the clamp */
        for (i = 0; i < n; i++) {
            out[2 * i] = gained(left[i], shift);
            out[2 * i + 1] = gained(right[i], shift);
        }
    } else {
        for (i = 0; i < n; i++) {
            int16_t s = gained(left[i], shift);
            out[2 * i] = s;
            out[2 * i + 1] = s;
        }
    }
}
