/* Desktop benchmark (host/bench.c): statistics and report, with a fake clock. */
#include "../host/bench.h"

typedef struct {
    uint64_t now, step, delta;
    uint32_t calls, calls_per_rep;
} fake_clock;

static uint64_t fake_now(void *ctx)     /* each block takes `step`; +delta every rep */
{
    fake_clock *c = ctx;
    if (c->calls > 0 && c->calls % c->calls_per_rep == 0)
        c->step += c->delta;
    c->calls++;
    if (c->calls % 2 == 0)
        c->now += c->step;
    return c->now;
}

static bench_result bres;

TT_TEST(bench_median_of_odd_and_even_counts)
{
    uint64_t a[] = {5, 1, 3}, b[] = {4, 1, 3, 2};
    TT_CHECK_EQ(bench_median(a, 3), 3);
    TT_CHECK_EQ(bench_median(b, 4), 2);             /* lower middle: an observed block time */
}

TT_TEST(bench_times_every_voice_count_with_steady_blocks)
{
    fake_clock c = {0, 25600, 0, 0, 0};
    uint32_t v;
    c.calls_per_rep = 2u * 10u * FM_MAX_VOICES;
    TT_CHECK_EQ(bench_run(10, 3, fake_now, &c, &bres), 0);
    TT_CHECK_EQ(c.calls, 3u * c.calls_per_rep);     /* setup blocks are not timed */
    for (v = 1; v <= FM_MAX_VOICES; v++) {
        TT_CHECK_EQ(bres.rows[v - 1].voices, v);
        TT_CHECK_EQ(bres.rows[v - 1].block_ns, 25600);
        TT_CHECK_EQ(bres.rows[v - 1].min_ns, 25600);
        TT_CHECK_EQ(bres.rows[v - 1].max_ns, 25600);
    }
}

TT_TEST(bench_reports_median_and_spread_across_runs)
{
    fake_clock c = {0, 1000, 100, 0, 0};             /* runs: 1000, 1100, 1200 ns per block */
    c.calls_per_rep = 2u * 4u * FM_MAX_VOICES;
    TT_CHECK_EQ(bench_run(4, 3, fake_now, &c, &bres), 0);
    TT_CHECK_EQ(bres.rows[0].block_ns, 1100);
    TT_CHECK_EQ(bres.rows[0].min_ns, 1000);
    TT_CHECK_EQ(bres.rows[0].max_ns, 1200);
}

TT_TEST(bench_rejects_impossible_workloads)
{
    fake_clock c = {0, 1, 0, 0, 1};
    TT_CHECK(bench_run(0, 3, fake_now, &c, &bres) != 0);
    TT_CHECK(bench_run(10, 0, fake_now, &c, &bres) != 0);
    TT_CHECK(bench_run(BENCH_MAX_BLOCKS + 1, 1, fake_now, &c, &bres) != 0);
    TT_CHECK(bench_run(10, BENCH_MAX_REPS + 1, fake_now, &c, &bres) != 0);
}

TT_TEST(bench_report_gives_time_per_frame_and_per_voice)
{
    char out[4096];
    FILE *f = tmpfile();
    size_t n;
    memset(&bres, 0, sizeof bres);
    bres.blocks = 1000;
    bres.reps = 5;
    bres.rows[3].voices = 4;
    bres.rows[3].block_ns = 25600;                  /* 100 ns a frame, 25 per voice */
    bres.rows[3].min_ns = 25344;
    bres.rows[3].max_ns = 26112;                    /* spread 768 / 25600 = 3.0 % */
    TT_CHECK(f != NULL);
    bench_print(&bres, f);
    rewind(f);
    n = fread(out, 1, sizeof out - 1, f);
    fclose(f);
    out[n] = '\0';
    TT_CHECK(strstr(out, "|      4 |      25600 |      100.0 |             25.0 |  3.0 % |") != NULL);
}

static void run_bench_tests(void)
{
    TT_RUN(bench_median_of_odd_and_even_counts);
    TT_RUN(bench_times_every_voice_count_with_steady_blocks);
    TT_RUN(bench_reports_median_and_spread_across_runs);
    TT_RUN(bench_rejects_impossible_workloads);
    TT_RUN(bench_report_gives_time_per_frame_and_per_voice);
}
