/* fm1-8p play: the playable synth's logic, keys and MIDI into the engine. Plain C, no host code: the
 * fm1-dock module glue (firmware/fm1_8p/fm1_8p.c) calls it, and the desktop tests run it. */
#ifndef PLAY_H
#define PLAY_H
#include <stdint.h>
#include "../engine/fm1_8p.h"

#define PLAY_FS 44100u
#define PLAY_BLOCK 256u                 /* frames per fm1-dock render call */
#define PLAY_VOICES 4u                  /* polyphony on the device; 6 after the speed work (#34) */
#define PLAY_QUEUE 64u                  /* events between two renders (a power of two) */

/* One param of a patch: fm_param_id and its value. */
typedef struct {
    uint16_t id;
    int32_t value;
} play_param;

typedef struct {
    fm_engine engine;
    int16_t buf[2u * PLAY_BLOCK];       /* engine output, static: not on the ISR stack */
    /* events from the main loop to the audio interrupt: status << 16 | d1 << 8 | d2 */
    uint32_t queue[PLAY_QUEUE];
    volatile uint32_t head;             /* main loop writes: events put */
    volatile uint32_t tail;             /* ISR writes: events applied */
    volatile uint32_t lost;             /* main loop: +1 per event the full queue dropped */
    uint32_t lost_seen;                 /* ISR only: the last `lost` it acted on */
    int32_t last_note;                  /* main loop: the last note on, -1 none */
} play;

/* Sets up the engine on PLAY_VOICES voices with the default patch; 0: ready. */
int play_init(play *p);
/* The default patch: the params init sets (every other one keeps the engine's default); *n gets
 * their count. */
const play_param *play_patch(uint32_t *n);
/* main loop: a note from the keys (velocity 0: note off); the next render applies it */
void play_note(play *p, uint32_t note, uint32_t vel);
/* main loop: a channel message from the host's midi() (USB, Bluetooth or the jack: all alike), any
 * channel; the next render applies it */
void play_midi(play *p, uint32_t st, uint32_t d1, uint32_t d2);
/* main loop: the last note played (keys or MIDI; a note off leaves it), -1 none yet */
int32_t play_last_note(const play *p);
/* audio interrupt: add n (<= PLAY_BLOCK) stereo frames, Q15 in int32, into out */
void play_render(play *p, int32_t *out, uint32_t n);
#endif
