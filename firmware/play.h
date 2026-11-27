/* fm1-8p play: the playable synth's logic, keys and MIDI into the engine. Plain C, no host code: the
 * fm1-dock module glue (firmware/fm1_8p/fm1_8p.c) calls it, and the desktop tests run it. */
#ifndef PLAY_H
#define PLAY_H
#include <stdint.h>
#include "../engine/fm1_8p.h"
#include "patches.h"
#include "patch_store.h"
#include "seq.h"
#include "pattern_store.h"
#include "pattern_now.h"

#define PLAY_FS 44100u
#define PLAY_BLOCK 256u                 /* frames per fm1-dock render call */
#define PLAY_VOICES 4u                  /* polyphony on the device: 4, final (Eric, 2026-11-15; #13) */
#define PLAY_KEYS 27u                   /* the FM-1's note keys */
#define PLAY_KEY0 53u                   /* key 0's note at octave 0: F3 */
#define PLAY_PARAM_WORDS ((FM_PARAM_COUNT + 31u) / 32u)
#define PLAY_QUEUE 64u                  /* events between two renders (a power of two) */

typedef struct {
    fm_engine engine;
    int16_t buf[2u * PLAY_BLOCK];       /* engine output, static: not on the ISR stack */
    /* Fields both contexts touch are handoffs, each of one kind (A queue, B ask counter, C gated
     * publish, D single word): docs/handoffs.md has the rules and every field.
     * events from the main loop to the audio interrupt: status << 16 | d1 << 8 | d2 (kind A) */
    uint32_t queue[PLAY_QUEUE];
    volatile uint32_t head;             /* kind A, main loop writes: events put */
    volatile uint32_t tail;             /* kind A, ISR writes: events applied */
    volatile uint32_t lost;             /* kind B, main loop: +1 per event the full queue dropped */
    uint32_t lost_seen;                 /* ISR only: the last `lost` it acted on */
    int32_t last_note;                  /* main loop: the last note on, -1 none */
    const patch *patch;                 /* the patch playing */
    int32_t oct;                        /* main loop: the keys' octave, -2..2 */
    uint8_t held[PLAY_KEYS];            /* main loop: the note each key started */
    /* params set from the main loop (the UI's knobs): the value asked for, which the ISR moves the
     * engine to */
    volatile int32_t target[FM_PARAM_COUNT];   /* payload of asked (B) and of loading (C) */
    int32_t stored[FM_PARAM_COUNT];     /* main loop: the stored patch's values (play_edited) */
    volatile uint32_t asked[PLAY_PARAM_WORDS];  /* bit id: a param set that the ISR hasn't applied (two
                                                 * writers: main sets, ISR clears; docs/handoffs.md) */
    volatile uint32_t loading;          /* kind C gate, main loop: 1 part way through a load (the ISR waits) */
    volatile uint32_t loads;            /* kind C generation, main loop: +1 per load done */
    uint32_t loads_seen;                /* ISR only: the last `loads` it applied */
    store_lib *lib;                     /* main loop: the user slots (NULL: none, or not loaded yet) */
    store_lib *lib_attached;            /* main loop: attached at init, loaded on the first tick */
    const store_io *io;                 /* main loop: its flash */
    volatile uint32_t sounding;         /* kind D, ISR writes after each block: the voices sounding */
    uint32_t live_mod;                  /* ISR only: the last MIDI CC1 (#42: where a stop returns the mod) */
    uint32_t prog;                      /* main loop: the program playing (PLAY_PROG_P + n: P n+1) */
    uint32_t prog_gone;                 /* main loop: 1 when its slot was deleted (#303): edited, whatever */
    char name[STORE_NAME_LEN + 1u];     /* main loop: its name */
    /* compare (EDIT held): the edit buffer, kept while the stored patch sounds */
    int32_t edit[FM_PARAM_COUNT];
    uint32_t comparing;                 /* main loop: 1 while the stored patch sounds for compare */
    seq seq;                            /* the sequencer (seq.h: its own main loop / ISR split) */
    /* pattern slots (#40), main loop: the slots (NULL: none, or not read yet), and which pattern is
     * running, queued and stored (#324) */
    pattern_now pat;
    pat_lib *plib;
    pat_lib *plib_attached;
    uint32_t store_bank;                /* main loop: the next bank the first draws read (one a draw) */
    uint32_t store_read;                /* main loop: the banks read so far, all kinds (the boot's bar) */
    seq_event seq_ev[SEQ_EVENTS];       /* ISR only: a block's sequencer notes, static (not on its stack) */
} play;

/* Sets up the engine on PLAY_VOICES voices with the default patch, SOUNDTRACK
 * (PATCHES[PATCH_SOUNDTRACK]); 0: ready. */
int play_init(play *p);
/* Programs, as PRESETS goes through them: the user slots 0..63 (11..88), then P1.. (the factory
 * patches) from PLAY_PROG_P. */
#define PLAY_PROG_P STORE_USER_SLOTS
#define PLAY_PROGS (PLAY_PROG_P + PATCH_COUNT)
/* The program `clicks` steps from `from`, over the user slots that hold a patch, wrapping. */
uint32_t play_prog_step(const play *p, uint32_t from, int32_t clicks);
/* Loads program q (play_load_patch's way: one block, all notes off): 0, or -1 for an empty user slot
 * (nothing changes). */
int play_load_prog(play *p, uint32_t q);
/* The program playing, and its name (STORE_NAME_LEN + 1 chars). */
uint32_t play_prog(const play *p);
void play_prog_name(const play *p, char *name);
/* At init: the user slots and their flash, not read yet (init stays fixed work). */
void play_attach_store(play *p, store_lib *lib, const store_io *io);
/* At init, after play_attach_store: the pattern slots, on the same flash, not read yet. */
void play_attach_patterns(play *p, pat_lib *lib);
/* main loop, each draw: the first calls read the attached banks, one a call (a bank's read is about
 * 4 ms, a draw call's budget 5: visual.md): the user slots' two, then the pattern slots' two, and with
 * the last slot 0's pattern into the sequencer. 1 when it read one (the call's work is done), 0 after. */
uint32_t play_store_tick(play *p);
/* Pattern slots (#40), main loop: the running, queued and stored patterns (pattern_now.h, #324). */
pattern_now *play_patterns(play *p);
/* the flash banks the first draws read: the user slots' and the pattern slots', and how many are read so
 * far (the boot screen's load bar, #50) */
#define PLAY_STORE_BANKS (STORE_USER_SLOTS / STORE_BANK_SLOTS + PAT_SLOTS / STORE_BANK_SLOTS)
uint32_t play_store_progress(const play *p);
/* main loop: 1 when nothing sounds, no note waits to, and the sequencer is stopped (a flash write may
 * stall the sound then; #201) */
uint32_t play_silent(const play *p);
/* The sequencer: play_render plays its notes at their frames, on PLAY_VOICES voices. */
seq *play_seq(play *p);
/* main loop: the edit buffer as a patch named `name` (during compare: the buffer kept) */
void play_snapshot(const play *p, const char *name, store_patch *s);
/* main loop: s into user slot `dest` and its bank to flash: 0, or -1 when the write failed. When the
 * edit buffer still equals s, that slot becomes the stored patch playing (no longer edited). */
int play_save(play *p, uint32_t dest, const store_patch *s);
/* Empties user slot dest (#303): 0, or -1 when the write failed. What plays stays as it is. */
int play_delete(play *p, uint32_t dest);
/* The patch playing. */
const patch *play_patch(const play *p);
/* main loop: load PATCHES[i] (PRESETS): the next render lets the sounding notes go and applies every
 * param that changes, all in one block, before that block's notes. play_load_patch is begin then end:
 * a render between them (the ISR, part way) applies none of it. */
void play_load_patch(play *p, uint32_t i);
void play_load_begin(play *p, uint32_t i);
void play_load_end(play *p);
/* main loop: a note from the keys (velocity 0: note off); the next render applies it */
void play_note(play *p, uint32_t note, uint32_t vel);
/* main loop: a channel message from the host's midi() (USB, Bluetooth or the jack: all alike), any
 * channel; the next render applies it */
void play_midi(play *p, uint32_t st, uint32_t d1, uint32_t d2);
/* main loop: the last note played (keys or MIDI; a note off leaves it), -1 none yet */
int32_t play_last_note(const play *p);
/* The keys' octave (the module owns it: fm1-dock lights OCT- / OCT+ only for its own octave):
 * play_octave moves it by step, clamped to -2..2; play_oct_led says whether that button's LED is lit
 * (OCT- below 0, OCT+ above). */
enum { PLAY_OCT_DOWN = 0, PLAY_OCT_UP = 1 };
void play_octave(play *p, int32_t step);
uint32_t play_oct_led(const play *p, uint32_t button);
/* Note key k (0 = F3 .. 26 = G5) down plays F3 + 12 x octave + k at vel (0, an unsensed key: 100);
 * up stops the note that key started. */
void play_key_down(play *p, uint32_t k, uint32_t vel);
void play_key_up(play *p, uint32_t k);
#define PLAY_NOTE_NAME 5u               /* room for a note's name: "C#-1" and its end */
/* the note's name into s (PLAY_NOTE_NAME bytes): MIDI 60 is "C4", 61 "C#4"; outside 0..127 "--" */
void play_note_name(int32_t note, char *s);
/* main loop: set a param (clamped to its range); the next renders move the engine to it */
void play_set_param(play *p, uint16_t id, int32_t value);
/* main loop: a param's value as last set (the engine follows it), 0 for an unknown id */
int32_t play_get_param(const play *p, uint16_t id);
/* main loop: 1 while some param of the edit buffer differs from the stored patch (during compare too) */
uint32_t play_edited(const play *p);
/* main loop: compare. begin keeps the edit buffer and sounds the stored patch (notes held go on);
 * end puts the buffer back exactly. */
void play_compare_begin(play *p);
void play_compare_end(play *p);
/* audio interrupt: add n (<= PLAY_BLOCK) stereo frames, Q15 in int32, into out */
void play_render(play *p, int32_t *out, uint32_t n);
#endif
