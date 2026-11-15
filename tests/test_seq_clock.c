/* The sequencer under external MIDI clock (#38): ticks stamped on a simulated host counter as the main
 * loop would see them, the audio interrupt's blocks stamped on the same counter, at seq's public API. */
#include <math.h>
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
}
static void ck_msg_at(double at, double late, uint8_t st)
{
    ck_msgs[ck_n_msgs].at = at;
    ck_msgs[ck_n_msgs].stamp = at + late;
    ck_msgs[ck_n_msgs].st = st;
    ck_n_msgs++;
}
/* a clock at bpm from `from` s, n ticks, each stamped up to `jitter` s late */
static double ck_ticks(double from, double bpm, uint32_t n, double jitter)
{
    double dt = 60.0 / bpm / SEQ_CLK_PPQN;
    uint32_t i;
    for (i = 0; i < n; i++)
        ck_msg_at(from + i * dt, jitter * ck_rand(), 0xF8u);
    return from + n * dt;
}
/* render `secs` s: between blocks the main loop handles what has been stamped by then and polls */
static void ck_run(double secs)
{
    static seq_event ev[SEQ_EVENTS];
    uint32_t frames = (uint32_t)(secs * CK_FS), done = 0, m = 0, k, got;
    while (done < frames) {
        double now = (double)done / CK_FS;
        while (m < ck_n_msgs && ck_msgs[m].stamp <= now) {
            seq_clock_in(&ck, ck_msgs[m].st, ck_host(ck_msgs[m].stamp));
            m++;
        }
        seq_clock_poll(&ck, ck_host(now));
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

static void run_seq_clock_tests(void)
{
    TT_RUN(clock_steps_follow_a_steady_clock);
}
