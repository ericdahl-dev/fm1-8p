/*
 * main.c - fm1-8p-render: render a Standard MIDI File, or a sequencer pattern, to a stereo WAV.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include <stdio.h>
#include <string.h>

#include "../engine/fm1_8p.h"
#include "args.h"
#include "pattern.h"
#include "render.h"

static void usage(void)
{
    uint16_t id;
    fprintf(stderr,
            "usage: fm1-8p-render [options] input.mid output.wav\n"
            "       fm1-8p-render [options] -p PATTERN output.wav\n"
            "  -p PATTERN     play a sequencer pattern instead of a MIDI file: up to\n"
            "                 %u steps split by ';', each MIDI notes split by ','\n"
            "                 (a chord, up to %u), '-' a rest or '_' a tie\n"
            "                 (\"60,64,67;_;-;62\"); with -B, -g, -w and -n\n"
            "  -B TEMPO       the pattern's tempo, tenths of a BPM (default 1200)\n"
            "  -g GATE        the pattern's gate, %% of a step (default 50)\n"
            "  -w SWING       the pattern's swing, %%: 50 straight .. 75 (default 50)\n"
            "  -n LOOPS       times through the pattern (default 2)\n"
            "  -r RATE        sample rate in Hz (default 44100)\n"
            "  -t MS          tail rendered after the song ends (default 1000)\n"
            "  -v VOICES      voice count, 1..%d (default %d); each voice is\n"
            "                 mixed at 1.44/(2 x VOICES), so fewer voices play louder\n"
            "  -c CHANNEL     MIDI channel to play, 1..16 (default: the channel\n"
            "                 of the first note on); other channels are ignored\n"
            "  -s NAME=VALUE  set a parameter (repeatable); parameters:\n",
            (unsigned)SEQ_STEPS, (unsigned)SEQ_NOTES, FM_MAX_VOICES, RENDER_DEFAULT_VOICES);
    for (id = 0; id < FM_PARAM_COUNT; id++) {
        const fm_param_info *p = fm_param_info_get(id);
        fprintf(stderr, "                   %-22s %d..%d (default %d)\n",
                p->name, (int)p->min, (int)p->max, (int)p->def);
    }
}

int main(int argc, char **argv)
{
    render_opts o;
    render_info info;
    char line[160];
    const char *pattern = NULL;
    uint32_t tempo = SEQ_TEMPO_DEF, gate = SEQ_GATE_DEF, swing = SEQ_SWING_DEF, loops = 2;
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
        } else if (strcmp(opt, "-v") == 0) {
            if (args_parse_u32(argv[++i], &o.voice_count) != 0 ||
                o.voice_count < 1 || o.voice_count > FM_MAX_VOICES) {
                fprintf(stderr, "bad voice count: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(opt, "-c") == 0) {
            if (args_parse_u32(argv[++i], &o.channel) != 0 || o.channel < 1 ||
                o.channel > 16) {
                fprintf(stderr, "bad MIDI channel: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(opt, "-p") == 0) {
            pattern = argv[++i];
        } else if (strcmp(opt, "-B") == 0) {
            if (args_parse_u32(argv[++i], &tempo) != 0 || tempo < SEQ_TEMPO_MIN || tempo > SEQ_TEMPO_MAX) {
                fprintf(stderr, "bad tempo (tenths of a BPM, %u..%u): %s\n", (unsigned)SEQ_TEMPO_MIN,
                        (unsigned)SEQ_TEMPO_MAX, argv[i]);
                return 2;
            }
        } else if (strcmp(opt, "-g") == 0) {
            if (args_parse_u32(argv[++i], &gate) != 0 || gate < 1 || gate > 100) {
                fprintf(stderr, "bad gate (1..100): %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(opt, "-w") == 0) {
            if (args_parse_u32(argv[++i], &swing) != 0 || swing < 50 || swing > 75) {
                fprintf(stderr, "bad swing (50..75): %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(opt, "-n") == 0) {
            if (args_parse_u32(argv[++i], &loops) != 0 || loops < 1 || loops > 1000) {
                fprintf(stderr, "bad loop count (1..1000): %s\n", argv[i]);
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
    if (pattern) {
        if (argc - i != 1) {
            usage();
            return 2;
        }
        if (render_pattern(pattern, argv[i], &o, tempo, gate, swing, loops) != 0) return 1;
        printf("%s: the pattern, %u times\n", argv[i], (unsigned)loops);
        return 0;
    }
    if (argc - i != 2) {
        usage();
        return 2;
    }
    if (render_midi(argv[i], argv[i + 1], &o, &info) != 0) return 1;
    render_describe(&info, line, sizeof line);
    printf("%s: %s\n", argv[i + 1], line);
    return 0;
}
