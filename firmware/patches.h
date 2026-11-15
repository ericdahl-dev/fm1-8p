/* fm1-8p patches: the 32 JX-8P factory tones, converted to engine values by tools/jx8p.py
 * (firmware/patches.c is generated; docs/patches.md has the source and the mapping; the logic is by
 * hand in firmware/patch.c). Plain C, no host code. */
#ifndef PATCHES_H
#define PATCHES_H
#include <stdint.h>

#define PATCH_COUNT 32u
#define PATCH_NAME_MAX 12u              /* characters in a name, without its end */
#define PATCH_SOUNDTRACK 16u            /* tone 17: the boot default of the playable module */
#define PATCH_NONE (-1)                 /* patch_param: a field the engine has no param for yet */

/* The patch_field enum: generated from tools/params.py (PATCH_FIELDS). */
#include "patch_fields.h"

/* Each value is stored + PATCH_BIAS: as plain int16_t, a small negative value (-64..-49, 0xFFCx)
 * next to another could make a 32-bit word fm1-dock rejects as a ROM address (0xFFCxxxxx). With
 * the bias no field's range reaches 0xFFC0. */
#define PATCH_BIAS 32768

typedef struct {
    char name[PATCH_NAME_MAX + 1u];
    uint16_t v[PATCH_FIELD_COUNT];      /* by patch_field, + PATCH_BIAS: read with patch_value */
} patch;

extern const patch PATCHES[PATCH_COUNT];

/* Field f of patch t, in engine units. */
static inline int32_t patch_value(const patch *t, patch_field f)
{
    return (int32_t)t->v[f] - PATCH_BIAS;
}

/* The engine param a field sets (an fm_param_id), or PATCH_NONE while it waits for its feature. */
int32_t patch_param(patch_field f);

/* Patch t as every param's value, into v[FM_PARAM_COUNT]: each field of t the engine has a param for
 * (the others wait for their features), clamped to the param's range, and the default for a param no
 * field sets. What the playable module boots with and what PRESETS loads. */
void patch_values(const patch *t, int32_t *v);
#endif
