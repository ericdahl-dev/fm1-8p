/*
 * args.c - strict parsing of fm1-8p-render's numeric options.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Parsed by hand rather than with strtol/strtoul, which accept leading
 * spaces and signs and wrap negative input for unsigned results.
 */
#include "args.h"

#include <string.h>

#include "../engine/fm1_8p.h"

/* Digits only, value <= limit. */
static int parse_digits(const char *s, uint64_t limit, uint64_t *out)
{
    uint64_t v = 0;
    if (*s == '\0') return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10u + (uint64_t)(*s - '0');
        if (v > limit) return -1;
    }
    *out = v;
    return 0;
}

int args_parse_u32(const char *s, uint32_t *out)
{
    uint64_t v;
    if (parse_digits(s, 0xFFFFFFFFu, &v) != 0) return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_i32(const char *s, int32_t *out)
{
    uint64_t v;
    if (*s == '-') {
        if (parse_digits(s + 1, 2147483648u, &v) != 0) return -1;
        *out = v == 2147483648u ? INT32_MIN : -(int32_t)v;
        return 0;
    }
    if (parse_digits(s, 2147483647u, &v) != 0) return -1;
    *out = (int32_t)v;
    return 0;
}

int args_parse_setting(const char *s, render_opts *o)
{
    const char *eq = strchr(s, '=');
    size_t name_len;
    int32_t value;
    uint16_t id;
    if (!eq || o->param_count >= RENDER_MAX_SETTINGS) return -1;
    if (parse_i32(eq + 1, &value) != 0) return -1;
    name_len = (size_t)(eq - s);
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        const fm_param_info *p = fm_param_info_get(id);
        if (strlen(p->name) == name_len && strncmp(p->name, s, name_len) == 0) {
            o->param_id[o->param_count] = id;
            o->param_value[o->param_count] = value;
            o->param_count++;
            return 0;
        }
    }
    return -1;
}
