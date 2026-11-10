/*
 * wav.h - minimal stereo 16-bit PCM WAV writer (desktop host only).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef WAV_H
#define WAV_H

#include <stdint.h>

/*
 * Writes `frames` interleaved stereo int16 frames as a canonical 44-byte
 * header RIFF/WAVE file. Byte order is little-endian regardless of host.
 * Returns 0 on success, -1 on I/O error or if the data would exceed 4 GB.
 */
int wav_write_file(const char *path, const int16_t *stereo, uint32_t frames,
                   uint32_t sample_rate);

#endif /* WAV_H */
