/*
 * bench.h - desktop benchmark: the FM-1 CPU test's workload (firmware/cpu_test.c),
 * timed per 256-frame block at each voice count, so changes in cost show before flashing.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>
#include <stdio.h>

#include "../engine/fm1_8p.h"

#define BENCH_MAX_BLOCKS 100000u
#define BENCH_MAX_REPS 32u

/* Monotonic time in ns; ctx is passed through. */
typedef uint64_t (*bench_clock)(void *ctx);

typedef struct {
    uint32_t voices;
    uint64_t block_ns;          /* median over runs of each run's median block */
    uint64_t min_ns, max_ns;    /* the fastest and slowest run's median block */
} bench_row;

typedef struct {
    uint32_t blocks, reps;
    bench_row rows[FM_MAX_VOICES];  /* [voices - 1] */
} bench_result;

/* Lower median of v[0..n) (sorts v); n >= 1. */
uint64_t bench_median(uint64_t *v, uint32_t n);

/*
 * Each run: for 1..FM_MAX_VOICES voices, the CPU test's chord (one setup block, not timed),
 * then `blocks` blocks timed one by one. Returns 0, or -1 if blocks or reps is 0 or too big.
 */
int bench_run(uint32_t blocks, uint32_t reps, bench_clock clock, void *ctx, bench_result *r);

/* A Markdown table: voices, ns per block, ns per frame, ns per voice-frame, run spread. */
void bench_print(const bench_result *r, FILE *f);

/* The system's monotonic clock (ctx unused). */
uint64_t bench_now_ns(void *ctx);

#endif /* BENCH_H */
