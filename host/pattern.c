/*
 * pattern.c - fm1-8p-render's -p (pattern.h).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "pattern.h"

#include <stdio.h>
#include <stdlib.h>

#include "wav.h"

int pattern_parse(const char *text, seq *s)
{
    const char *p = text;
    uint32_t step = 0;
    seq_init(s);
    for (;;) {
        uint8_t notes[SEQ_NOTES];
        uint32_t n = 0;
        if (step >= SEQ_STEPS) {
            fprintf(stderr, "pattern: more than %u steps\n", (unsigned)SEQ_STEPS);
            return -1;
        }
        if (*p == '-' || *p == '_') {
            if (*p == '_')
                seq_toggle_tie(s, step);
            p++;
        } else {
            for (;;) {
                uint32_t v = 0, digits = 0;
                while (*p >= '0' && *p <= '9' && digits < 4) {
                    v = v * 10u + (uint32_t)(*p++ - '0');
                    digits++;
                }
                if (!digits || v > 127u) {
                    fprintf(stderr, "pattern: step %u: a note is 0..127, '-' a rest, '_' a tie\n",
                            (unsigned)step + 1u);
                    return -1;
                }
                if (n == SEQ_NOTES) {
                    fprintf(stderr, "pattern: step %u: more than %u notes\n", (unsigned)step + 1u,
                            (unsigned)SEQ_NOTES);
                    return -1;
                }
                notes[n++] = (uint8_t)v;
                if (*p != ',')
                    break;
                p++;
            }
            seq_set_step(s, step, notes, n, SEQ_VEL_DEF);
        }
        step++;
        if (*p == '\0')
            break;
        if (*p != ';') {
            fprintf(stderr, "pattern: step %u: unexpected '%c'\n", (unsigned)step, *p);
            return -1;
        }
        p++;
    }
    seq_set_length(s, step);
    return 0;
}

int render_pattern(const char *text, const char *wav_path, const render_opts *o, uint32_t tempo,
                   uint32_t gate, uint32_t swing, uint32_t loops)
{
    static seq s;
    static seq_event ev[SEQ_EVENTS];
    fm_engine eng;
    uint32_t voices = o->voice_count ? o->voice_count : RENDER_DEFAULT_VOICES, i, total, play, done;
    uint64_t units = 0, inc;
    int16_t *pcm;
    int rc;
    if (voices > FM_MAX_VOICES || o->sample_rate < 8000 || o->sample_rate > 192000 || loops < 1 ||
        loops > 1000) {
        fprintf(stderr, "pattern: voices 1..%d, sample rate 8000..192000, loops 1..1000\n", FM_MAX_VOICES);
        return -1;
    }
    if (pattern_parse(text, &s) != 0)
        return -1;
    seq_set_tempo(&s, tempo);
    seq_set_gate(&s, gate);
    seq_set_swing(&s, swing);
    inc = (uint64_t)seq_tempo(&s) * SEQ_PPQ;
    for (i = 0; i < seq_length(&s); i++) {     /* the pattern once, in clock units (seq.c's step_len) */
        uint64_t first = (uint64_t)o->sample_rate * 12u * s.edit.swing_pct;
        units += i % 2u ? (uint64_t)o->sample_rate * 1200u - first : first;
    }
    play = (uint32_t)((loops * units + inc - 1u) / inc);
    total = play + (uint32_t)((uint64_t)o->tail_ms * o->sample_rate / 1000u);
    pcm = malloc((size_t)(total ? total : 1) * 2 * sizeof *pcm);
    if (!pcm || fm_engine_init(&eng, o->sample_rate, (uint8_t)voices) != FM_OK) {
        fprintf(stderr, "cannot set up render\n");
        free(pcm);
        return -1;
    }
    for (i = 0; i < o->param_count && i < RENDER_MAX_SETTINGS; i++)
        if (fm_engine_set_param(&eng, o->param_id[i], o->param_value[i]) != FM_OK)
            fprintf(stderr, "ignoring unknown parameter id %u\n", (unsigned)o->param_id[i]);
    seq_play(&s);
    for (done = 0; done < total;) {             /* blocks of SEQ_BLOCK, as play_render's */
        uint32_t n = total - done < SEQ_BLOCK ? total - done : SEQ_BLOCK, e, cnt, at = 0;
        if (done < play && play - done < n)
            n = play - done;                    /* (a block ends where the pattern does) */
        if (done == play)
            seq_stop(&s);                       /* the tail: every note let go */
        cnt = seq_render(&s, n, o->sample_rate, voices, ev);
        for (e = 0; e < cnt; e++) {
            if (ev[e].at > at) {
                fm_engine_render(&eng, pcm + 2u * (size_t)(done + at), ev[e].at - at);
                at = ev[e].at;
            }
            seq_apply_event(&eng, &ev[e], 0);
        }
        fm_engine_render(&eng, pcm + 2u * (size_t)(done + at), n - at);
        done += n;
    }
    rc = wav_write_file(wav_path, pcm, total, o->sample_rate);
    free(pcm);
    if (rc != 0)
        fprintf(stderr, "%s: cannot write\n", wav_path);
    return rc;
}
