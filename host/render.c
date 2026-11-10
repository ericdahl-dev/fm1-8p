/*
 * render.c - render a Standard MIDI File through the engine to a WAV.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "render.h"

#include <stdio.h>
#include <stdlib.h>

#include "../engine/fm1_8p.h"
#include "smf.h"
#include "wav.h"

/* Longest render accepted, to keep the output buffer sane. */
#define MAX_SECONDS 3600u

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 &&
        fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc(n ? (size_t)n : 1);
        if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
            free(buf);
            buf = NULL;
        }
        *len = (size_t)n;
    }
    fclose(f);
    return buf;
}

static uint32_t us_to_frames(uint64_t us, uint32_t rate)
{
    return (uint32_t)(us * rate / 1000000u);
}

static void dispatch(fm_engine *e, const smf_event *ev)
{
    switch (ev->status & 0xF0) {
    case 0x90: fm_engine_note_on(e, ev->data1, ev->data2); break;
    case 0x80: fm_engine_note_off(e, ev->data1); break;
    default: break;
    }
}

int render_midi_file(const char *midi_path, const char *wav_path,
                     const render_opts *o)
{
    static fm_engine eng; /* large-ish; keep it off the stack */
    char err[128];
    size_t len = 0, i;
    uint8_t *data;
    smf_song song;
    uint64_t total_us;
    uint32_t total, done = 0;
    int16_t *pcm;
    int rc;

    if (o->sample_rate < 8000 || o->sample_rate > 192000) {
        fprintf(stderr, "sample rate %u out of range (8000..192000)\n",
                (unsigned)o->sample_rate);
        return -1;
    }
    data = read_file(midi_path, &len);
    if (!data) {
        fprintf(stderr, "%s: cannot read\n", midi_path);
        return -1;
    }
    rc = smf_parse(data, len, &song, err, sizeof err);
    free(data);
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", midi_path, err);
        return -1;
    }
    total_us = song.end_us + (uint64_t)o->tail_ms * 1000u;
    if (total_us > (uint64_t)MAX_SECONDS * 1000000u) {
        fprintf(stderr, "%s: longer than %u s\n", midi_path, MAX_SECONDS);
        smf_free(&song);
        return -1;
    }
    total = us_to_frames(total_us, o->sample_rate);
    pcm = malloc((size_t)(total ? total : 1) * 2 * sizeof *pcm);
    if (!pcm || fm_engine_init(&eng, o->sample_rate, 1) != FM_OK) {
        fprintf(stderr, "cannot set up render\n");
        free(pcm);
        smf_free(&song);
        return -1;
    }

    for (i = 0; i <= song.count; i++) {
        uint32_t at = i < song.count
                          ? us_to_frames(song.events[i].time_us, o->sample_rate)
                          : total;
        if (at > total) at = total;
        if (at > done) {
            fm_engine_render(&eng, pcm + 2 * (size_t)done, at - done);
            done = at;
        }
        if (i < song.count) dispatch(&eng, &song.events[i]);
    }
    smf_free(&song);

    rc = wav_write_file(wav_path, pcm, total, o->sample_rate);
    free(pcm);
    if (rc != 0) fprintf(stderr, "%s: cannot write\n", wav_path);
    return rc;
}
