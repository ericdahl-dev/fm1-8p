/*
 * render.h - render a Standard MIDI File through the engine to a WAV.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef RENDER_H
#define RENDER_H

#include <stddef.h>
#include <stdint.h>

#define RENDER_MAX_SETTINGS 32
#define RENDER_DEFAULT_VOICES 6 /* the FM-1 target */

typedef struct {
    uint32_t sample_rate; /* Hz */
    uint32_t tail_ms;     /* extra time rendered after the song ends */
    uint32_t voice_count; /* 1..FM_MAX_VOICES; 0 means RENDER_DEFAULT_VOICES */
    uint32_t channel;     /* MIDI channel 1..16 to play; 0 means the
                             channel of the first note on */
    /* engine parameters set before the song starts, in order */
    uint32_t param_count;
    uint16_t param_id[RENDER_MAX_SETTINGS];
    int32_t param_value[RENDER_MAX_SETTINGS];
} render_opts;

/*
 * Renders `midi_path` to a stereo 16-bit WAV at `wav_path`. The WAV is
 * exactly (song length + tail) long, at o->sample_rate. The engine is a
 * one-channel synth: only note, controller and pitch bend messages on
 * the chosen channel reach it. Returns 0 on
 * success; on failure prints a message to stderr and returns -1.
 */
int render_midi_file(const char *midi_path, const char *wav_path,
                     const render_opts *o);

/* What a render used, for the summary the tool prints. */
typedef struct {
    uint32_t voice_count; /* after the default is applied */
    uint32_t channel;     /* MIDI channel played, 1..16 */
} render_info;

/* render_midi_file, also filling `info` (if non-null) on success. */
int render_midi(const char *midi_path, const char *wav_path,
                const render_opts *o, render_info *info);

/*
 * One line for the user: voice count, channel and the level one note
 * peaks at, since the mix gain is 1 / (2 x voices).
 */
void render_describe(const render_info *info, char *buf, size_t len);

#endif /* RENDER_H */
