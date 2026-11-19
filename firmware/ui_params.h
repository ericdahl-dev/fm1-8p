/* fm1-8p UI: how the screen shows each param, by fm_param_id (#196). GENERATED from tools/params.py
 * (DISPLAY) by tools/jx8p.py (make generate): edit the list, not this file. Include it from ui.c
 * only (its value_parts formats a value by its kind): the tables are static, one copy a file. */
#ifndef UI_PARAMS_H
#define UI_PARAMS_H
#include <stdint.h>
#include "../engine/fm1_8p.h"

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

static const char *const UI_NAMES_0[] = {"POLY", "UNISON"};
static const char *const UI_NAMES_1[] = {"SAW", "PULSE", "SQUARE", "NOISE", "SPIKE"};
static const char *const UI_NAMES_2[] = {"32", "16", "8", "4", "2"};
static const char *const UI_NAMES_3[] = {"OFF", "I", "II", "FAST"};
static const char *const UI_NAMES_4[] = {"TRI", "SAW", "SQUARE", "RANDOM"};
static const char *const UI_NAMES_5[] = {"D1"};
static const char *const UI_NAMES_6[] = {"OFF", "SYNC 1", "X-MOD", "SYNC 2"};
static const char *const UI_NAMES_7[] = {"FLAT", "100", "240", "560", "BOOST"};
static const char *const UI_NAMES_8[] = {"ENV1", "ENV2", "ENV1 INV", "ENV2 INV"};
static const char *const UI_NAMES_9[] = {"OFF"};
static const char *const UI_NAMES_10[] = {"OFF", "ON"};
static const char *const UI_NAMES_11[] = {"ENV2", "GATE"};

static const ui_display UI_DISPLAY[FM_PARAM_COUNT] = {
    [FM_PARAM_AMP_ATTACK_MS] = {UI_SHOW_MS_S, 0, "", 0},
    [FM_PARAM_AMP_DECAY_MS] = {UI_SHOW_MS_S, 0, "", 0},
    [FM_PARAM_AMP_SUSTAIN] = {UI_SHOW_TENTHS, 0, "", 0},
    [FM_PARAM_AMP_RELEASE_MS] = {UI_SHOW_MS_S, 0, "", 0},
    [FM_PARAM_BEND_RANGE] = {UI_SHOW_PLAIN, 0, "st", 0},
    [FM_PARAM_VOICE_MODE] = {UI_SHOW_NONZERO, 2, "", UI_NAMES_0},
    [FM_PARAM_UNISON_DETUNE_CENTS] = {UI_SHOW_PLAIN, 0, "ct", 0},
    [FM_PARAM_OSC1_WAVE] = {UI_SHOW_NAMES, 5, "", UI_NAMES_1},
    [FM_PARAM_OSC1_PULSE_WIDTH_PCT] = {UI_SHOW_PLAIN, 0, "%", 0},
    [FM_PARAM_OSC1_OCTAVE] = {UI_SHOW_NAMES, 5, "'", UI_NAMES_2},
    [FM_PARAM_OSC1_FINE_CENTS] = {UI_SHOW_SIGNED, 0, "ct", 0},
    [FM_PARAM_OSC2_WAVE] = {UI_SHOW_NAMES, 5, "", UI_NAMES_1},
    [FM_PARAM_OSC2_PULSE_WIDTH_PCT] = {UI_SHOW_PLAIN, 0, "%", 0},
    [FM_PARAM_OSC2_OCTAVE] = {UI_SHOW_NAMES, 5, "'", UI_NAMES_2},
    [FM_PARAM_OSC2_FINE_CENTS] = {UI_SHOW_SIGNED, 0, "ct", 0},
    [FM_PARAM_OSC2_LEVEL] = {UI_SHOW_ABS_TENTHS, 0, "", 0},
    [FM_PARAM_CHORUS_MODE] = {UI_SHOW_NAMES, 4, "", UI_NAMES_3},
    [FM_PARAM_LFO_WAVE] = {UI_SHOW_NAMES, 4, "", UI_NAMES_4},
    [FM_PARAM_LFO_RATE_CENTIHZ] = {UI_SHOW_RATE, 0, "Hz", 0},
    [FM_PARAM_LFO_DELAY_MS] = {UI_SHOW_MS_S, 0, "", 0},
    [FM_PARAM_LFO_PITCH_CENTS] = {UI_SHOW_NEG_NAME, 1, "ct", UI_NAMES_5},
    [FM_PARAM_AMP_VELOCITY] = {UI_SHOW_PLAIN, 0, "%", 0},
    [FM_PARAM_LFO_PW_PCT] = {UI_SHOW_PLAIN, 0, "%", 0},
    [FM_PARAM_OSC_SYNC] = {UI_SHOW_MASK3, 4, "", UI_NAMES_6},
    [FM_PARAM_OSC_XMOD] = {UI_SHOW_TENTHS, 0, "%", 0},
    [FM_PARAM_FILTER_CUTOFF_CENTS] = {UI_SHOW_CUTOFF, 0, "", 0},
    [FM_PARAM_FILTER_RESONANCE] = {UI_SHOW_TENTHS, 0, "", 0},
    [FM_PARAM_FILTER_KEY_TRACK] = {UI_SHOW_TENTHS, 0, "%", 0},
    [FM_PARAM_FILTER_ENV_ATTACK_MS] = {UI_SHOW_MS_S, 0, "", 0},
    [FM_PARAM_FILTER_ENV_DECAY_MS] = {UI_SHOW_MS_S, 0, "", 0},
    [FM_PARAM_FILTER_ENV_SUSTAIN] = {UI_SHOW_TENTHS, 0, "", 0},
    [FM_PARAM_FILTER_ENV_RELEASE_MS] = {UI_SHOW_MS_S, 0, "", 0},
    [FM_PARAM_FILTER_ENV_AMOUNT_CENTS] = {UI_SHOW_ABS_HUNDREDTHS, 0, "st", 0},
    [FM_PARAM_HIGHPASS_STEP] = {UI_SHOW_NAMES_INNER_UNIT, 5, "Hz", UI_NAMES_7},
    [FM_PARAM_OSC2_COARSE_SEMIS] = {UI_SHOW_SIGNED, 0, "st", 0},
    [FM_PARAM_OSC_MIX_ENV] = {UI_SHOW_ABS_TENTHS, 0, "", 0},
    [FM_PARAM_OSC_MIX_ENV_SOURCE] = {UI_SHOW_MASK3, 4, "", UI_NAMES_8},
    [FM_PARAM_FILTER_ENV_SOURCE] = {UI_SHOW_MASK3, 4, "", UI_NAMES_8},
    [FM_PARAM_LFO_PITCH_OSC2_CENTS] = {UI_SHOW_NEG_NAME, 1, "ct", UI_NAMES_5},
    [FM_PARAM_PITCH_ENV_OSC1_CENTS] = {UI_SHOW_TENTH_SEMIS, 0, "st", 0},
    [FM_PARAM_PITCH_ENV_OSC2_CENTS] = {UI_SHOW_TENTH_SEMIS, 0, "st", 0},
    [FM_PARAM_PITCH_ENV_SOURCE] = {UI_SHOW_MASK3, 4, "", UI_NAMES_8},
    [FM_PARAM_PITCH_ENV_DYNA] = {UI_SHOW_OFF_OR_N, 1, "", UI_NAMES_9},
    [FM_PARAM_OSC1_LEVEL] = {UI_SHOW_ABS_TENTHS, 0, "", 0},
    [FM_PARAM_OSC_RING] = {UI_SHOW_NONE, 0, "", 0},
    [FM_PARAM_FILTER_ENV_DYNA] = {UI_SHOW_OFF_OR_N, 1, "", UI_NAMES_9},
    [FM_PARAM_AMP_KEY_FOLLOW] = {UI_SHOW_KEY_POS, 1, "", UI_NAMES_9},
    [FM_PARAM_FILTER_ENV_KEY_FOLLOW] = {UI_SHOW_KEY_POS, 1, "", UI_NAMES_9},
    [FM_PARAM_AMP_DYNA] = {UI_SHOW_OFF_OR_N, 1, "", UI_NAMES_9},
    [FM_PARAM_OSC_MIX_ENV_DYNA] = {UI_SHOW_OFF_OR_N, 1, "", UI_NAMES_9},
    [FM_PARAM_AMP_LEVEL] = {UI_SHOW_TENTHS, 0, "", 0},
    [FM_PARAM_SETTING_GLIDE] = {UI_SHOW_NONZERO, 2, "", UI_NAMES_10},
    [FM_PARAM_AMP_GATE] = {UI_SHOW_NONZERO, 2, "", UI_NAMES_11},
    [FM_PARAM_WHEEL_PITCH_CENTS] = {UI_SHOW_PLAIN, 0, "ct", 0},
    [FM_PARAM_WHEEL_PW_PCT] = {UI_SHOW_PLAIN, 0, "%", 0},
    [FM_PARAM_OSC1_COARSE_SEMIS] = {UI_SHOW_SIGNED, 0, "st", 0},
    [FM_PARAM_AT_VIB_CENTS] = {UI_SHOW_PLAIN, 0, "ct", 0},
    [FM_PARAM_AT_BRI_CENTS] = {UI_SHOW_PLAIN, 0, "ct", 0},
    [FM_PARAM_AT_VOL_PCT] = {UI_SHOW_PLAIN, 0, "%", 0},
};

#endif
