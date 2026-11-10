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
#include "../firmware/cpu_test.h"

#define BENCH_MAX_BLOCKS 100000u
#define BENCH_MAX_REPS 32u

/* Monotonic time in ns; ctx is passed through. */
typedef uint64_t (*bench_clock)(void *ctx);

typedef struct {
    uint32_t voices;
    uint64_t block_ns;          /* median over runs of each run's median block */
    uint64_t min_ns, max_ns;    /* the fastest and slowest run's median block */
} bench_row;

/* What plays: the CPU test's default chord (steps 1..8), or its full patch (step F's sound: two
 * oscillators, vibrato, pulse-width modulation, amp velocity and the chorus) on the same chord. */
typedef enum {
    BENCH_DEFAULT_PATCH = 0,
    BENCH_FULL_PATCH,
    BENCH_WORKLOAD_COUNT
} bench_workload;

typedef struct {
    bench_workload workload;
    uint32_t blocks, reps;
    bench_row rows[FM_MAX_VOICES];  /* [voices - 1] */
} bench_result;

/* Lower median of v[0..n) (sorts v, O(n log n)); n >= 1. */
uint64_t bench_median(uint64_t *v, uint32_t n);

/*
 * Each run: for 1..FM_MAX_VOICES voices, the CPU test's chord (one setup block, not timed),
 * then `blocks` blocks timed one by one. Returns 0, or -1 if blocks or reps is 0 or too big.
 */
int bench_run_workload(bench_workload w, uint32_t blocks, uint32_t reps, bench_clock clock, void *ctx,
                       bench_result *r);
/* bench_run_workload with BENCH_DEFAULT_PATCH. */
int bench_run(uint32_t blocks, uint32_t reps, bench_clock clock, void *ctx, bench_result *r);

/* "default", "full", or "all" (both) as a mask of 1 << bench_workload. Returns 0, or -1 if unknown. */
int bench_workloads_parse(const char *s, uint32_t *mask);
/* "default" or "full" */
const char *bench_workload_name(bench_workload w);
/* The CPU test the last run drove (its engine as the run left it). */
const cpu_test *bench_cpu_test(void);

/*
 * A Markdown table: voices, ns per block, ns per frame, ns per voice-frame, run spread.
 * Returns 0, or -1 (after saying "timer too coarse") if a voice count read as 0 ns per block.
 */
int bench_print(const bench_result *r, FILE *f);

/* The system's monotonic clock in ns (ctx unused): CLOCK_UPTIME_RAW on macOS, whose
 * CLOCK_MONOTONIC ticks in microseconds; CLOCK_MONOTONIC elsewhere. */
uint64_t bench_now_ns(void *ctx);

/* The smallest nonzero step between readings of `clock` (0 if it never moved). */
uint64_t bench_clock_step(bench_clock clock, void *ctx);

#endif /* BENCH_H */
