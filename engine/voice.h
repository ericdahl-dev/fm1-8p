/*
 * voice.h - one note: oscillator and amp envelope (internal to the
 * engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * The engine decides which voice plays a note (see docs/voices.md); the
 * voice handles starting, releasing and stealing it. A stolen voice
 * fades its old note out over a short time and then starts the new one
 * from silence, so a steal never jumps.
 */
#ifndef FM_VOICE_H
#define FM_VOICE_H

#include <stdint.h>

#include "env.h"
#include "osc.h"

typedef struct {
    fm_osc osc;
    fm_env amp_env;
    uint32_t stamp;    /* engine event count at the last note on or off */
    uint32_t next_inc; /* pitch of `note` (kept while an old note fades) */
    uint8_t note;
    uint8_t key_down;
    uint8_t sustained; /* key up, held by the sustain pedal */
    uint8_t pending;   /* fading out a stolen note; `note` starts after */
} fm_voice;

void fm_voice_reset(fm_voice *v);

/* Sounding: attack through release, or fading out after a steal. */
int fm_voice_active(const fm_voice *v);

/*
 * Plays `note` at phase increment `inc`. An idle voice starts it at
 * once, and a voice already playing the same note retriggers its attack
 * from the current level. Any other sounding voice is stolen: the old
 * note fades out over `fade_samples`, then the new one starts. A voice
 * already fading just changes the note it will start.
 */
void fm_voice_note_on(fm_voice *v, uint8_t note, uint32_t inc,
                      uint32_t fade_samples);

/* The key came up: hold the note if the pedal is down, else release. */
void fm_voice_key_up(fm_voice *v, int pedal_down);

/* Releases the note whatever holds it (pedal up, all notes off). */
void fm_voice_release(fm_voice *v);

/* Silences the voice at once (all sound off). */
void fm_voice_silence(fm_voice *v);

/* New pitch for the note (bend). An old note that is fading keeps its. */
void fm_voice_set_inc(fm_voice *v, uint32_t inc);

/* Adds `frames` samples of the voice (Q8.24) to `mix`. */
void fm_voice_render_add(fm_voice *v, int32_t *mix, uint32_t frames);

#endif /* FM_VOICE_H */
