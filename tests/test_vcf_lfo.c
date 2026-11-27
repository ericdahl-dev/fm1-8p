/* The VCF LFO (#172): the LFO moves the cutoff by lfo_cutoff_cents each way at full, as the JX-8P's
 * VCF LFO does in PG-8X, through the engine's public interface. */

#define VCL_RATE 44100u

static fm_engine vcl_eng;
static int16_t vcl_buf[VCL_RATE * 2];

/* The cutoff's lowest and highest over `ms`, read every LFO tick, of a held note on a square LFO at
 * 2 Hz: full each way, no ramp to read past. */
static void vcl_swing(int32_t cents, uint32_t ms, int32_t *lo, int32_t *hi)
{
    uint32_t t;
    TT_CHECK_EQ(fm_engine_init(&vcl_eng, VCL_RATE, 1), FM_OK);
    fm_engine_set_param(&vcl_eng, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    fm_engine_set_param(&vcl_eng, FM_PARAM_LFO_WAVE, FM_LFO_SQUARE);
    fm_engine_set_param(&vcl_eng, FM_PARAM_LFO_RATE_CENTIHZ, 200);
    fm_engine_set_param(&vcl_eng, FM_PARAM_LFO_DELAY_MS, 0);
    fm_engine_set_param(&vcl_eng, FM_PARAM_LFO_CUTOFF_CENTS, cents);
    fm_engine_note_on(&vcl_eng, 60, 100);
    *lo = INT32_MAX;
    *hi = INT32_MIN;
    for (t = 0; t < VCL_RATE * ms / 1000u; t += FM_LFO_TICK_FRAMES) {
        int32_t c;
        fm_engine_render(&vcl_eng, vcl_buf, FM_LFO_TICK_FRAMES);
        c = fm_vfilter_cutoff(fm_voice_filter(&vcl_eng.voices[0]));
        if (c < *lo) *lo = c;
        if (c > *hi) *hi = c;
    }
}

/* Depth 0 (the default): the cutoff holds wherever the patch puts it. */
TT_TEST(vcf_lfo_at_zero_leaves_the_cutoff)
{
    int32_t lo, hi;
    TT_CHECK_EQ(fm_param_info_get(FM_PARAM_LFO_CUTOFF_CENTS)->def, 0);
    vcl_swing(0, 1000, &lo, &hi);
    TT_CHECK_EQ(lo, hi);
}

/* The cutoff a held note settles at with the filter envelope at full (attack 0, sustain full) and
 * `amount` cents of it: the engine's one unit for moving the cutoff. */
static int32_t vcl_env_cutoff(int32_t amount)
{
    if (fm_engine_init(&vcl_eng, VCL_RATE, 1) != FM_OK) return INT32_MIN;
    fm_engine_set_param(&vcl_eng, FM_PARAM_FILTER_CUTOFF_CENTS, 6000);
    fm_engine_set_param(&vcl_eng, FM_PARAM_FILTER_ENV_ATTACK_MS, 0);
    fm_engine_set_param(&vcl_eng, FM_PARAM_FILTER_ENV_SUSTAIN, 1000);
    fm_engine_set_param(&vcl_eng, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, amount);
    fm_engine_note_on(&vcl_eng, 60, 100);
    fm_engine_render(&vcl_eng, vcl_buf, VCL_RATE / 10);
    return fm_vfilter_cutoff(fm_voice_filter(&vcl_eng.voices[0]));
}

/* At full the LFO moves the cutoff by its depth each way, in the units the filter envelope's amount
 * moves it: 1200 cents of LFO up is the cutoff 1200 cents of envelope puts it at, and down, -1200's. */
TT_TEST(vcf_lfo_swings_the_cutoff_by_its_depth_each_way)
{
    int32_t lo, hi, up = vcl_env_cutoff(1200), down = vcl_env_cutoff(-1200);
    vcl_swing(1200, 1000, &lo, &hi);
    printf("    VCF LFO 1200 ct: cutoff %d .. %d cents (envelope +-1200: %d .. %d)\n", (int)lo, (int)hi,
           (int)down, (int)up);
    TT_CHECK_EQ(hi, up);
    TT_CHECK_EQ(lo, down);
}

static void run_vcf_lfo_tests(void)
{
    TT_RUN(vcf_lfo_at_zero_leaves_the_cutoff);
    TT_RUN(vcf_lfo_swings_the_cutoff_by_its_depth_each_way);
}
