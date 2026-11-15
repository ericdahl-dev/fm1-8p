/* The voices' self-noise (#247): PG-8X 2.0.13's SELFNOISE at its default 0.5, white noise into each
 * sounding voice's filter, as PG-8X has it (measured 2026-11-15, DCO mixes at 0: 45.6 dB under a full
 * saw through the same voice, independent per voice, shaped by the VCF, HPF and VCA, nothing without a
 * note). Through the public interface only. */

static fm_engine sn_eng;
static int16_t sn_buf[RATE * 2 * 2];             /* up to 2 s of stereo */

/* RMS of the left channel over frames [from, to), dBFS */
static double sn_rms_db(const int16_t *b, uint32_t from, uint32_t to)
{
    double sum = 0;
    uint32_t i;
    for (i = from; i < to; i++) sum += (double)b[2 * i] * b[2 * i];
    return 10.0 * log10(sum / (to - from) / (32768.0 * 32768.0) + 1e-30);
}

/* one C4 (or `n` notes from C4 up by major thirds) on 6 voices with the VCA open, osc 1 at `osc1`
 * per mille and osc 2 off, 1.3 s; the RMS over 0.3..1.3 s */
static double sn_held_db(int32_t osc1, uint32_t n, int32_t cutoff_cents)
{
    uint32_t k;
    if (fm_engine_init(&sn_eng, RATE, 6) != FM_OK) return 0.0;   /* (a helper: no TT_CHECK here) */
    fm_engine_set_param(&sn_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&sn_eng, FM_PARAM_OSC1_LEVEL, osc1);
    fm_engine_set_param(&sn_eng, FM_PARAM_FILTER_CUTOFF_CENTS, cutoff_cents);
    for (k = 0; k < n; k++)
        fm_engine_note_on(&sn_eng, (uint8_t)(60 + 4 * k), 100);
    fm_engine_render(&sn_eng, sn_buf, RATE * 13 / 10);
    return sn_rms_db(sn_buf, RATE * 3 / 10, RATE * 13 / 10);
}

/* With both oscillators off a held note leaves the noise, 45.6 dB under the same voice's full saw
 * (PG-8X: -66.7 dBFS against -21.1). */
TT_TEST(self_noise_sits_45_6_db_under_a_full_saw)
{
    double saw = sn_held_db(1000, 1, FM_FILTER_OPEN_CENTS), noise = sn_held_db(0, 1, FM_FILTER_OPEN_CENTS);
    printf("    self-noise %.2f dBFS, saw %.2f: %.2f dB under (PG-8X 45.6)\n", noise, saw, saw - noise);
    TT_CHECK_NEAR(saw - noise, 45.6, 0.3);
}

/* Each voice its own noise: three held notes are 10 log10 3 = 4.8 dB over one (PG-8X: 4.9). */
TT_TEST(self_noise_is_independent_per_voice)
{
    double one = sn_held_db(0, 1, FM_FILTER_OPEN_CENTS), three = sn_held_db(0, 3, FM_FILTER_OPEN_CENTS);
    TT_CHECK_NEAR(three - one, 4.8, 0.7);
}

/* No note, no noise; and none once a note's release has ended: it is a sounding voice's. */
TT_TEST(self_noise_is_silent_without_a_note)
{
    uint32_t i, loud = 0;
    TT_CHECK_EQ(fm_engine_init(&sn_eng, RATE, 6), FM_OK);
    fm_engine_set_param(&sn_eng, FM_PARAM_OSC1_LEVEL, 0);
    fm_engine_set_param(&sn_eng, FM_PARAM_AMP_RELEASE_MS, 5);
    fm_engine_render(&sn_eng, sn_buf, RATE / 2);
    for (i = 0; i < RATE; i++) loud |= (uint32_t)(sn_buf[i] != 0);
    TT_CHECK_EQ(loud, 0u);
    fm_engine_note_on(&sn_eng, 60, 100);
    fm_engine_render(&sn_eng, sn_buf, RATE / 2);
    TT_CHECK(sn_rms_db(sn_buf, RATE / 10, RATE / 2) > -90.0);   /* the noise is there while held */
    fm_engine_note_off(&sn_eng, 60);
    fm_engine_render(&sn_eng, sn_buf, RATE / 2);
    for (i = RATE / 2; i < RATE; i++) loud |= (uint32_t)(sn_buf[i] != 0);   /* the last 0.25 s */
    TT_CHECK_EQ(loud, 0u);
}

/* energy per Hz between lo and hi Hz of the left channel's 32768 frames from 0.3 s, dB */
static double sn_band_db(double lo, double hi)
{
    static double re[32768], im[32768];
    double sum = 0;
    uint32_t i, n = 32768u, a = (uint32_t)(lo * n / RATE), b = (uint32_t)(hi * n / RATE);
    for (i = 0; i < n; i++) {
        re[i] = sn_buf[2 * (RATE * 3 / 10 + i)] * (0.5 - 0.5 * cos(2.0 * AN_PI * i / n));
        im[i] = 0;
    }
    fft(re, im, n);
    for (i = a; i < b; i++) sum += re[i] * re[i] + im[i] * im[i];
    return 10.0 * log10(sum / (b - a) + 1e-30);
}

/* The noise comes from the filter, as PG-8X's (2026-11-15): white with it open (250-500 Hz and 4-8 kHz
 * within 3 dB a Hz, as PG-8X's +2 and -1 re 1 kHz); with the lowpass at 500 Hz its total stays up (PG-8X:
 * +0.7 to +5.0 dB over open at every VCF FREQ, kT/C noise), piled up under the cutoff (100-250 Hz up
 * more than 6 dB), and above it falls by the last two stages' slope, not the four's (PG-8X: about 10 dB
 * an octave band, where its input falls 20): 4-8 kHz at least 8 dB under 2-4 kHz, and at most 16. */
TT_TEST(self_noise_comes_from_the_filter)
{
    double lo, hi, closed, lo_open, lo_closed, total_open, total_closed, mid_closed;
    total_open = sn_held_db(0, 1, FM_FILTER_OPEN_CENTS);
    lo = sn_band_db(250, 500);
    hi = sn_band_db(4000, 8000);
    lo_open = sn_band_db(100, 250);
    TT_CHECK_NEAR(hi - lo, 0.0, 3.0);
    total_closed = sn_held_db(0, 1, 7100);       /* about 500 Hz */
    closed = sn_band_db(4000, 8000);
    lo_closed = sn_band_db(100, 250);
    mid_closed = sn_band_db(2000, 4000);
    printf("    self-noise, open / cutoff 500 Hz: total %.1f / %.1f dBFS, 100-250 Hz %+.1f dB, "
           "4-8 kHz under 2-4 kHz by %.1f dB\n", total_open, total_closed, lo_closed - lo_open, mid_closed - closed);
    TT_CHECK(total_closed - total_open > 0.0 && total_closed - total_open < 5.5);
    TT_CHECK(lo_closed - lo_open > 6.0);
    TT_CHECK(mid_closed - closed > 8.0 && mid_closed - closed < 16.0);
    (void)hi;
    total_closed = sn_held_db(0, 1, 3950);       /* about 80 Hz: the total still stays up */
    printf("    self-noise, cutoff 80 Hz: total %.1f dBFS\n", total_closed);
    TT_CHECK(total_closed - total_open > 0.0 && total_closed - total_open < 5.5);
}

/* The highpass shapes it as it does the oscillators (PG-8X: HPF 3 takes 20-40 Hz down 34 dB on both). */
TT_TEST(self_noise_goes_through_the_highpass)
{
    double plain, hp;
    (void)sn_held_db(0, 1, 7100);
    plain = sn_band_db(30, 60);
    if (fm_engine_init(&sn_eng, RATE, 6) != FM_OK) return;
    fm_engine_set_param(&sn_eng, FM_PARAM_AMP_SUSTAIN, 1000);
    fm_engine_set_param(&sn_eng, FM_PARAM_OSC1_LEVEL, 0);
    fm_engine_set_param(&sn_eng, FM_PARAM_FILTER_CUTOFF_CENTS, 7100);
    fm_engine_set_param(&sn_eng, FM_PARAM_HIGHPASS_STEP, 3);
    fm_engine_note_on(&sn_eng, 60, 100);
    fm_engine_render(&sn_eng, sn_buf, RATE * 13 / 10);
    hp = sn_band_db(30, 60);
    printf("    self-noise 30-60 Hz: highpass off %.1f, step 3 %.1f dB\n", plain, hp);
    TT_CHECK(plain - hp > 15.0);
}

static void run_self_noise_tests(void)
{
    TT_RUN(self_noise_sits_45_6_db_under_a_full_saw);
    TT_RUN(self_noise_is_independent_per_voice);
    TT_RUN(self_noise_is_silent_without_a_note);
    TT_RUN(self_noise_comes_from_the_filter);
    TT_RUN(self_noise_goes_through_the_highpass);
}
