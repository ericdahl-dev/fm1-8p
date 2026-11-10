/*
 * smf.h - Standard MIDI File reader (desktop host only).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Reads format 0 and 1 files with metrical (ticks per quarter note)
 * timing, follows tempo changes, and returns every channel voice message
 * from all tracks merged into one list ordered by time.
 */
#ifndef SMF_H
#define SMF_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t time_us; /* from the start of the song */
    uint8_t status;   /* 0x80..0xEF; note on with velocity 0 becomes 0x8n */
    uint8_t data1;
    uint8_t data2;    /* 0 for one-data-byte messages */
} smf_event;

typedef struct {
    smf_event *events; /* malloc'd; release with smf_free */
    size_t count;
    uint64_t end_us;   /* time of the last end-of-track or event */
} smf_song;

/*
 * Parses `len` bytes. Returns 0 on success; otherwise -1 with a message
 * in `err` (if non-null) and nothing to free.
 */
int smf_parse(const uint8_t *data, size_t len, smf_song *song, char *err,
              size_t err_len);
void smf_free(smf_song *song);

#endif /* SMF_H */
