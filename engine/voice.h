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
#include "filter.h"
#include "lfo.h"
#include "mod.h"
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
    uint32_t lfo_start; /* control tick the LFO delay started at */
    int32_t next_peak; /* amp peak (velocity) of `note`, Q1.30, kept while
                        * an old note fades */
    uint8_t note;
    uint8_t old_note;  /* while pending: the stolen note fading out */
    uint8_t key_down;
    uint8_t sustained; /* key up, held by the sustain pedal */
    uint8_t pending;   /* fading out a stolen note; `note` starts after */
    /* the filter's byte-sized state, packed with the above */
    uint8_t key_note;       /* the key the filter tracks: the sounding
                             * note's, kept when a mode switch sets
                             * FM_VOICE_NO_NOTE */
    uint8_t next_key;       /* `note` as set, for key_note when it starts
                             * (after a steal fade, as next_pitch) */
    uint8_t ctrl_phase;     /* samples until the next cutoff update */
    uint8_t busy;           /* FM_BUSY_* bits: processors away from rest */
    uint8_t velocity;       /* the sounding note's MIDI velocity */
    uint8_t next_velocity;  /* `note`'s, kept beside next_peak */
    /* filter: settings copied from the engine's parameters, and state */
    fm_filter filter;
    fm_env filter_env;
    int32_t rate_cents;     /* fm_filter_rate_cents(sample rate) */
    int32_t cutoff_cents;   /* base cutoff */
    int32_t key_track;      /* per mille */
    int32_t cutoff_now;     /* cents the filter is set to (INT32_MIN: none) */
    uint32_t fenv_lag;      /* samples filter_env is behind: it is caught
                             * up only when read (the control rate) */
    int32_t mix_base;       /* osc2_level, Q1.30 */
    int32_t mix_step;       /* the mix's glide each sample to its next target */
    /* last: read only at control ticks and note starts, so the fields the
     * render reads keep their offsets */
    fm_mod mod;             /* the modulation sources and sums (mod.h) */
} fm_voice;

/* What the LFO does to every voice, set by the engine from the patch. */
typedef struct {
    fm_mod_routes routes; /* source -> destination depths (mod.h) */
    uint32_t delay_ticks; /* LFO delay, and fade-in time, in ticks */
    int32_t fade_step;    /* Q1.30 fade-in per tick after the delay */
    uint8_t penv_amp;     /* the pitch envelope follows envelope 2 (amp),
                           * else envelope 1 (filter) */
    uint8_t penv_dyna;    /* its DYNAMICS position (dyna.h), 0 off */
    uint8_t fenv_dyna;    /* the filter envelope's, on its cutoff depth */
} fm_voice_mod;

/* Idle, with the filter open. `seed` starts the voice's noise; give each
 * voice its own. `rate_cents` is fm_filter_rate_cents of the engine's
 * sample rate. `amp` and `filter` are the settings its two envelopes read
 * (shared by every voice, owned by the engine; kept by pointer). */
void fm_voice_reset(fm_voice *v, uint32_t seed, int32_t rate_cents,
                    const fm_env_settings *amp, const fm_env_settings *filter);

/*
 * Brings filter_env up to date. The render does not step it every sample:
 * it counts the samples (fenv_lag) and catches it up (fm_env_advance,
 * exactly that many steps) when it reads the level, at a control point or
 * a note change, and before every gate and setting change. Call this
 * before reading or changing filter_env from outside the voice.
 */
void fm_voice_sync_filter_env(fm_voice *v);

/* Sounding: attack through release, or fading out after a steal. */
int fm_voice_active(const fm_voice *v);

/*
 * `now` in the calls below is the number of the next control tick: a
 * note that starts from silence counts its LFO delay from it.
 * `m` is the engine's modulation (its routes give the note's level from
 * `peak`, the velocity source, summed with the voice's other sources).
 *
 * Plays `note` at `pitch` (Q16.16 semitones) with amp envelope peak
 * `peak` (Q1.30, from its MIDI `velocity`, which the voice keeps for the
 * DYNAMICS of its other depths). An idle voice starts it at
 * once, and a voice already playing the same note retriggers its attack
 * from the current level. Any other sounding voice is stolen: the old
 * note fades out over `fade_samples`, then the new one starts. A voice
 * already fading just changes the note it will start. If the new note's
 * key comes up during the fade, the note still starts after it, holds
 * for the fade time (as long as it waited) and then releases.
 */
void fm_voice_note_on(fm_voice *v, const fm_voice_mod *m, uint8_t note,
                      int32_t pitch, int32_t peak, uint8_t velocity,
                      uint32_t now, uint32_t sample_rate,
                      uint32_t fade_samples);

/*
 * Takes a voice that is fading out a stolen note back for that note
 * (`old_note`, played again): the steal is called off and the note
 * restarts its attack from the current level at `pitch`, `peak` and
 * `velocity`. The note that was waiting for the fade is dropped; the
 * caller can give it another voice, at the peak and velocity still in
 * `next_peak` and `next_velocity`.
 */
void fm_voice_take_back(fm_voice *v, const fm_voice_mod *m, int32_t pitch,
                        int32_t peak, uint8_t velocity, uint32_t now,
                        uint32_t sample_rate);

/*
 * Moves the voice to `note` at `pitch` (Q16.16 semitones) without a steal fade (unison mode,
 * where every voice follows one note). The oscillator keeps its phase,
 * so the pitch changes without a jump. With `retrigger` the amp and
 * filter envelopes restart their attack from the current level (the amp
 * envelope toward `peak`, at `velocity`); without, they carry on (legato)
 * and `peak` and `velocity` are ignored, unless the voice was idle. Either way the filter's key
 * tracking follows `note` at once. A voice still fading out a stolen poly
 * note starts `note` when the fade ends.
 */
void fm_voice_change_note(fm_voice *v, const fm_voice_mod *m, uint8_t note,
                          int32_t pitch, int32_t peak, uint8_t velocity,
                          uint32_t now, uint32_t sample_rate, int retrigger);

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
 * One control tick (every FM_LFO_TICK_FRAMES samples, numbered `now`):
 * applies `lfo` (Q1.30, the shared LFO's value), scaled by the voice's
 * delay and fade-in, at the depths in `m`. With zero depth nothing
 * changes.
 */
void fm_voice_tick(fm_voice *v, int32_t lfo, const fm_voice_mod *m,
                   uint32_t now);

/*
 * A voice's busy word: each per-voice processor sets its bit when it leaves
 * rest and clears it when it returns. A voice with none set (and sounding)
 * renders on the fast path.
 */
#define FM_BUSY_CUTOFF 0x01u     /* a setting or the note changed the cutoff */
#define FM_BUSY_FILTER_ENV 0x02u /* the filter envelope moves the cutoff */
#define FM_BUSY_STEAL 0x04u      /* fading out a stolen note (pending) */
#define FM_BUSY_GATE 0x08u       /* a release queued (gate_left) */
#define FM_BUSY_FILTER 0x10u     /* the filter isn't a wire (fm_filter_is_wire) */
#define FM_BUSY_MIX 0x20u        /* the mix envelope on, or a glide not landed */
#define FM_BUSY_ALL 0x3Fu

/* every FM_BUSY_* bit fits in fm_voice's busy */
typedef char fm_require_busy_bits_fit[
    ((FM_BUSY_CUTOFF | FM_BUSY_FILTER_ENV | FM_BUSY_STEAL | FM_BUSY_GATE |
      FM_BUSY_FILTER | FM_BUSY_MIX) == FM_BUSY_ALL &&
     FM_BUSY_ALL >> (8 * sizeof(((fm_voice *)0)->busy)) == 0) ? 1 : -1];

/* Sets or clears busy bit(s) `bits` of voice `v`. */
static inline void fm_voice_mark(fm_voice *v, uint8_t bits, int on)
{
    v->busy = (uint8_t)(on ? v->busy | bits : v->busy & ~bits);
}

/* Marks the mix busy while an envelope reaches it (or its sum is not yet
 * back at 0), a glide step is set or osc2's level is away from osc2_level; call
 * after any of them changes (the glide itself moves the mix only while a
 * step is set). */
static inline void fm_voice_mark_mix(fm_voice *v, const fm_voice_mod *m)
{
    fm_voice_mark(v, FM_BUSY_MIX,
                  !fm_mod_at_rest(&v->mod, &m->routes, FM_MOD_MIX) ||
                  v->mix_step || v->osc.req[1] != v->mix_base);
}

/* Nonzero while any of the voice's processors is away from rest; a voice
 * at rest (and sounding) renders on the fast path. The busy word itself:
 * the one way to read it (mask it with FM_BUSY_* for one processor). */
static inline uint32_t fm_voice_busy(const fm_voice *v)
{
    return v->busy;
}

/* The same question asked of the processors themselves instead of the busy
 * word: nonzero while any is away from rest (a steal fade or queued
 * release, a filter envelope amount, the mix envelope or a glide, the
 * filter not a wire, or a cutoff not yet updated for the settings and the
 * note). The busy word must be nonzero whenever this is. For checks only:
 * it costs a cutoff computation. */
uint32_t fm_voice_busy_derived(const fm_voice *v, const fm_voice_mod *m);

/* Sets the highpass position 0..3 of voice `v`'s filter (see
 * fm_filter_set_highpass), keeping the voice's busy word. */
void fm_voice_set_highpass(fm_voice *v, uint8_t step);

/*
 * Adds `frames` samples of the voice (Q8.24) to `mix`. `next_tick` is the
 * number of the next control tick and `tick_left` the samples before it,
 * so a stolen note that starts mid-chunk knows which tick its LFO delay
 * counts from. `osc` is scratch for `frames` samples (the oscillators'
 * chunk, when nothing can change them within it).
 */
void fm_voice_render_add(fm_voice *v, const fm_voice_mod *m,
                         int32_t *mix, int32_t *osc,
                         uint32_t frames, uint32_t sample_rate,
                         uint32_t next_tick, uint32_t tick_left);

#endif /* FM_VOICE_H */
