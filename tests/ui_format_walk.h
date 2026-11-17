/*
 * The display walk behind tests/ui_format_golden.h (#196): every param's value as the screen reads it,
 * at every value from its min - 2 to its max + 2 (so the clamps, and X-MOD's and the ENV MODEs' 0..3,
 * count too). test_ui_format.c checks the golden against it; tests/ui_golden_main.c (make ui-golden)
 * writes the golden from it.
 *
 * Each param's walk is one FNV-1a hash over, for every value: ui_value's number and unit, ui_format's
 * text and ui_column's number and unit. A few values are also kept as text (UFW_SAMPLES), so a changed
 * hash comes with the readable rows that moved.
 */
#ifndef UI_FORMAT_WALK_H
#define UI_FORMAT_WALK_H
#include <stdint.h>
#include <string.h>
#include "../engine/fm1_8p.h"
#include "../firmware/ui.h"

#define UFW_SAMPLES 10u                 /* min, 1/8 .. 7/8 of the way, max, then the default */
#define UFW_TEXT 40u                    /* "focus | column num unit": room for all three */

typedef struct {
    uint16_t id;
    const char *name;
    uint32_t points;                    /* values walked: max - min + 5 */
    uint32_t hash;
    const char *sample[UFW_SAMPLES];    /* ufw_text at ufw_sample_value(i) */
} ufw_row;

static uint32_t ufw_fnv(uint32_t h, const char *s)
{
    do {                                /* (the 0 too: "AB" + "C" is not "A" + "BC") */
        h ^= (uint8_t)*s;
        h *= 16777619u;
    } while (*s++);
    return h;
}

/* the i-th sample value of in's range (i < UFW_SAMPLES) */
static int32_t ufw_sample_value(const fm_param_info *in, uint32_t i)
{
    if (i == UFW_SAMPLES - 1u)
        return in->def;
    return in->min + (int32_t)(((int64_t)(in->max - in->min) * (int64_t)i) / (int64_t)(UFW_SAMPLES - 2u));
}

/* value v as the screen reads it, as one line: the focus row's text, then the column's */
static void ufw_text(uint16_t id, int32_t v, char *out)
{
    char s[UI_VALUE_LEN], n[UI_VALUE_LEN], u[UI_VALUE_LEN];
    ui_format(id, v, s);
    ui_column(id, v, n, u);
    strcpy(out, s);
    strcat(out, " | ");
    strcat(out, n);
    if (*u) {
        strcat(out, " ");
        strcat(out, u);
    }
}

/* param id's walk: its hash, and how many values went into it */
static uint32_t ufw_hash(uint16_t id, uint32_t *points)
{
    const fm_param_info *in = fm_param_info_get(id);
    uint32_t h = 2166136261u, n = 0;
    int32_t v;
    for (v = in->min - 2; v <= in->max + 2; v++, n++) {
        char s[UI_VALUE_LEN], num[UI_VALUE_LEN], unit[UI_VALUE_LEN];
        ui_value(id, v, num, unit);
        h = ufw_fnv(ufw_fnv(h, num), unit);
        ui_format(id, v, s);
        h = ufw_fnv(h, s);
        ui_column(id, v, num, unit);
        h = ufw_fnv(ufw_fnv(h, num), unit);
    }
    *points = n;
    return h;
}

#endif
