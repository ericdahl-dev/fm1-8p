/*
 * main.c - fm1-8p-render: render a Standard MIDI File to a stereo WAV.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/fm1_8p.h"
#include "render.h"

static void usage(void)
{
    uint16_t id;
    fprintf(stderr,
            "usage: fm1-8p-render [options] input.mid output.wav\n"
            "  -r RATE        sample rate in Hz (default 48000)\n"
            "  -t MS          tail rendered after the song ends (default 1000)\n"
            "  -s NAME=VALUE  set a parameter (repeatable); parameters:\n");
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        const fm_param_info *p = fm_param_info_get(id);
        fprintf(stderr, "                   %-22s %d..%d (default %d)\n",
                p->name, (int)p->min, (int)p->max, (int)p->def);
    }
}

static int parse_u32(const char *s, uint32_t *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v > 0xFFFFFFFFul) return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_setting(const char *s, render_opts *o)
{
    const char *eq = strchr(s, '=');
    uint16_t id;
    char *end;
    long v;
    if (!eq || o->param_count >= RENDER_MAX_SETTINGS) return -1;
    v = strtol(eq + 1, &end, 10);
    if (eq[1] == '\0' || *end != '\0') return -1;
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        const fm_param_info *p = fm_param_info_get(id);
        if (strlen(p->name) == (size_t)(eq - s) &&
            strncmp(p->name, s, (size_t)(eq - s)) == 0) {
            o->param_id[o->param_count] = id;
            o->param_value[o->param_count] = (int32_t)v;
            o->param_count++;
            return 0;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    render_opts o;
    int i;
    memset(&o, 0, sizeof o);
    o.sample_rate = 48000;
    o.tail_ms = 1000;

    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        const char *opt = argv[i];
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        if (strcmp(opt, "-r") == 0) {
            if (parse_u32(argv[++i], &o.sample_rate) != 0) {
                fprintf(stderr, "bad sample rate: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(opt, "-t") == 0) {
            if (parse_u32(argv[++i], &o.tail_ms) != 0) {
                fprintf(stderr, "bad tail: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(opt, "-s") == 0) {
            if (parse_setting(argv[++i], &o) != 0) {
                fprintf(stderr, "bad setting: %s\n", argv[i]);
                usage();
                return 2;
            }
        } else {
            usage();
            return 2;
        }
    }
    if (argc - i != 2) {
        usage();
        return 2;
    }
    return render_midi_file(argv[i], argv[i + 1], &o) == 0 ? 0 : 1;
}
