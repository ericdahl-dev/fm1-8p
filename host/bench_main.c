/*
 * bench_main.c - fm1-8p-bench: time the engine on the FM-1 CPU test's workload.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include <stdio.h>
#include <string.h>

#include "bench.h"

static void usage(FILE *f)
{
    fprintf(f,
            "usage: fm1-8p-bench [-b BLOCKS] [-r RUNS] [-w WORKLOAD] | -h\n"
            "  -b BLOCKS  timed 256-frame blocks per voice count and run (default 2000, max %u)\n"
            "  -r RUNS    runs; the table gives the median run and the spread (default 5, max %u)\n"
            "  -w WORKLOAD  default (without -w), full or all; repeat to add one: one table each\n"
            "Workloads: the FM-1 CPU test's held chord (firmware/cpu_test.c) at 1..%u voices, 44100 Hz.\n"
            "  default  the default patch (steps 1..8 of the CPU test)\n"
            "  full     the full patch (step F's sound: pulse + saw, vibrato, pulse-width modulation,\n"
            "           amp velocity with varied note velocities, resonant filter, chorus)\n",
            (unsigned)BENCH_MAX_BLOCKS, (unsigned)BENCH_MAX_REPS, (unsigned)FM_MAX_VOICES);
}

static bench_result result;

int main(int argc, char **argv)
{
    bench_args a;
    uint32_t w;
    int rc = 0;
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }
    if (bench_args_parse(argc, argv, &a) != 0) {
        usage(stderr);
        return 2;
    }
    printf("clock step %llu ns\n", (unsigned long long)bench_clock_step(bench_now_ns, NULL));
    for (w = 0; w < BENCH_WORKLOAD_COUNT; w++) {
        if (!(a.workloads & (1u << w)))
            continue;
        if (bench_run_workload((bench_workload)w, a.blocks, a.reps, bench_now_ns, NULL, &result) != 0) {
            usage(stderr);
            return 2;
        }
        printf("\n## %s patch\n\n", bench_workload_name((bench_workload)w));
        if (bench_print(&result, stdout) != 0)
            rc = 1;
    }
    return rc;
}
