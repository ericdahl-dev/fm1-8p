/*
 * voice.h - one note: oscillator pair and amp envelope (internal to the
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
#include "lfo.h"
#include "osc.h"

/* `note` of a voice left sounding by a voice-mode switch: it belongs to
 * no key, so no note on reuses it and no bend retunes it. */
#define FM_VOICE_NO_NOTE 0xFF

typedef struct {
    fm_osc_pair osc;
    fm_env amp_env;
    uint32_t stamp;    /* engine event count at the last note on or off */
    int32_t pitch;     /* sounding pitch, Q16.16 semitones (note + bend) */
    int32_t next_pitch; /* pitch of `note` (kept while an old note fades) */
    uint32_t wait;     /* length of the current steal fade, in samples */
    uint32_t gate_left; /* samples until a queued release (0: none) */
    int32_t pitch_oct; /* LFO pitch offset now applied, Q16.16 octaves */
    int32_t pw_off;    /* LFO pulse width offset now applied, Q0.32 */
    int32_t lfo_fade;  /* LFO delay / fade-in level, Q1.30 */
    uint32_t lfo_wait; /* control ticks of LFO delay left */
    int32_t next_peak; /* amp peak (velocity) for the next note start */
    uint8_t note;
    uint8_t old_note;  /* while pending: the stolen note fading out */
    uint8_t key_down;
    uint8_t sustained; /* key up, held by the sustain pedal */
    uint8_t pending;   /* fading out a stolen note; `note` starts after */
    uint8_t lfo_restart; /* a note started: restart the LFO delay */
} fm_voice;

/* What the LFO does to every voice, set by the engine from the patch. */
typedef struct {
    int32_t pitch_oct;    /* pitch depth at full LFO, Q16.16 octaves */
    int32_t pw;           /* pulse width depth at full LFO, Q0.32 */
    uint32_t delay_ticks; /* LFO delay, and fade-in time, in ticks */
    int32_t fade_step;    /* Q1.30 fade-in per tick after the delay */
} fm_voice_mod;

/* `seed` starts the voice's noise; give each voice its own. */
void fm_voice_reset(fm_voice *v, uint32_t seed);

/* Sounding: attack through release, or fading out after a steal. */
int fm_voice_active(const fm_voice *v);

/*
 * Plays `note` at `pitch` (Q16.16 semitones). An idle voice starts it at
 * once, and a voice already playing the same note retriggers its attack
 * from the current level. Any other sounding voice is stolen: the old
 * note fades out over `fade_samples`, then the new one starts. A voice
 * already fading just changes the note it will start. If the new note's
 * key comes up during the fade, the note still starts after it, holds
 * for the fade time (as long as it waited) and then releases.
 */
void fm_voice_note_on(fm_voice *v, uint8_t note, int32_t pitch,
                      uint32_t sample_rate, uint32_t fade_samples);

/*
 * Takes a voice that is fading out a stolen note back for that note
 * (`old_note`, played again): the steal is called off and the note
 * restarts its attack from the current level at `pitch`. The note that was
 * waiting for the fade is dropped; the caller can give it another voice.
 */
void fm_voice_take_back(fm_voice *v, int32_t pitch, uint32_t sample_rate);

/*
 * Moves the voice to `note` at `pitch` (Q16.16 semitones) without a steal fade (unison mode,
 * where every voice follows one note). The oscillator keeps its phase,
 * so the pitch changes without a jump. With `retrigger` the envelope
 * restarts its attack from the current level; without, it carries on
 * (legato), unless the voice was idle. A voice still fading out a stolen
 * poly note starts `note` when the fade ends.
 */
void fm_voice_change_note(fm_voice *v, uint8_t note, int32_t pitch,
                          uint32_t sample_rate, int retrigger);

/*
 * Amp envelope peak (Q1.30, from the note's velocity) for the note the
 * next note on, take back or retriggering note change starts. A note
 * already sounding keeps its own.
 */
void fm_voice_set_velocity(fm_voice *v, int32_t peak);

/* The key came up: hold the note if the pedal is down, else release. */
void fm_voice_key_up(fm_voice *v, int pedal_down);

/* Releases the note whatever holds it (pedal up, all notes off). */
void fm_voice_release(fm_voice *v);

/* Silences the voice at once (all sound off). */
void fm_voice_silence(fm_voice *v);

/* New pitch for the note (bend). An old note that is fading keeps its. */
void fm_voice_set_pitch(fm_voice *v, int32_t pitch, uint32_t sample_rate);

/* Retunes oscillator `which` (0 or 1) by `cents` from the voice's pitch. */
void fm_voice_set_tune(fm_voice *v, uint8_t which, int32_t cents,
                       uint32_t sample_rate);

/*
 * One control tick (every FM_LFO_TICK_FRAMES samples): advances the
 * voice's LFO delay and fade-in and applies `lfo` (Q1.30, the shared
 * LFO's value) at the depths in `m`. With zero depth nothing changes.
 */
void fm_voice_tick(fm_voice *v, int32_t lfo, const fm_voice_mod *m);

/* Adds `frames` samples of the voice (Q8.24) to `mix`. */
void fm_voice_render_add(fm_voice *v, int32_t *mix, uint32_t frames,
                         uint32_t sample_rate);

#endif /* FM_VOICE_H */
