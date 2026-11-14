/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p patch storage: a patch as a flash record (#3, #48). Plain C, no host code: the module glue
 * moves records to and from fm1-dock's flash, and the desktop tests run the rest. */
#ifndef PATCH_STORE_H
#define PATCH_STORE_H
#include <stdint.h>
#include "../engine/fm1_8p.h"

#define STORE_NAME_LEN 12u              /* a patch name: A-Z, 0-9, space and '-' (interaction.md SAVE) */
#define STORE_SLOT_BYTES 248u           /* one slot in a flash bank: 32 a bank */

typedef struct {
    char name[STORE_NAME_LEN + 1u];     /* ends in '\0' */
    int32_t v[FM_PARAM_COUNT];          /* every engine param's value */
} store_patch;

/* Writes p as a record into rec (STORE_SLOT_BYTES); returns the bytes used. */
uint32_t store_encode(const store_patch *p, uint8_t *rec);
/* Reads a record of up to n bytes into p: 0, or -1 when the slot is empty or not a record. */
int store_decode(const uint8_t *rec, uint32_t n, store_patch *p);
/* The param a tag names, or FM_PARAM_COUNT for a tag this firmware doesn't know. */
uint16_t store_tag_param(uint32_t tag);

/* The user slots (interaction.md: 11..88), 32 to a flash bank: slot s is in bank 1 + s / 32. */
#define STORE_USER_SLOTS 64u
#define STORE_BANK_SLOTS 32u
#define STORE_BANK_BYTES (STORE_BANK_SLOTS * STORE_SLOT_BYTES)

/* The flash, as fm1-dock's bank_load_n / bank_save_n (hb_module.h) or a test's fake: load gives the
 * bytes read (-1: no such bank), save 0 or -1. */
typedef struct {
    int (*load)(void *ctx, uint32_t idx, void *p, uint32_t n);
    int (*save)(void *ctx, uint32_t idx, const void *p, uint32_t n);
    void *ctx;
} store_io;

/* The user slots in RAM: each bank's image as it is in flash. */
typedef struct {
    uint8_t img[STORE_USER_SLOTS / STORE_BANK_SLOTS][STORE_BANK_BYTES];
} store_lib;

/* At power-on: the banks from flash (a bank not there: every slot empty). */
void store_lib_load(store_lib *l, const store_io *io);
/* User slot s (0..63) into p: 0, or -1 when it is empty. */
int store_slot_read(const store_lib *l, uint32_t s, store_patch *p);
/* 1 when user slot s holds a patch */
int store_slot_used(const store_lib *l, uint32_t s);
/* p into user slot s, and its bank to flash: 0, or -1 when the write failed (the slot is then as
 * it was). */
int store_slot_write(store_lib *l, const store_io *io, uint32_t s, const store_patch *p);

#endif
