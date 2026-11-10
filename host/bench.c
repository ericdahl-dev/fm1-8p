/*
 * bench.c - desktop benchmark of the engine on the FM-1 CPU test's workload.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE        /* clock_gettime_nsec_np */
#else
#define _POSIX_C_SOURCE 199309L /* clock_gettime */
#endif

#include "bench.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../firmware/cpu_test.h"

static cpu_test bench_ct;
static int32_t bench_out[2 * CT_BLOCK];
static uint64_t bench_block[BENCH_MAX_BLOCKS];
static uint64_t bench_rep[FM_MAX_VOICES][BENCH_MAX_REPS];

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

uint64_t bench_median(uint64_t *v, uint32_t n)
{
    qsort(v, n, sizeof v[0], cmp_u64);
    return v[(n - 1) / 2];
}

/* The FM-1 host clears the block and each module adds into it; do the same, outside the timing,
 * so long runs never pile up past int32. */
static void render_block(void)
{
    memset(bench_out, 0, sizeof bench_out);
    cpu_test_render(&bench_ct, bench_out, CT_BLOCK);
}

int bench_run(uint32_t blocks, uint32_t reps, bench_clock clock, void *ctx, bench_result *r)
{
    uint32_t rep, v, b;
    if (blocks == 0 || blocks > BENCH_MAX_BLOCKS || reps == 0 || reps > BENCH_MAX_REPS)
        return -1;
    if (cpu_test_init(&bench_ct) != 0)
        return -1;
    r->blocks = blocks;
    r->reps = reps;
    for (rep = 0; rep < reps; rep++) {
        for (v = 1; v <= FM_MAX_VOICES; v++) {
            cpu_test_request_voices(&bench_ct, (int32_t)v);
            render_block();                                     /* setup block: not timed */
            for (b = 0; b < blocks; b++) {
                uint64_t t0;
                memset(bench_out, 0, sizeof bench_out);
                t0 = clock(ctx);
                cpu_test_render(&bench_ct, bench_out, CT_BLOCK);
                bench_block[b] = clock(ctx) - t0;
            }
            bench_rep[v - 1][rep] = bench_median(bench_block, blocks);
        }
    }
    for (v = 1; v <= FM_MAX_VOICES; v++) {
        bench_row *row = &r->rows[v - 1];
        row->voices = v;
        row->block_ns = bench_median(bench_rep[v - 1], reps);  /* sorts: [0] min, [reps-1] max */
        row->min_ns = bench_rep[v - 1][0];
        row->max_ns = bench_rep[v - 1][reps - 1];
    }
    return 0;
}

int bench_print(const bench_result *r, FILE *f)
{
    uint32_t i;
    int coarse = 0;
    fprintf(f, "%u runs x %u blocks of %u frames per voice count (median block of each run, "
               "median of the runs)\n\n", (unsigned)r->reps, (unsigned)r->blocks, (unsigned)CT_BLOCK);
    fprintf(f, "| voices | ns / block | ns / frame | ns / voice-frame | spread |\n");
    fprintf(f, "| -----: | ---------: | ---------: | ---------------: | -----: |\n");
    for (i = 0; i < FM_MAX_VOICES; i++) {
        const bench_row *row = &r->rows[i];
        double per_frame, spread;
        if (row->voices == 0)
            continue;
        if (row->block_ns == 0) {
            fprintf(f, "| %6u | timer too coarse: blocks read as 0 ns |\n", (unsigned)row->voices);
            coarse = 1;
            continue;
        }
        per_frame = (double)row->block_ns / CT_BLOCK;
        spread = 100.0 * (double)(row->max_ns - row->min_ns) / (double)row->block_ns;
        fprintf(f, "| %6u | %10llu | %10.1f | %16.1f | %4.1f %% |\n", (unsigned)row->voices,
                (unsigned long long)row->block_ns, per_frame, per_frame / row->voices, spread);
    }
    if (coarse)
        fprintf(f, "\ntimer too coarse to time a block: use a finer clock\n");
    return coarse ? -1 : 0;
}

uint64_t bench_now_ns(void *ctx)
{
    (void)ctx;
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
    }
#endif
}

uint64_t bench_clock_step(bench_clock clock, void *ctx)
{
    uint64_t best = 0, prev = clock(ctx);
    uint32_t i;
    for (i = 0; i < 100000; i++) {
        uint64_t now = clock(ctx);
        if (now != prev && (best == 0 || now - prev < best))
            best = now - prev;
        prev = now;
    }
    return best;
}
