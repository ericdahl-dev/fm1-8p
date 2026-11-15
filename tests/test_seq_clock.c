/* The sequencer under external MIDI clock (#38): ticks stamped on a simulated host counter as the main
 * loop would see them, the audio interrupt's blocks stamped on the same counter, at seq's public API. */
#include <math.h>
#include <stdlib.h>
#include "../firmware/seq.h"

#define CK_FS 44100u
#define CK_BLOCK 256u
static seq ck;
/* a clock event: when it arrives (s) and when the main loop stamps it (s, >= arrival) */
typedef struct { double at, stamp; uint8_t st; } ck_msg;
static ck_msg ck_msgs[40000];
static uint32_t ck_n_msgs;
static double ck_on[4096];                     /* each note on's time, s from the render's start */
static uint32_t ck_n_on, ck_n_off;
static uint32_t ck_rng = 12345u;
static double ck_watch_from = 1e9;             /* from this time on, the tempo shown's range */
static uint32_t ck_tempo_lo, ck_tempo_hi;

static uint32_t ck_host(double s) { return (uint32_t)(uint64_t)llround(s * SEQ_CLK_HZ); }
static double ck_rand(void)                    /* 0 .. 1 */
{
    ck_rng = ck_rng * 1664525u + 1013904223u;
    return (ck_rng >> 8) / 16777216.0;
}
static void ck_fresh(uint32_t src)
{
    uint32_t i;
    seq_init(&ck);
    for (i = 0; i < SEQ_STEPS; i++) {
        uint8_t n = (uint8_t)(48 + i);
        seq_set_step(&ck, i, &n, 1, 100);
    }
    seq_set_gate(&ck, 50);
    seq_set_clock_source(&ck, src);
    ck_n_msgs = ck_n_on = ck_n_off = 0;
    ck_rng = 12345u;
    ck_watch_from = 1e9;
    ck_tempo_lo = 0xFFFFFFFFu;
    ck_tempo_hi = 0;
}
static void ck_msg_at(double at, double late, uint8_t st)
{
    ck_msgs[ck_n_msgs].at = at;
    ck_msgs[ck_n_msgs].stamp = at + late;
    ck_msgs[ck_n_msgs].st = st;
    ck_n_msgs++;
}
/* a clock at bpm from `from` s, n ticks, each stamped up to `jitter` s late (ck_ticks), or within
 * +-jitter/2 of its time (ck_ticks_around) */
static double ck_ticks(double from, double bpm, uint32_t n, double jitter)
{
    double dt = 60.0 / bpm / SEQ_CLK_PPQN;
    uint32_t i;
    for (i = 0; i < n; i++)
        ck_msg_at(from + i * dt, jitter * ck_rand(), 0xF8u);
    return from + n * dt;
}
static double ck_ticks_around(double from, double bpm, uint32_t n, double jitter)
{
    double dt = 60.0 / bpm / SEQ_CLK_PPQN;
    uint32_t i;
    for (i = 0; i < n; i++)
        ck_msg_at(from + i * dt, jitter * (ck_rand() - 0.5), 0xF8u);
    return from + n * dt;
}
static int ck_by_stamp(const void *a, const void *b)
{
    double x = ((const ck_msg *)a)->stamp, y = ((const ck_msg *)b)->stamp;
    return x < y ? -1 : x > y;
}
/* render `secs` s: between blocks the main loop handles what has been stamped by then and polls */
static void ck_run(double secs)
{
    static seq_event ev[SEQ_EVENTS];
    uint32_t frames = (uint32_t)(secs * CK_FS), done = 0, m = 0, k, got;
    qsort(ck_msgs, ck_n_msgs, sizeof ck_msgs[0], ck_by_stamp);   /* as the main loop sees them */
    while (done < frames) {
        double now = (double)done / CK_FS;
        while (m < ck_n_msgs && ck_msgs[m].stamp <= now) {
            seq_clock_in(&ck, ck_msgs[m].st, ck_host(ck_msgs[m].stamp));
            m++;
        }
        seq_clock_poll(&ck, ck_host(now));
        if (now >= ck_watch_from) {
            uint32_t tp = seq_clock_tempo(&ck);
            ck_tempo_lo = tp < ck_tempo_lo ? tp : ck_tempo_lo;
            ck_tempo_hi = tp > ck_tempo_hi ? tp : ck_tempo_hi;
        }
        seq_block_time(&ck, ck_host(now));
        got = seq_render(&ck, CK_BLOCK, CK_FS, SEQ_NOTES, ev);
        for (k = 0; k < got; k++)
            if (ev[k].kind == SEQ_EV_NOTE) {
                if (ev[k].vel && ck_n_on < 4096)
                    ck_on[ck_n_on++] = (double)(done + ev[k].at) / CK_FS;
                else if (!ev[k].vel)
                    ck_n_off++;
            }
        done += CK_BLOCK;
    }
}
/* the worst |onset - grid| over steps first..n_on-1, ms: step j on the clock's tick 6j from t0 */
static double ck_worst_ms(double t0, double bpm, uint32_t first)
{
    double step = 6.0 * 60.0 / bpm / SEQ_CLK_PPQN, worst = 0;
    uint32_t j;
    for (j = first; j < ck_n_on; j++) {
        double d = fabs(ck_on[j] - (t0 + j * step)) * 1000.0;
        worst = d > worst ? d : worst;
    }
    return worst;
}

/* A steady 120 BPM clock, source MIDI: a start, then ticks. Step j starts on tick 6j (within 0.5 ms
 * once the first beat has locked it), and every 6 ticks play exactly one step. */
TT_TEST(clock_steps_follow_a_steady_clock)
{
    double t0 = 0.1, end;
    ck_fresh(SEQ_CLK_MIDI);
    ck_msg_at(0.05, 0, 0xFAu);
    end = ck_ticks(t0, 120.0, 24u * 20u, 0);   /* 20 beats: 80 steps */
    ck_run(end - 0.012);                       /* (to before tick 480, step 80's: whole blocks) */
    printf("    steady 120 BPM: %u steps for %u ticks, worst %.3f ms off the grid after a beat\n",
           ck_n_on, 24u * 20u, ck_worst_ms(t0, 120.0, 4));
    TT_CHECK(ck_n_on == 80u);
    TT_CHECK(ck_worst_ms(t0, 120.0, 4) < 0.5);
}

/* worst |onset - grid| over steps first..last-1 */
static double ck_worst_range_ms(double t0, double bpm, uint32_t first, uint32_t last)
{
    double step = 6.0 * 60.0 / bpm / SEQ_CLK_PPQN, worst = 0;
    uint32_t j;
    for (j = first; j < last && j < ck_n_on; j++) {
        double d = fabs(ck_on[j] - (t0 + j * step)) * 1000.0;
        worst = d > worst ? d : worst;
    }
    return worst;
}

/* Each tick stamped within +-1.5 ms of its time (the main loop's latency, a USB frame), the clock
 * running 2 s before the start, as a DAW's does. Step 1 can only start in the block after tick 0 is
 * seen (within a block and the jitter: 8 ms); the steps after it are within 2 ms of the grid, within
 * 0.5 ms after 4 beats, and the tempo shown stays within 0.2 BPM. Over five jitter draws. */
TT_TEST(clock_steps_ride_out_a_jittery_clock)
{
    static const uint32_t seeds[] = {12345u, 1u, 777u, 99991u, 31337u};
    double dt = 60.0 / 120.0 / SEQ_CLK_PPQN, t0 = 0.05 + 48 * dt, worst_first = 0, worst_lock = 0, worst = 0;
    uint32_t i, lo = 0xFFFFFFFFu, hi = 0, steps_ok = 1;
    for (i = 0; i < 5; i++) {
        ck_fresh(SEQ_CLK_MIDI);
        ck_rng = seeds[i];
        ck_msg_at(t0 - dt / 2, 0, 0xFAu);           /* the start, between ticks 47 and 48 */
        (void)ck_ticks_around(0.05, 120.0, 48u + 24u * 60u, 0.003);
        ck_watch_from = 2.0;
        ck_run(t0 + 60 * 24 * dt - 0.012);
        worst_first = fmax(worst_first, ck_worst_range_ms(t0, 120.0, 0, 1));
        worst_lock = fmax(worst_lock, ck_worst_range_ms(t0, 120.0, 1, 16));
        worst = fmax(worst, ck_worst_range_ms(t0, 120.0, 16, 240));
        lo = ck_tempo_lo < lo ? ck_tempo_lo : lo;
        hi = ck_tempo_hi > hi ? ck_tempo_hi : hi;
        steps_ok &= ck_n_on == 240u;
    }
    printf("    +-1.5 ms jitter at 120 BPM: step 1 %.3f ms late at worst, then %.3f ms in the first 4 beats, %.3f ms after; tempo shown %.1f..%.1f\n",
           worst_first, worst_lock, worst, lo / 10.0, hi / 10.0);
    TT_CHECK(steps_ok);
    TT_CHECK(worst_first < 8.0);
    TT_CHECK(worst_lock < 2.0);
    TT_CHECK(worst < 0.5);
    TT_CHECK(lo >= 1198u && hi <= 1202u);
}

static void run_seq_clock_tests(void)
{
    TT_RUN(clock_steps_follow_a_steady_clock);
    TT_RUN(clock_steps_ride_out_a_jittery_clock);
}
