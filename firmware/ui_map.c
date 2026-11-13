/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_map.h"
#include "../engine/fm1_8p.h"

/* visual.md: Membrane */
const ui_theme UI_MEMBRANE = {
    .bg = 0x18E4u, .surface = 0x2145u, .surface_hi = 0x31A7u, .line = 0x3A09u, .text = 0xEF3Au,
    .dim = 0xA513u, .ink = 0x1082u, .window = 0x0861u, .led = 0xF984u,
    .section = {0xEC07u, 0xEDC7u, 0xCE15u, 0x4D9Au, 0xDB53u, 0x8E4Cu, 0x9C5Cu, 0x5657u, 0xFB4Du, 0xCE15u, 0xEF3Au},
    .ghost = 26u,                       /* 10 % */
    .chip = 1u,
    .meter_segments = 0u,
    .focus = {256u, 256u, 0u},          /* a 2 px frame */
    .hatch = 0u,
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
static const uint8_t HPF_ORDER[5] = {4, 0, 1, 2, 3};
#define HPF_STOPS 0x1Bu                     /* BOOST, FLAT, 240, 560 */

#define LIVE(n, l, f, id, st, fa) {n, l, f, id, UI_LIVE, st, fa, 0, 0, 0}
#define PEND(n, l, f) {n, l, f, 0, UI_PENDING, 0, 0, 0, 0, 0}
#define PAGE(p) [4 * (p)] =

/* interaction.md 3, page by page (a page left out is empty, and the rings skip it) */
const ui_slot UI_SLOTS[UI_PAGES * 4u] = {
    PAGE(0) LIVE(11, "RANGE", "RANGE", FM_PARAM_OSC1_OCTAVE, 1, 1),
    LIVE(12, "WAVE", "WAVEFORM", FM_PARAM_OSC1_WAVE, 1, 1),
    PEND(13, "TUNE", "TUNE"),                                      /* #158 */
    LIVE(14, "LFO", "LFO DEPTH", FM_PARAM_LFO_PITCH_CENTS, 1, 10),
    PAGE(7) {51, "HPF", "HIGHPASS", FM_PARAM_HIGHPASS_STEP, UI_LIVE, 1, 1, HPF_ORDER, 5, HPF_STOPS},
    LIVE(52, "CUTOFF", "CUTOFF", FM_PARAM_FILTER_CUTOFF_CENTS, 100, 400),
    LIVE(53, "RES", "RESONANCE", FM_PARAM_FILTER_RESONANCE, 10, 50),
    PEND(54, "LFO", "LFO DEPTH"),                                  /* #172 */
    PAGE(8) LIVE(55, "ENV", "ENV AMOUNT", FM_PARAM_FILTER_ENV_AMOUNT_CENTS, 100, 600),
    LIVE(56, "KEY", "KEY FOLLOW", FM_PARAM_FILTER_KEY_TRACK, 10, 100),
    PEND(57, "DYNA", "DYNAMICS"),
    PEND(58, "MODE", "ENV MODE"),
};

/* interaction.md 4 */
const uint8_t UI_RING_SEL[] = {0, 1, 2, 3, 4, 5, 6, UI_RING_END};
const uint8_t UI_RING_ENV[] = {7, 8, 12, 13, 14, 15, UI_RING_END};
const uint8_t UI_RING_FX[] = {9, 10, UI_RING_END};
const uint8_t UI_RING_LFO[] = {11, 16, 17, UI_RING_END};
