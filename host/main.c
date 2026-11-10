/*
 * main.c - fm1-8p-render: render a Standard MIDI File to a stereo WAV.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include <stdio.h>
#include <string.h>

#include "../engine/fm1_8p.h"
#include "args.h"
#include "render.h"

static void usage(void)
{
    uint16_t id;
    fprintf(stderr,
            "usage: fm1-8p-render [options] input.mid output.wav\n"
            "  -r RATE        sample rate in Hz (default 44100)\n"
            "  -t MS          tail rendered after the song ends (default 1000)\n"
            "  -s NAME=VALUE  set a parameter (repeatable); parameters:\n");
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        const fm_param_info *p = fm_param_info_get(id);
        fprintf(stderr, "                   %-22s %d..%d (default %d)\n",
                p->name, (int)p->min, (int)p->max, (int)p->def);
    }
}

int main(int argc, char **argv)
{
    render_opts o;
    int i;
    memset(&o, 0, sizeof o);
    o.sample_rate = 44100;
    o.tail_ms = 1000;

    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        const char *opt = argv[i];
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        if (strcmp(opt, "-r") == 0) {
            if (args_parse_u32(argv[++i], &o.sample_rate) != 0) {
                fprintf(stderr, "bad sample rate: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(opt, "-t") == 0) {
            if (args_parse_u32(argv[++i], &o.tail_ms) != 0) {
                fprintf(stderr, "bad tail: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(opt, "-s") == 0) {
            if (args_parse_setting(argv[++i], &o) != 0) {
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
