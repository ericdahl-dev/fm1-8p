/*
 * fm1_8p_params.h - the parameter ids. GENERATED from tools/params.py by
 * tools/jx8p.py (make generate): edit the list, not this file.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef FM1_8P_PARAMS_H
#define FM1_8P_PARAMS_H

/* Parameter IDs. Stable: the patch format stores them. */
typedef enum {
    FM_PARAM_AMP_ATTACK_MS = 0,
    FM_PARAM_AMP_DECAY_MS,
    /* per mille of full level, 0..1000 */
    FM_PARAM_AMP_SUSTAIN,
    FM_PARAM_AMP_RELEASE_MS,
    /* semitones each way at full bend, 0..24 */
    FM_PARAM_BEND_RANGE,
    /* FM_VOICE_MODE_POLY or FM_VOICE_MODE_UNISON */
    FM_PARAM_VOICE_MODE,
    /* unison spread, lowest to highest voice */
    FM_PARAM_UNISON_DETUNE_CENTS,
    /* fm_wave: 0 saw, 1 pulse, 2 square, 3 noise */
    FM_PARAM_OSC1_WAVE,
    /* pulse duty, percent of a cycle */
    FM_PARAM_OSC1_PULSE_WIDTH_PCT,
    /* -2..+2 octaves */
    FM_PARAM_OSC1_OCTAVE,
    /* -100..+100 cents */
    FM_PARAM_OSC1_FINE_CENTS,
    /* the same four for osc2 */
    FM_PARAM_OSC2_WAVE,
    FM_PARAM_OSC2_PULSE_WIDTH_PCT,
    FM_PARAM_OSC2_OCTAVE,
    FM_PARAM_OSC2_FINE_CENTS,
    /* per mille: osc2's level into the sum, 0 off */
    FM_PARAM_OSC2_LEVEL,
    /* 0 = off, then the fm_chorus_mode modes */
    FM_PARAM_CHORUS_MODE,
    /* fm_lfo_wave (docs/modulation.md) */
    FM_PARAM_LFO_WAVE,
    /* hundredths of a hertz */
    FM_PARAM_LFO_RATE_CENTIHZ,
    /* zero for this long, then fades in as long */
    FM_PARAM_LFO_DELAY_MS,
    /* vibrato depth at full LFO, each way */
    FM_PARAM_LFO_PITCH_CENTS,
    /* percent: 0 ignores velocity, 100 full curve */
    FM_PARAM_AMP_VELOCITY,
    /* pulse width depth, percent of a cycle each way */
    FM_PARAM_LFO_PW_PCT,
    /* 1: osc2 restarts with every cycle of osc1 */
    FM_PARAM_OSC_SYNC,
    /* per mille: osc1 bends osc2's frequency by up to +-100 % */
    FM_PARAM_OSC_XMOD,
    /* lowpass cutoff, cents above MIDI note 0 (6900 = 440 Hz); FM_FILTER_OPEN_CENTS (the
     * maximum) is fully open */
    FM_PARAM_FILTER_CUTOFF_CENTS,
    /* per mille; 1000 self-oscillates */
    FM_PARAM_FILTER_RESONANCE,
    /* per mille; 1000 follows the note 1:1 */
    FM_PARAM_FILTER_KEY_TRACK,
    FM_PARAM_FILTER_ENV_ATTACK_MS,
    FM_PARAM_FILTER_ENV_DECAY_MS,
    /* per mille of full level, 0..1000 */
    FM_PARAM_FILTER_ENV_SUSTAIN,
    FM_PARAM_FILTER_ENV_RELEASE_MS,
    /* cents the filter envelope adds to the cutoff at full level; negative sweeps the cutoff
     * down */
    FM_PARAM_FILTER_ENV_AMOUNT_CENTS,
    /* 0 (off) .. 3 (most low cut) */
    FM_PARAM_HIGHPASS_STEP,
    /* -12..+12 semitones, on top of osc2's octave and fine */
    FM_PARAM_OSC2_COARSE_SEMIS,
    /* per mille, -1000..+1000: the envelope's share added to osc2_level */
    FM_PARAM_OSC_MIX_ENV,
    /* the envelope it follows: 0 filter, 1 amp */
    FM_PARAM_OSC_MIX_ENV_SOURCE,
    /* the envelope the cutoff follows at filter_env_amount_cents: 0 envelope 1 (filter), 1
     * envelope 2 (amp) */
    FM_PARAM_FILTER_ENV_SOURCE,
    /* osc 2's vibrato depth; -1 (default): lfo_pitch_cents, which is then both's */
    FM_PARAM_LFO_PITCH_OSC2_CENTS,
    /* osc 1's pitch at full envelope, signed */
    FM_PARAM_PITCH_ENV_OSC1_CENTS,
    /* osc 2's */
    FM_PARAM_PITCH_ENV_OSC2_CENTS,
    /* the envelope it follows: 0 filter, 1 amp */
    FM_PARAM_PITCH_ENV_SOURCE,
    /* DYNAMICS 0 (off) .. 3: velocity scales it */
    FM_PARAM_PITCH_ENV_DYNA,
    /* per mille: osc1's level into the sum, 0 off */
    FM_PARAM_OSC1_LEVEL,
    /* 1: osc2 times osc1, the JX-8P's SYNC 2 and X-MOD */
    FM_PARAM_OSC_RING,
    /* DYNAMICS 0 (off) .. 3: velocity scales the filter envelope's cutoff depth */
    FM_PARAM_FILTER_ENV_DYNA,
    /* per mille, 0..1000: the amp envelope's times x 2^(-this x octaves above C2) (ENV-2 KEY) */
    FM_PARAM_AMP_KEY_FOLLOW,
    /* the same for the filter envelope (ENV-1 KEY) */
    FM_PARAM_FILTER_ENV_KEY_FOLLOW,
    /* VCA DYNAMICS 0 (off: amp_velocity) .. 3: the level is the DYNAMICS share at the velocity
     * (engine/dyna.c) */
    FM_PARAM_AMP_DYNA,
    /* MIX DYNAMICS 0 (off) .. 3: osc_mix_env's depth times the DYNAMICS share at the velocity */
    FM_PARAM_OSC_MIX_ENV_DYNA,
    /* per mille: the whole note's level (VCA LEVEL), read at note on */
    FM_PARAM_AMP_LEVEL,
    /* 1: levels, filter and LFO depths glide as PG-8X's (#146) */
    FM_PARAM_SETTING_GLIDE,
    /* VCA MODE GATE (#63): 1, the amp envelope is a gate (on with the key, a fixed short
     * ramp), and the amp_* settings run envelope 2 for the cutoff, pitch and mix instead */
    FM_PARAM_AMP_GATE,
    FM_PARAM_COUNT
} fm_param_id;

#endif
