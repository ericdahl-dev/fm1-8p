/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p pattern storage (#40): a sequencer pattern (seq.h) as a flash record in its own slots, separate
 * from patches (decision Q2): 64 slots, 11..88 on the screen, in fm1-dock's banks 3 and 4 (the user
 * patches have 1 and 2). Plain C, no host code; the module glue passes the flash (store_io). */
#ifndef PATTERN_STORE_H
#define PATTERN_STORE_H
#include <stdint.h>
#include "seq.h"
#include "patch_store.h"                /* STORE_SLOT_BYTES, STORE_BANK_*, STORE_NAME_LEN, store_io */

#define PAT_SLOTS 64u
#define PAT_BANK0 3u                    /* fm1-dock's bank idx of slots 0..31; 32..63 in the next */

/* Writes p, named `name` (up to STORE_NAME_LEN characters), as a record into rec (STORE_SLOT_BYTES);
 * returns the bytes used. */
uint32_t pat_encode(const seq_pattern *p, const char *name, uint8_t *rec);
/* Reads a record of up to n bytes into p and name (STORE_NAME_LEN + 1): 0; or -1 when the slot is empty
 * or not a record, and then p is the empty pattern (seq_pattern_empty) and name "". */
int pat_decode(const uint8_t *rec, uint32_t n, seq_pattern *p, char *name);

/* The pattern slots in RAM: each bank's image as it is in flash. */
typedef struct {
    uint8_t img[PAT_SLOTS / STORE_BANK_SLOTS][STORE_BANK_BYTES];
} pat_lib;

/* At the first draw: the banks from flash (a bank not there: every slot empty). */
void pat_lib_load(pat_lib *l, const store_io *io);
/* One of them, b (0, 1): a bank's read is a draw call's work (visual.md's 5 ms) */
void pat_lib_load_bank(pat_lib *l, const store_io *io, uint32_t b);
/* Slot s (0..63) into p and name: 0, or -1 when it is empty (p the empty pattern, name ""). */
int pat_slot_read(const pat_lib *l, uint32_t s, seq_pattern *p, char *name);
/* 1 when slot s holds a pattern */
int pat_slot_used(const pat_lib *l, uint32_t s);
/* p and its name into slot s, and its bank to flash: 0, or -1 when the write failed (the slot is then as
 * it was). */
int pat_slot_write(pat_lib *l, const store_io *io, uint32_t s, const seq_pattern *p, const char *name);
#endif
