/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p patch storage: the record format (version 1).
 *
 *   [0]        length of the record in bytes (header and entries); 0 or 0xFF: an empty slot
 *   [1]        version, 1
 *   [2..13]    the name, STORE_NAME_LEN bytes, '\0'-padded
 *   [14]       entry count
 *   [15..]     entries of 3 bytes: a tag, then the value as int16, low byte first
 *
 * A tag names a param for good: TAG_PARAM below. A record from older firmware lacks the params added
 * since, which load at their engine default; one from newer firmware has tags this one doesn't know,
 * which are skipped. */
#include "patch_store.h"

#define HEADER 15u
#define ENTRY 3u
#define VERSION 1u

/* Tag n is the param TAG_PARAM[n]. Append only: never reorder, reuse or remove a tag. */
static const uint8_t TAG_PARAM[] = {
    FM_PARAM_AMP_ATTACK_MS,
    FM_PARAM_AMP_DECAY_MS,
    FM_PARAM_AMP_SUSTAIN,
    FM_PARAM_AMP_RELEASE_MS,
    FM_PARAM_BEND_RANGE,
    FM_PARAM_VOICE_MODE,
    FM_PARAM_UNISON_DETUNE_CENTS,
    FM_PARAM_OSC1_WAVE,
    FM_PARAM_OSC1_PULSE_WIDTH_PCT,
    FM_PARAM_OSC1_OCTAVE,
    FM_PARAM_OSC1_FINE_CENTS,
    FM_PARAM_OSC2_WAVE,
    FM_PARAM_OSC2_PULSE_WIDTH_PCT,
    FM_PARAM_OSC2_OCTAVE,
    FM_PARAM_OSC2_FINE_CENTS,
    FM_PARAM_OSC2_LEVEL,
    FM_PARAM_CHORUS_MODE,
    FM_PARAM_LFO_WAVE,
    FM_PARAM_LFO_RATE_CENTIHZ,
    FM_PARAM_LFO_DELAY_MS,
    FM_PARAM_LFO_PITCH_CENTS,
    FM_PARAM_AMP_VELOCITY,
    FM_PARAM_LFO_PW_PCT,
    FM_PARAM_OSC_SYNC,
    FM_PARAM_OSC_XMOD,
    FM_PARAM_FILTER_CUTOFF_CENTS,
    FM_PARAM_FILTER_RESONANCE,
    FM_PARAM_FILTER_KEY_TRACK,
    FM_PARAM_FILTER_ENV_ATTACK_MS,
    FM_PARAM_FILTER_ENV_DECAY_MS,
    FM_PARAM_FILTER_ENV_SUSTAIN,
    FM_PARAM_FILTER_ENV_RELEASE_MS,
    FM_PARAM_FILTER_ENV_AMOUNT_CENTS,
    FM_PARAM_HIGHPASS_STEP,
    FM_PARAM_OSC2_COARSE_SEMIS,
    FM_PARAM_OSC_MIX_ENV,
    FM_PARAM_OSC_MIX_ENV_SOURCE,
    FM_PARAM_FILTER_ENV_SOURCE,
    FM_PARAM_LFO_PITCH_OSC2_CENTS,
    FM_PARAM_PITCH_ENV_OSC1_CENTS,
    FM_PARAM_PITCH_ENV_OSC2_CENTS,
    FM_PARAM_PITCH_ENV_SOURCE,
    FM_PARAM_PITCH_ENV_DYNA,
    FM_PARAM_OSC1_LEVEL,
    FM_PARAM_OSC_RING,
    FM_PARAM_FILTER_ENV_DYNA,
    FM_PARAM_AMP_KEY_FOLLOW,
    FM_PARAM_FILTER_ENV_KEY_FOLLOW,
    FM_PARAM_AMP_DYNA,
    FM_PARAM_OSC_MIX_ENV_DYNA,
    FM_PARAM_AMP_LEVEL,
    FM_PARAM_SETTING_GLIDE,
    FM_PARAM_AMP_GATE,
};
#define TAGS (sizeof TAG_PARAM / sizeof TAG_PARAM[0])

typedef char store_every_record_fits_a_slot[HEADER + ENTRY * TAGS <= STORE_SLOT_BYTES ? 1 : -1];
typedef char store_tags_fit_a_byte[TAGS <= 255u ? 1 : -1];

uint16_t store_tag_param(uint32_t tag)
{
    return tag < TAGS ? TAG_PARAM[tag] : (uint16_t)FM_PARAM_COUNT;
}

uint32_t store_encode(const store_patch *p, uint8_t *rec)
{
    uint32_t i, n = HEADER;
    for (i = 0; i < STORE_NAME_LEN; i++)
        rec[2u + i] = 0;
    for (i = 0; i < STORE_NAME_LEN && p->name[i]; i++)
        rec[2u + i] = (uint8_t)p->name[i];
    for (i = 0; i < TAGS; i++) {
        int32_t v = p->v[TAG_PARAM[i]];
        rec[n] = (uint8_t)i;
        rec[n + 1u] = (uint8_t)((uint32_t)v & 0xFFu);
        rec[n + 2u] = (uint8_t)(((uint32_t)v >> 8) & 0xFFu);
        n += ENTRY;
    }
    rec[0] = (uint8_t)n;
    rec[1] = VERSION;
    rec[14] = (uint8_t)TAGS;
    return n;
}

int store_decode(const uint8_t *rec, uint32_t n, store_patch *p)
{
    uint32_t i, len, count;
    if (n < HEADER) return -1;
    len = rec[0];
    count = rec[14];
    if (rec[1] != VERSION || len > n || len != HEADER + ENTRY * count) return -1;   /* 0, 0xFF: empty */
    for (i = 0; i < FM_PARAM_COUNT; i++)       /* a param the record lacks: its default */
        p->v[i] = fm_param_info_get((uint16_t)i)->def;
    for (i = 0; i < STORE_NAME_LEN; i++)
        p->name[i] = (char)rec[2u + i];
    p->name[STORE_NAME_LEN] = '\0';
    for (i = 0; i < count; i++) {
        const uint8_t *e = rec + HEADER + ENTRY * i;
        int32_t v = (int16_t)(uint16_t)(e[1] | (uint32_t)e[2] << 8);
        const fm_param_info *in;
        if (e[0] >= TAGS) continue;             /* newer firmware's param */
        in = fm_param_info_get(TAG_PARAM[e[0]]);
        p->v[TAG_PARAM[e[0]]] = v < in->min ? in->min : v > in->max ? in->max : v;
    }
    return 0;
}
