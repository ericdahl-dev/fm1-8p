/*
 * bench_main.c - fm1-8p-bench: time the engine on the FM-1 CPU test's workload.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include <stdio.h>
#include <string.h>

#include "args.h"
#include "bench.h"

static void usage(void)
{
    fprintf(stderr,
            "usage: fm1-8p-bench [-b BLOCKS] [-r RUNS]\n"
            "  -b BLOCKS  timed 256-frame blocks per voice count and run (default 2000, max %u)\n"
            "  -r RUNS    runs; the table gives the median run and the spread (default 5, max %u)\n"
            "Workload: the FM-1 CPU test's held chord (firmware/cpu_test.c) at 1..%u voices, 44100 Hz.\n",
            (unsigned)BENCH_MAX_BLOCKS, (unsigned)BENCH_MAX_REPS, (unsigned)FM_MAX_VOICES);
}

static bench_result result;

int main(int argc, char **argv)
{
    uint32_t blocks = 2000, runs = 5;
    int i;
    for (i = 1; i < argc; i += 2) {
        uint32_t *dst = strcmp(argv[i], "-b") == 0 ? &blocks : strcmp(argv[i], "-r") == 0 ? &runs : NULL;
        if (dst == NULL || i + 1 >= argc || args_parse_u32(argv[i + 1], dst) != 0) {
            usage();
            return 2;
        }
    }
    if (bench_run(blocks, runs, bench_now_ns, NULL, &result) != 0) {
        usage();
        return 2;
    }
    bench_print(&result, stdout);
    return 0;
}
