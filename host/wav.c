/*
 * wav.c - minimal stereo 16-bit PCM WAV writer.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "wav.h"

#include <stdio.h>

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

int wav_write_file(const char *path, const int16_t *stereo, uint32_t frames,
                   uint32_t sample_rate)
{
    uint8_t h[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
                      'f', 'm', 't', ' ' };
    uint8_t chunk[4096];
    uint32_t data_bytes, i, n = 0;
    FILE *f;
    int ok;

    if (sample_rate == 0 || sample_rate > WAV_MAX_SAMPLE_RATE) return -1;
    if (frames > (0xFFFFFFFFu - 36u) / 4u) return -1;
    data_bytes = frames * 4u;
    put32(h + 4, 36u + data_bytes);
    put32(h + 16, 16);              /* fmt chunk size */
    put16(h + 20, 1);               /* PCM */
    put16(h + 22, 2);               /* channels */
    put32(h + 24, sample_rate);
    put32(h + 28, sample_rate * 4u); /* byte rate */
    put16(h + 32, 4);               /* block align */
    put16(h + 34, 16);              /* bits per sample */
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    put32(h + 40, data_bytes);

    f = fopen(path, "wb");
    if (!f) return -1;
    ok = fwrite(h, 1, sizeof h, f) == sizeof h;
    for (i = 0; ok && i < frames * 2u; i++) {
        put16(chunk + n, (uint16_t)stereo[i]);
        n += 2;
        if (n == sizeof chunk || i + 1 == frames * 2u) {
            ok = fwrite(chunk, 1, n, f) == n;
            n = 0;
        }
    }
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}
