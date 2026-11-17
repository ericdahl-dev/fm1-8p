/* fm1-8p UI: how the screen shows each param, by fm_param_id (#196). GENERATED from tools/params.py
 * (DISPLAY) by tools/jx8p.py (make generate): edit the list, not this file. Included by ui.c,
 * whose value_parts formats a value by its kind. */
#ifndef UI_PARAMS_H
#define UI_PARAMS_H

typedef enum {
    UI_SHOW_NONE,
    UI_SHOW_CUTOFF,
    UI_SHOW_TENTHS,
    UI_SHOW_ABS_TENTHS,
    UI_SHOW_ABS_HUNDREDTHS,
    UI_SHOW_TENTH_SEMIS,
    UI_SHOW_SIGNED,
    UI_SHOW_PLAIN,
    UI_SHOW_NEG_NAME,
    UI_SHOW_OFF_OR_N,
    UI_SHOW_KEY_POS,
    UI_SHOW_MS_S,
    UI_SHOW_RATE,
    UI_SHOW_NAMES,
    UI_SHOW_NAMES_INNER_UNIT,
    UI_SHOW_NONZERO,
    UI_SHOW_MASK3,
} ui_show;

typedef struct {
    uint8_t kind;                       /* ui_show */
    uint8_t n;                          /* names: how many */
    const char *unit;
    const char *const *names;
} ui_display;

static const char *const UI_NAMES_VOICE_MODE[] = {"POLY", "UNISON"};
static const char *const UI_NAMES_OSC1_WAVE[] = {"SAW", "PULSE", "SQUARE", "NOISE", "SPIKE"};
static const char *const UI_NAMES_OSC1_OCTAVE[] = {"32", "16", "8", "4", "2"};
static const char *const UI_NAMES_CHORUS_MODE[] = {"OFF", "I", "II", "FAST"};
static const char *const UI_NAMES_LFO_WAVE[] = {"TRI", "SAW", "SQUARE", "RANDOM"};
static const char *const UI_NAMES_LFO_PITCH_CENTS[] = {"D1"};
static const char *const UI_NAMES_OSC_SYNC[] = {"OFF", "SYNC 1", "X-MOD", "SYNC 2"};
static const char *const UI_NAMES_HIGHPASS_STEP[] = {"FLAT", "100", "240", "560", "BOOST"};
static const char *const UI_NAMES_OSC_MIX_ENV_SOURCE[] = {"ENV1", "ENV2", "ENV1 INV", "ENV2 INV"};
static const char *const UI_NAMES_PITCH_ENV_DYNA[] = {"OFF"};
static const char *const UI_NAMES_SETTING_GLIDE[] = {"OFF", "ON"};
static const char *const UI_NAMES_AMP_GATE[] = {"ENV2", "GATE"};

static const ui_display UI_DISPLAY[FM_PARAM_COUNT] = {
    {UI_SHOW_MS_S, 0, "", 0},   /* AMP_ATTACK_MS */
    {UI_SHOW_MS_S, 0, "", 0},   /* AMP_DECAY_MS */
    {UI_SHOW_TENTHS, 0, "", 0},   /* AMP_SUSTAIN */
    {UI_SHOW_MS_S, 0, "", 0},   /* AMP_RELEASE_MS */
    {UI_SHOW_PLAIN, 0, "st", 0},   /* BEND_RANGE */
    {UI_SHOW_NONZERO, 2, "", UI_NAMES_VOICE_MODE},   /* VOICE_MODE */
    {UI_SHOW_PLAIN, 0, "ct", 0},   /* UNISON_DETUNE_CENTS */
    {UI_SHOW_NAMES, 5, "", UI_NAMES_OSC1_WAVE},   /* OSC1_WAVE */
    {UI_SHOW_PLAIN, 0, "%", 0},   /* OSC1_PULSE_WIDTH_PCT */
    {UI_SHOW_NAMES, 5, "'", UI_NAMES_OSC1_OCTAVE},   /* OSC1_OCTAVE */
    {UI_SHOW_SIGNED, 0, "ct", 0},   /* OSC1_FINE_CENTS */
    {UI_SHOW_NAMES, 5, "", UI_NAMES_OSC1_WAVE},   /* OSC2_WAVE */
    {UI_SHOW_PLAIN, 0, "%", 0},   /* OSC2_PULSE_WIDTH_PCT */
    {UI_SHOW_NAMES, 5, "'", UI_NAMES_OSC1_OCTAVE},   /* OSC2_OCTAVE */
    {UI_SHOW_SIGNED, 0, "ct", 0},   /* OSC2_FINE_CENTS */
    {UI_SHOW_ABS_TENTHS, 0, "", 0},   /* OSC2_LEVEL */
    {UI_SHOW_NAMES, 4, "", UI_NAMES_CHORUS_MODE},   /* CHORUS_MODE */
    {UI_SHOW_NAMES, 4, "", UI_NAMES_LFO_WAVE},   /* LFO_WAVE */
    {UI_SHOW_RATE, 0, "Hz", 0},   /* LFO_RATE_CENTIHZ */
    {UI_SHOW_MS_S, 0, "", 0},   /* LFO_DELAY_MS */
    {UI_SHOW_NEG_NAME, 1, "ct", UI_NAMES_LFO_PITCH_CENTS},   /* LFO_PITCH_CENTS */
    {UI_SHOW_PLAIN, 0, "%", 0},   /* AMP_VELOCITY */
    {UI_SHOW_PLAIN, 0, "%", 0},   /* LFO_PW_PCT */
    {UI_SHOW_MASK3, 4, "", UI_NAMES_OSC_SYNC},   /* OSC_SYNC */
    {UI_SHOW_TENTHS, 0, "%", 0},   /* OSC_XMOD */
    {UI_SHOW_CUTOFF, 0, "", 0},   /* FILTER_CUTOFF_CENTS */
    {UI_SHOW_TENTHS, 0, "", 0},   /* FILTER_RESONANCE */
    {UI_SHOW_TENTHS, 0, "%", 0},   /* FILTER_KEY_TRACK */
    {UI_SHOW_MS_S, 0, "", 0},   /* FILTER_ENV_ATTACK_MS */
    {UI_SHOW_MS_S, 0, "", 0},   /* FILTER_ENV_DECAY_MS */
    {UI_SHOW_TENTHS, 0, "", 0},   /* FILTER_ENV_SUSTAIN */
    {UI_SHOW_MS_S, 0, "", 0},   /* FILTER_ENV_RELEASE_MS */
    {UI_SHOW_ABS_HUNDREDTHS, 0, "st", 0},   /* FILTER_ENV_AMOUNT_CENTS */
    {UI_SHOW_NAMES_INNER_UNIT, 5, "Hz", UI_NAMES_HIGHPASS_STEP},   /* HIGHPASS_STEP */
    {UI_SHOW_SIGNED, 0, "st", 0},   /* OSC2_COARSE_SEMIS */
    {UI_SHOW_ABS_TENTHS, 0, "", 0},   /* OSC_MIX_ENV */
    {UI_SHOW_MASK3, 4, "", UI_NAMES_OSC_MIX_ENV_SOURCE},   /* OSC_MIX_ENV_SOURCE */
    {UI_SHOW_MASK3, 4, "", UI_NAMES_OSC_MIX_ENV_SOURCE},   /* FILTER_ENV_SOURCE */
    {UI_SHOW_NEG_NAME, 1, "ct", UI_NAMES_LFO_PITCH_CENTS},   /* LFO_PITCH_OSC2_CENTS */
    {UI_SHOW_TENTH_SEMIS, 0, "st", 0},   /* PITCH_ENV_OSC1_CENTS */
    {UI_SHOW_TENTH_SEMIS, 0, "st", 0},   /* PITCH_ENV_OSC2_CENTS */
    {UI_SHOW_MASK3, 4, "", UI_NAMES_OSC_MIX_ENV_SOURCE},   /* PITCH_ENV_SOURCE */
    {UI_SHOW_OFF_OR_N, 1, "", UI_NAMES_PITCH_ENV_DYNA},   /* PITCH_ENV_DYNA */
    {UI_SHOW_ABS_TENTHS, 0, "", 0},   /* OSC1_LEVEL */
    {UI_SHOW_NONE, 0, "", 0},   /* OSC_RING */
    {UI_SHOW_OFF_OR_N, 1, "", UI_NAMES_PITCH_ENV_DYNA},   /* FILTER_ENV_DYNA */
    {UI_SHOW_KEY_POS, 1, "", UI_NAMES_PITCH_ENV_DYNA},   /* AMP_KEY_FOLLOW */
    {UI_SHOW_KEY_POS, 1, "", UI_NAMES_PITCH_ENV_DYNA},   /* FILTER_ENV_KEY_FOLLOW */
    {UI_SHOW_OFF_OR_N, 1, "", UI_NAMES_PITCH_ENV_DYNA},   /* AMP_DYNA */
    {UI_SHOW_OFF_OR_N, 1, "", UI_NAMES_PITCH_ENV_DYNA},   /* OSC_MIX_ENV_DYNA */
    {UI_SHOW_TENTHS, 0, "", 0},   /* AMP_LEVEL */
    {UI_SHOW_NONZERO, 2, "", UI_NAMES_SETTING_GLIDE},   /* SETTING_GLIDE */
    {UI_SHOW_NONZERO, 2, "", UI_NAMES_AMP_GATE},   /* AMP_GATE */
    {UI_SHOW_PLAIN, 0, "ct", 0},   /* WHEEL_PITCH_CENTS */
    {UI_SHOW_PLAIN, 0, "%", 0},   /* WHEEL_PW_PCT */
    {UI_SHOW_SIGNED, 0, "st", 0},   /* OSC1_COARSE_SEMIS */
};

#endif
