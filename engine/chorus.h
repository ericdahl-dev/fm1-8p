/*
 * chorus.h - stereo chorus at the end of the signal path (internal to
 * the engine; not part of the public interface). The state, the modes
 * and the memory use are described in chorus_state.h.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef FM_CHORUS_H
#define FM_CHORUS_H

#include <stdint.h>

#include "chorus_state.h"

/*
 * Sets up `c` at `sample_rate` Hz, mode off. Cheap: the delay line is
 * emptied only when a mode turns on, not here.
 */
void fm_chorus_init(fm_chorus *c, uint32_t sample_rate);

/*
 * Selects a mode; an unknown mode, or a zero sample rate, turns the
 * chorus off. Turning it on from off starts from a silent delay line.
 * Switching between two "on" modes jumps the tap delays at once (no
 * crossfade), so it can click while sound is playing.
 */
void fm_chorus_set_mode(fm_chorus *c, uint8_t mode);

/* Empties the delay line (all sound off); the mode and LFO are kept. */
void fm_chorus_silence(fm_chorus *c);

/* Processes one sample: `in` is Q8.24, `left` and `right` get Q8.24. */
void fm_chorus_process(fm_chorus *c, int32_t in, int32_t *left,
                       int32_t *right);

/*
 * Processes a block in place: `left` holds `frames` Q8.24 input samples
 * and gets the left output; `right` gets the right output. The same as
 * calling fm_chorus_process on each sample.
 */
void fm_chorus_process_block(fm_chorus *c, int32_t *left, int32_t *right,
                             uint32_t frames);

/* Whether a mode is on (the engine skips the chorus entirely when off). */
#define fm_chorus_is_on(c) ((c)->mode != FM_CHORUS_OFF)

#endif /* FM_CHORUS_H */
