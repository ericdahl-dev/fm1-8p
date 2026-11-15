/*
 * pattern.h - fm1-8p-render's -p: a sequencer pattern (firmware/seq.c) rendered to a WAV (#36).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef PATTERN_H
#define PATTERN_H

#include <stdint.h>

#include "../firmware/seq.h"
#include "render.h"

/*
 * A pattern's text into s (seq_init'd here): up to SEQ_STEPS steps split by ';', each a chord of up
 * to SEQ_NOTES MIDI notes split by ',' ("60,64,67"), "-" a rest or "_" a tie, at velocity 100.
 * Returns 0, or -1 with a message on stderr.
 */
int pattern_parse(const char *text, seq *s);

/*
 * Plays `text` `loops` times at tempo (tenths of a BPM) and gate (%) through the engine, as the FM-1
 * plays it (seq_render, each note at its frame), with o's sample rate, voices and settings, then
 * o->tail_ms with the sequencer stopped, into a WAV. Returns 0, or -1 with a message on stderr.
 */
int render_pattern(const char *text, const char *wav_path, const render_opts *o, uint32_t tempo,
                   uint32_t gate, uint32_t loops);

#endif /* PATTERN_H */
