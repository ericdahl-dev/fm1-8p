/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_map.h"
#include "../engine/fm1_8p.h"

/* visual.md: Membrane */
const ui_theme UI_MEMBRANE = {
    /* the JX-8P's and PG-800's own colors (Eric, 2026-11-14): the body's neutral charcoal gray, off-white
     * lettering, and the buttons' red, amber, cream, gray and sky blue as the sections, white-balanced
     * from a photo (its room light made the body look brown) */
    .bg = 0x2124u, .surface = 0x2965u, .surface_hi = 0x39C7u, .line = 0x528Au, .text = 0xF79Du,
    .dim = 0xBDD6u, .ink = 0x10A2u, .window = 0x0841u, .led = 0xFB47u,
    .section = {0xFB47u, 0xFE2Cu, 0xEF19u, 0x6D3Fu, 0xFDD1u, 0xB5B6u, 0xAE7Fu, 0xFE8Du, 0xFB47u, 0x6D3Fu, 0xF79Du},
    .ghost = 26u,                       /* 10 % */
    .chip = 1u,
    .meter_segments = 0u,
    .focus = {256u, 256u, 0u},          /* a 2 px frame */
    .hatch = 0u,
    .sun = {0xFE88u, 0xFD87u, 0xFC67u, 0xFB49u, 0xF24Eu, 0xD1D2u},
    .art = UI_ART_BADGE,
};

/* visual.md: Night Panel */
const ui_theme UI_NIGHT_PANEL = {
    .bg = 0x0863u, .surface = 0x18C5u, .surface_hi = 0x2107u, .line = 0x2969u, .text = 0xEF7Fu,
    .dim = 0x9CB7u, .ink = 0x0863u, .window = 0x0021u, .led = 0u,
    .section = {0xFAF4u, 0xFC47u, 0xDEBEu, 0x2EDFu, 0xFE07u, 0x7F8Fu, 0xB3FFu, 0x3D1Fu, 0xF9FBu, 0xDEBEu, 0xF9FBu},
    .ghost = 20u,                       /* 8 % */
    .chip = 0u,                         /* the name under a 3 px band */
    .meter_segments = 8u,               /* an LED ladder */
    .focus = {256u, 115u, 51u},         /* a glow: 1.0, 0.45, 0.2 */
    .hatch = 41u,                       /* 16 % */
    .sun = {0xFEC9u, 0xFD88u, 0xFC48u, 0xFAECu, 0xF1F2u, 0xC198u},
    .led_section = 1u,
    .art = UI_ART_SUN,
    .grid = 0x5953u, .horizon = 0xF9FBu,
};

/* interaction.md 3: the pages, as the JX-8P's panel has its sections */
const ui_page_info UI_PAGE_INFO[UI_PAGES] = {
    {"1A", UI_SEC_DCO1, "DCO-1"}, {"1B", UI_SEC_DCO1, "DCO-1"}, {"2A", UI_SEC_DCO2, "DCO-2"},
    {"2B", UI_SEC_DCO2, "DCO-2"}, {"3A", UI_SEC_DCO1, "DCO ENV"}, {"4A", UI_SEC_MIX, "MIXER"},
    {"4B", UI_SEC_MIX, "MIXER"}, {"5A", UI_SEC_VCF, "VCF"}, {"5B", UI_SEC_VCF, "VCF"},
    {"6A", UI_SEC_VCA, "VCA"}, {"6B", UI_SEC_VCA, "VCA"}, {"7A", UI_SEC_LFO, "LFO"},
    {"8A", UI_SEC_VCF_ENV, "ENV-1"}, {"8B", UI_SEC_VCF_ENV, "ENV-1"}, {"9A", UI_SEC_VCA, "ENV-2"},
    {"9B", UI_SEC_VCA, "ENV-2"}, {"0A", UI_SEC_CTRL, "AFTERTOUCH"}, {"0B", UI_SEC_CTRL, "CONTROL"},
};

/* HPF as the JX-8P's panel has it (#138): BOOST, FLAT, 240 Hz, 560 Hz (engine steps 4, 0, 2, 3), with
 * the engine's own 100 Hz (1) in its place between, shown but not a knob position */
static const int16_t HPF_ORDER[5] = {4, 0, 1, 2, 3};
#define HPF_STOPS 0x1Bu                     /* BOOST, FLAT, 240, 560 */

/* CROSS MOD (tools/jx8p.py): OFF, SYNC 1 (sync), SYNC 2 (sync and ring), X-MOD (ring), as
 * sync | ring << 1 */
static const int16_t XMOD_ORDER[4] = {0, 1, 3, 2};
/* RANGE, 32' (ours) to 2'; WAVE, SAW, PULSE, SQUARE, NOISE (the engine's order is the panel's) */
static const int16_t RANGE_ORDER[5] = {-2, -1, 0, 1, 2};
static const int16_t WAVE_ORDER[4] = {0, 1, 2, 3};
/* DYNAMICS: OFF, 1, 2, 3 */
static const int16_t DYNA_ORDER[4] = {0, 1, 2, 3};
/* LFO WAVE: RANDOM, SQUARE, TRI (the panel's), and our SAW; GLIDE and VOICE: two positions */
static const int16_t LFO_ORDER[4] = {FM_LFO_RANDOM, FM_LFO_SQUARE, FM_LFO_TRIANGLE, FM_LFO_SAW};
static const int16_t TWO_ORDER[2] = {0, 1};
/* VCA MODE: ENV2, GATE (#63); CHORUS: OFF, I, II, and our FAST; ENV KEY: OFF, 1, 2, 3 (260 per mille) */
static const int16_t GATE_ORDER[2] = {0, 1};
static const int16_t CHORUS_ORDER[4] = {0, 1, 2, 3};
static const int16_t KEY_ORDER[4] = {0, 260, 520, 780};
/* ENV MODE (tools/jx8p.py env_mode): ENV2 INV, ENV2, ENV1 INV, ENV1, as source | inverted << 1 */
static const int16_t MODE_ORDER[4] = {3, 1, 2, 0};
#define CHOICE(n, l, f, id, o) {n, l, f, id, UI_LIVE, 1, 1, o, sizeof o / 2u, (1u << sizeof o / 2u) - 1u, 0, 0, 0}
#define TIME(n, l, f, id) {n, l, f, id, UI_LIVE, 1, 4, 0, 0, 0, 0, UI_HOW_TIME, 0}
/* an ENV MODE: source id, depths d1 and d2 */
#define MODE(n, id, d1, d2) {n, "MODE", "ENV MODE", id, UI_LIVE, 1, 1, MODE_ORDER, 4, 0x0Fu, d1, UI_HOW_MODE, d2}

#define LIVE(n, l, f, id, st, fa) {n, l, f, id, UI_LIVE, st, fa, 0, 0, 0, 0, 0, 0}
#define SIZE(n, l, f, id, st, fa) {n, l, f, id, UI_LIVE, st, fa, 0, 0, 0, 0, UI_HOW_SIZE, 0}
#define PEND(n, l, f) {n, l, f, 0, UI_PENDING, 0, 0, 0, 0, 0, 0, 0, 0}
#define PAGE(p) [4 * (p)] =

/* interaction.md 3, page by page (a page left out is empty, and the rings skip it) */
const ui_slot UI_SLOTS[UI_PAGES * 4u] = {
    PAGE(0) CHOICE(11, "RANGE", "RANGE", FM_PARAM_OSC1_OCTAVE, RANGE_ORDER),
    CHOICE(12, "WAVE", "WAVEFORM", FM_PARAM_OSC1_WAVE, WAVE_ORDER),
    PEND(13, "TUNE", "TUNE"),                                      /* #158 */
    LIVE(14, "LFO", "LFO DEPTH", FM_PARAM_LFO_PITCH_CENTS, 1, 10),
    PAGE(1) SIZE(15, "ENV", "ENV DEPTH", FM_PARAM_PITCH_ENV_OSC1_CENTS, 10, 100),
    LIVE(16, "FINE", "FINE TUNE", FM_PARAM_OSC1_FINE_CENTS, 1, 10),
    LIVE(17, "WIDTH", "PULSE WIDTH", FM_PARAM_OSC1_PULSE_WIDTH_PCT, 1, 5),
    PAGE(2) CHOICE(21, "RANGE", "RANGE", FM_PARAM_OSC2_OCTAVE, RANGE_ORDER),
    CHOICE(22, "WAVE", "WAVEFORM", FM_PARAM_OSC2_WAVE, WAVE_ORDER),
    {23, "X-MOD", "CROSS MOD", FM_PARAM_OSC_SYNC, UI_LIVE, 1, 1, XMOD_ORDER, 4, 0x0Fu, FM_PARAM_OSC_RING, UI_HOW_PAIR, 0},
    LIVE(24, "TUNE", "TUNE", FM_PARAM_OSC2_COARSE_SEMIS, 1, 1),
    PAGE(3) LIVE(25, "FINE", "FINE TUNE", FM_PARAM_OSC2_FINE_CENTS, 1, 10),
    LIVE(26, "LFO", "LFO DEPTH", FM_PARAM_LFO_PITCH_OSC2_CENTS, 1, 10),
    SIZE(27, "ENV", "ENV DEPTH", FM_PARAM_PITCH_ENV_OSC2_CENTS, 10, 100),
    LIVE(28, "WIDTH", "PULSE WIDTH", FM_PARAM_OSC2_PULSE_WIDTH_PCT, 1, 5),
    PAGE(4) CHOICE(31, "DYNA", "DYNAMICS", FM_PARAM_PITCH_ENV_DYNA, DYNA_ORDER),
    MODE(32, FM_PARAM_PITCH_ENV_SOURCE, FM_PARAM_PITCH_ENV_OSC1_CENTS, FM_PARAM_PITCH_ENV_OSC2_CENTS),
    LIVE(33, "XM AMT", "X-MOD AMOUNT", FM_PARAM_OSC_XMOD, 10, 50),     /* ours */
    PAGE(5) LIVE(41, "DCO-1", "DCO-1 LEVEL", FM_PARAM_OSC1_LEVEL, 10, 50),
    LIVE(42, "DCO-2", "DCO-2 LEVEL", FM_PARAM_OSC2_LEVEL, 10, 50),
    SIZE(43, "ENV", "ENV DEPTH", FM_PARAM_OSC_MIX_ENV, 10, 50),
    CHOICE(44, "DYNA", "DYNAMICS", FM_PARAM_OSC_MIX_ENV_DYNA, DYNA_ORDER),
    PAGE(6) MODE(45, FM_PARAM_OSC_MIX_ENV_SOURCE, FM_PARAM_OSC_MIX_ENV, FM_PARAM_OSC_MIX_ENV),
    PAGE(9) LIVE(61, "LEVEL", "VCA LEVEL", FM_PARAM_AMP_LEVEL, 10, 50),
    CHOICE(62, "MODE", "VCA MODE", FM_PARAM_AMP_GATE, GATE_ORDER),
    CHOICE(63, "DYNA", "DYNAMICS", FM_PARAM_AMP_DYNA, DYNA_ORDER),
    CHOICE(64, "CHORUS", "CHORUS", FM_PARAM_CHORUS_MODE, CHORUS_ORDER),
    PAGE(10) LIVE(65, "VEL", "VELOCITY", FM_PARAM_AMP_VELOCITY, 1, 5),               /* ours */
    PAGE(12) TIME(81, "ATTACK", "ATTACK", FM_PARAM_FILTER_ENV_ATTACK_MS),
    TIME(82, "DECAY", "DECAY", FM_PARAM_FILTER_ENV_DECAY_MS),
    LIVE(83, "SUSTAIN", "SUSTAIN", FM_PARAM_FILTER_ENV_SUSTAIN, 10, 50),
    TIME(84, "RELEASE", "RELEASE", FM_PARAM_FILTER_ENV_RELEASE_MS),
    PAGE(13) CHOICE(85, "KEY", "KEY FOLLOW", FM_PARAM_FILTER_ENV_KEY_FOLLOW, KEY_ORDER),
    PAGE(14) TIME(91, "ATTACK", "ATTACK", FM_PARAM_AMP_ATTACK_MS),
    TIME(92, "DECAY", "DECAY", FM_PARAM_AMP_DECAY_MS),
    LIVE(93, "SUSTAIN", "SUSTAIN", FM_PARAM_AMP_SUSTAIN, 10, 50),
    TIME(94, "RELEASE", "RELEASE", FM_PARAM_AMP_RELEASE_MS),
    PAGE(15) CHOICE(95, "KEY", "KEY FOLLOW", FM_PARAM_AMP_KEY_FOLLOW, KEY_ORDER),
    PAGE(11) CHOICE(71, "WAVE", "WAVEFORM", FM_PARAM_LFO_WAVE, LFO_ORDER),
    TIME(72, "DELAY", "DELAY TIME", FM_PARAM_LFO_DELAY_MS),
    {73, "RATE", "RATE", FM_PARAM_LFO_RATE_CENTIHZ, UI_LIVE, 1, 4, 0, 0, 0, 0, UI_HOW_RATE, 0},
    LIVE(74, "PW", "PW DEPTH", FM_PARAM_LFO_PW_PCT, 1, 5),                 /* ours */
    PAGE(16) PEND(1, "AT VIB", "VIBRATO"),                              /* #61 */
    PEND(2, "AT BRI", "TO CUTOFF"),
    PEND(3, "AT VOL", "VOLUME"),
    CHOICE(4, "GLIDE", "GLIDE", FM_PARAM_SETTING_GLIDE, TWO_ORDER), /* ours (Q4) */
    PAGE(17) LIVE(5, "BEND", "BEND RANGE", FM_PARAM_BEND_RANGE, 1, 2),
    PEND(6, "PORTA", "PORTAMENTO"),                                        /* #62 */
    CHOICE(7, "VOICE", "VOICE MODE", FM_PARAM_VOICE_MODE, TWO_ORDER),       /* ours */
    LIVE(8, "DETUNE", "DETUNE", FM_PARAM_UNISON_DETUNE_CENTS, 1, 10),   /* ours */
    PAGE(7) {51, "HPF", "HIGHPASS", FM_PARAM_HIGHPASS_STEP, UI_LIVE, 1, 1, HPF_ORDER, 5, HPF_STOPS, 0, 0, 0},
    LIVE(52, "CUTOFF", "CUTOFF", FM_PARAM_FILTER_CUTOFF_CENTS, 100, 400),
    LIVE(53, "RES", "RESONANCE", FM_PARAM_FILTER_RESONANCE, 10, 50),
    PEND(54, "LFO", "LFO DEPTH"),                                  /* #172 */
    PAGE(8) SIZE(55, "ENV", "ENV DEPTH", FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 100, 600),
    LIVE(56, "KEY", "KEY FOLLOW", FM_PARAM_FILTER_KEY_TRACK, 10, 100),
    CHOICE(57, "DYNA", "DYNAMICS", FM_PARAM_FILTER_ENV_DYNA, DYNA_ORDER),
    MODE(58, FM_PARAM_FILTER_ENV_SOURCE, FM_PARAM_FILTER_ENV_AMOUNT_CENTS, FM_PARAM_FILTER_ENV_AMOUNT_CENTS),
};

/* interaction.md 4 */
const uint8_t UI_RING_SEL[] = {0, 1, 2, 3, 4, 5, 6, UI_RING_END};
const uint8_t UI_RING_ENV[] = {7, 8, 12, 13, 14, 15, UI_RING_END};
const uint8_t UI_RING_FX[] = {9, 10, UI_RING_END};
const uint8_t UI_RING_LFO[] = {11, 16, 17, UI_RING_END};

/* interaction.md, units: the time table, 0 to 32000 ms. 0..22 as listed, then each the readable
 * value nearest 7 % up (whole ms below 100, 5 ms below 1 s, 50 ms below 10 s, then 500 ms) */
const uint16_t UI_TIMES[UI_TIME_COUNT] = {
    0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 18, 20, 22,
    24, 26, 28, 30, 32, 34, 36, 39, 42, 45, 48, 51, 55, 59,
    63, 67, 72, 77, 82, 88, 94, 100, 105, 110, 120, 130, 140, 150,
    160, 170, 180, 195, 210, 225, 240, 255, 275, 295, 315, 335, 360, 385,
    410, 440, 470, 505, 540, 580, 620, 665, 710, 760, 815, 870, 930, 995,
    1050, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1950, 2100, 2250, 2400, 2550,
    2750, 2950, 3150, 3350, 3600, 3850, 4100, 4400, 4700, 5050, 5400, 5800, 6200, 6650,
    7100, 7600, 8150, 8700, 9300, 9950, 10500, 11000, 12000, 13000, 14000, 15000, 16000, 17000,
    18000, 19500, 21000, 22500, 24000, 25500, 27500, 29500, 31500, 32000,
};

/* interaction.md, units: the LFO rate table, centihertz, 0.01 to 30.0 Hz. Every 0.01 Hz to 0.17, then
 * the readable value nearest 6 % up (0.01 Hz below 1 Hz, 0.05 below 10 Hz, then 0.1) */
const uint16_t UI_RATES[UI_RATE_COUNT] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 28, 30,
    32, 34, 36, 38, 40, 42, 45, 48, 51, 54, 57, 60, 64, 68,
    72, 76, 81, 86, 91, 96, 100, 105, 110, 115, 120, 125, 130, 140,
    150, 160, 170, 180, 190, 200, 210, 225, 240, 255, 270, 285, 300, 320,
    340, 360, 380, 405, 430, 455, 480, 510, 540, 570, 605, 640, 680, 720,
    765, 810, 860, 910, 965, 1020, 1080, 1140, 1210, 1280, 1360, 1440, 1530, 1620,
    1720, 1820, 1930, 2050, 2170, 2300, 2440, 2590, 2750, 2920, 3000,
};

/* interaction.md 6: the number jump's list of sections, by digit */
const char *const UI_SECTION_NAME[10] = {"AFTERTOUCH", "DCO-1", "DCO-2", "DCO ENV", "MIXER", "VCF", "VCA",
                                         "LFO", "ENV-1", "ENV-2"};
const uint8_t UI_SECTION_PAGE[10] = {16, 0, 2, 4, 5, 7, 9, 11, 12, 14};     /* 0A, 1A, ... 9A */
const uint8_t UI_PLAY_SLOT[4] = {4 * 7 + 1, 4 * 7 + 2, 4 * 8 + 0, 4 * 14 + 3};   /* 52, 53, 55, 94 */
