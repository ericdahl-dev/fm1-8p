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

#endif
