/*
 * bench.c - desktop benchmark of the engine on the FM-1 CPU test's workload.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#define _POSIX_C_SOURCE 199309L /* clock_gettime */

#include "bench.h"

#include <time.h>

#include "../firmware/cpu_test.h"

static cpu_test bench_ct;
static int32_t bench_out[2 * CT_BLOCK];
static uint64_t bench_block[BENCH_MAX_BLOCKS];
static uint64_t bench_rep[FM_MAX_VOICES][BENCH_MAX_REPS];

uint64_t bench_median(uint64_t *v, uint32_t n)
{
    uint32_t i, j;
    for (i = 1; i < n; i++) {           /* insertion sort: n is small or nearly sorted */
        uint64_t x = v[i];
        for (j = i; j > 0 && v[j - 1] > x; j--)
            v[j] = v[j - 1];
        v[j] = x;
    }
    return v[(n - 1) / 2];
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
            cpu_test_render(&bench_ct, bench_out, CT_BLOCK);    /* setup block: not timed */
            for (b = 0; b < blocks; b++) {
                uint64_t t0 = clock(ctx);
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

void bench_print(const bench_result *r, FILE *f)
{
    uint32_t i;
    fprintf(f, "%u runs x %u blocks of %u frames per voice count (median block of each run, "
               "median of the runs)\n\n", (unsigned)r->reps, (unsigned)r->blocks, (unsigned)CT_BLOCK);
    fprintf(f, "| voices | ns / block | ns / frame | ns / voice-frame | spread |\n");
    fprintf(f, "| -----: | ---------: | ---------: | ---------------: | -----: |\n");
    for (i = 0; i < FM_MAX_VOICES; i++) {
        const bench_row *row = &r->rows[i];
        double per_frame, spread;
        if (row->voices == 0 || row->block_ns == 0)
            continue;
        per_frame = (double)row->block_ns / CT_BLOCK;
        spread = 100.0 * (double)(row->max_ns - row->min_ns) / (double)row->block_ns;
        fprintf(f, "| %6u | %10llu | %10.1f | %16.1f | %4.1f %% |\n", (unsigned)row->voices,
                (unsigned long long)row->block_ns, per_frame, per_frame / row->voices, spread);
    }
}

uint64_t bench_now_ns(void *ctx)
{
    struct timespec ts;
    (void)ctx;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
