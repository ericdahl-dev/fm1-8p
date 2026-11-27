/*
 * voice.h - one note: oscillator pair and amp envelope (internal to the
 * engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * The engine decides which voice plays a note (see docs/voices.md); the
 * voice handles starting, releasing and stealing it. A stolen voice
 * starts the new note at once from the levels its envelopes are at, as
 * PG-8X 2.0.13 does, so a steal never jumps.
 */
#ifndef FM_VOICE_H
#define FM_VOICE_H

#include <stdint.h>

#include "env.h"
#include "vfilter.h"
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
    int32_t next_pitch; /* pitch of `note`, set as it starts */
    uint32_t lfo_start; /* control tick the LFO delay started at */
    uint8_t note;
    uint8_t key_down;
    uint8_t sustained; /* key up, held by the sustain pedal */
    uint8_t next_key;       /* `note` as set, for the filter's key when it
                             * starts */
    uint8_t busy;           /* FM_BUSY_* bits: processors away from rest */
    uint8_t velocity;       /* the sounding note's MIDI velocity */
    uint8_t new_patch;      /* a patch loaded while it sounded: its next note
                             * starts the envelopes from 0 if it is idle then
                             * (fm_voice_patch_loaded) */
    /* the filter, its cutoff and the voice's control cadence (vfilter.h);
     * its key stays the sounding note's when a mode switch sets
     * FM_VOICE_NO_NOTE */
    fm_vfilter vf;
    fm_env filter_env;      /* lazy (fm_env_skip): read at the control rate */
    int32_t mix_base;       /* osc2_level, Q1.30 */
    int32_t mix_step;       /* the mix's glide each sample to its next target */
    /* last: read only at control ticks and note starts, so the fields the
     * render reads keep their offsets */
    fm_mod mod;             /* the modulation sources and sums (mod.h) */
    /* envelope 2 apart from the VCA, with the gate (#63): amp_env runs the
     * gate then, and this the amp settings for the cutoff, pitch and mix.
     * Lazy as filter_env (fm_env_skip); idle without the gate */
    fm_env env2;
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
    uint8_t mix_dyna;     /* the mix envelope's DYNAMICS position, 0 off */
} fm_voice_mod;

/* Idle, with the filter open. `seed` starts the voice's noise; give each
 * voice its own. `rate_cents` is fm_filter_rate_cents of the engine's
 * sample rate. `amp` and `filter` are the settings its two envelopes read
 * (shared by every voice, owned by the engine; kept by pointer). */
void fm_voice_reset(fm_voice *v, uint32_t seed, int32_t rate_cents,
                    const fm_env_settings *amp, const fm_env_settings *filter);

/* Sounding: attack through release. */
int fm_voice_active(const fm_voice *v);

/* What keeps a voice, in the order the engine steals: a free voice first,
 * then one releasing, then one the pedal holds, then one its key holds.
 * A key or the pedal holds only a sounding voice: a voice goes idle only
 * through a release, which lets go of both first (fm_voice_release), or through
 * fm_voice_silence or fm_voice_reset, which clear both. */
typedef enum {
    FM_VOICE_FREE,
    FM_VOICE_RELEASING,
    FM_VOICE_PEDAL,
    FM_VOICE_HELD
} fm_voice_hold;

static inline fm_voice_hold fm_voice_holding(const fm_voice *v)
{
    if (!fm_voice_active(v)) return FM_VOICE_FREE;
    if (v->key_down) return FM_VOICE_HELD;
    if (v->sustained) return FM_VOICE_PEDAL;
    return FM_VOICE_RELEASING;
}

/* The note the voice plays (FM_VOICE_NO_NOTE before its first note, or
 * after a voice-mode switch). */
static inline uint8_t fm_voice_note(const fm_voice *v)
{
    return v->note;
}

/* Sounding `note` (a voice-mode switch's leftover plays no note). */
static inline int fm_voice_plays(const fm_voice *v, uint8_t note)
{
    return v->note == note && fm_voice_active(v);
}

/* Its key is down on `note` (a note off for `note` lets it go). */
static inline int fm_voice_key_holds(const fm_voice *v, uint8_t note)
{
    return v->key_down && v->note == note;
}

/*
 * `now` in the calls below is the number of the next control tick: a
 * note that starts from silence counts its LFO delay from it.
 * `m` is the engine's modulation (its routes give the note's level from
 * `peak`, the velocity source, summed with the voice's other sources).
 *
 * Plays `note` at `pitch` (Q16.16 semitones) with amp envelope peak
 * `peak` (Q1.30, from its MIDI `velocity`, which the voice keeps for the
 * DYNAMICS of its other depths). An idle voice starts it from silence,
 * its filter envelope and envelope 2 from wherever their releases have
 * come to (#396, fm_voice_idle_pass). A
 * sounding voice, on the same note or another (a steal), starts it at once
 * from the levels its envelopes are at, as PG-8X 2.0.13 does: no fade, and
 * the filter envelope carries on from where it is. A steal starts the LFO's
 * delay again, as PG-8X's does; the same note again keeps it running.
 */
void fm_voice_note_on(fm_voice *v, const fm_voice_mod *m, uint8_t note,
                      int32_t pitch, int32_t peak, uint8_t velocity,
                      uint32_t now, uint32_t sample_rate);

/*
 * Moves the voice to `note` at `pitch` (Q16.16 semitones) without a new voice (unison mode,
 * where every voice follows one note). The oscillator keeps its phase,
 * so the pitch changes without a jump. With `retrigger` the amp and
 * filter envelopes restart their attack from the current level (the amp
 * envelope toward `peak`, at `velocity`); without, they carry on (legato)
 * and `peak` and `velocity` are ignored, unless the voice was idle. Either way the filter's key
 * tracking follows `note` at once.
 */
void fm_voice_change_note(fm_voice *v, const fm_voice_mod *m, uint8_t note,
                          int32_t pitch, int32_t peak, uint8_t velocity,
                          uint32_t now, uint32_t sample_rate, int retrigger);

/* The key came up: hold the note if the pedal is down, else release. */
void fm_voice_key_up(fm_voice *v, int pedal_down);

/* Releases the note whatever holds it (pedal up, all notes off). */
void fm_voice_release(fm_voice *v);

/* Lets go of the voice before a voice-mode switch: releases its note if a
 * key or the pedal holds it, and the voice then belongs to no note
 * (FM_VOICE_NO_NOTE; a sounding one finishes at its pitch, and an idle one
 * has no last note for a key to get back, #164). Returns whether it released
 * (the engine ages a voice it releases). */
int fm_voice_let_go(fm_voice *v);

/* The voice's age for allocation: the engine's event count at its last note
 * on or off, which the engine sets and compares (as a wrapping difference). */
static inline void fm_voice_age(fm_voice *v, uint32_t stamp)
{
    v->stamp = stamp;
}

static inline uint32_t fm_voice_stamp(const fm_voice *v)
{
    return v->stamp;
}

/* Silences the voice at once (all sound off). Its note stays its last
 * note, for the key to get the voice back (#164). */
void fm_voice_silence(fm_voice *v);

/* The amp envelope reads `amp` (envelope 2's settings, or the gate's with
 * amp_gate, #63). A sounding note takes the new times at once. */
void fm_voice_set_amp(fm_voice *v, const fm_env_settings *amp);

/* New pitch for the note (bend). */
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
#define FM_BUSY_FILTER 0x10u     /* the filter isn't a wire (fm_filter_is_wire) */
#define FM_BUSY_MIX 0x20u        /* the mix envelope on, or a glide not landed */
#define FM_BUSY_ALL 0x33u

/* every FM_BUSY_* bit fits in fm_voice's busy */
typedef char fm_require_busy_bits_fit[
    ((FM_BUSY_CUTOFF | FM_BUSY_FILTER_ENV | FM_BUSY_FILTER | FM_BUSY_MIX) == FM_BUSY_ALL &&
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
 * word: nonzero while any is away from rest (a filter envelope amount, the mix envelope or a glide, the
 * filter not a wire, or a cutoff not yet updated for the settings and the
 * note). The busy word must be nonzero whenever this is. For checks only:
 * it costs a cutoff computation. */
uint32_t fm_voice_busy_derived(const fm_voice *v, const fm_voice_mod *m);

/* Sets the highpass position 0..4 of voice `v`'s filter (see
 * fm_filter_set_highpass), keeping the voice's busy word. */
void fm_voice_set_highpass(fm_voice *v, uint8_t step);

/* The filter's cutoff setting (linear cents, fm_filter_linear_cents) and
 * key tracking (per mille): taken at the voice's next control point. */
void fm_voice_set_cutoff_linear(fm_voice *v, int32_t linear_cents);
void fm_voice_set_key_track(fm_voice *v, int32_t permille);

/* The output's level (VCA LEVEL), per mille: the amp envelope's floor
 * follows it, so the note frees its voice 80 dB under the output (#169). */
void fm_voice_set_output_level(fm_voice *v, int32_t permille);

/* The filter's resonance, per mille: at once. */
void fm_voice_set_resonance(fm_voice *v, int32_t permille);

/* The voice's filter, to read (its cutoff, key and cadence: vfilter.h). */
static inline const fm_vfilter *fm_voice_filter(const fm_voice *v)
{
    return &v->vf;
}

/*
 * A new patch has loaded (fm_engine_patch_loaded): its notes do not start
 * from the last patch's envelopes. Not sounding: the filter envelope and
 * envelope 2 are back at 0 now. Sounding: its release runs on, and its
 * next note starts them from 0 if the voice has gone idle by then.
 */
void fm_voice_patch_loaded(fm_voice *v);

/*
 * `frames` samples pass on a voice that is not sounding (fm_voice_active
 * 0), so fm_voice_render_add is not called for them: its filter envelope
 * and envelope 2 run on through their release, as PG-8X's do (#396), so the
 * voice's next note starts from where they then are. Two tests and nothing
 * else once both have ended, or on a voice not played yet; else a count
 * added (fm_env_skip), caught up within FM_ENV_LAG_MAX samples.
 */
void fm_voice_idle_pass(fm_voice *v, uint32_t frames);

/*
 * Adds `frames` samples of the voice (Q8.24) to `mix`. `osc` is scratch
 * for `frames` samples (the oscillators' chunk, when nothing can change
 * them within it).
 */
void fm_voice_render_add(fm_voice *v, const fm_voice_mod *m,
                         int32_t *mix, int32_t *osc, uint32_t frames);

#endif /* FM_VOICE_H */
