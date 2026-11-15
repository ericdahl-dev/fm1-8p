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
static uint8_t ck_note[4096];                  /* and its note (step i plays 48 + i) */
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
                if (ev[k].vel && ck_n_on < 4096) {
                    ck_note[ck_n_on] = ev[k].note;
                    ck_on[ck_n_on++] = (double)(done + ev[k].at) / CK_FS;
                }
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

/* 30..300 BPM (TEMPO's range), steady: the tempo reads within 0.1 BPM a beat after the clock came, and
 * the steps are on the grid (0.5 ms) with one per 6 ticks */
TT_TEST(clock_measures_the_tempo_over_its_range)
{
    static const double bpm[] = {30.0, 60.0, 200.0, 300.0};
    uint32_t i;
    for (i = 0; i < 4; i++) {
        double dt = 60.0 / bpm[i] / SEQ_CLK_PPQN, t0 = 0.05 + 24 * dt;
        uint32_t beats = bpm[i] < 100 ? 8u : 40u, tp;
        ck_fresh(SEQ_CLK_MIDI);
        ck_msg_at(t0 - dt / 2, 0, 0xFAu);
        (void)ck_ticks(0.05, bpm[i], 24u + 24u * beats, 0);
        ck_watch_from = 0.05 + 25 * dt;
        ck_run(t0 + beats * 24 * dt - 0.012);
        tp = seq_clock_tempo(&ck);
        printf("    %.0f BPM: reads %.1f (%.1f..%.1f after a beat), %u steps for %u, worst %.3f ms off the grid\n",
               bpm[i], tp / 10.0, ck_tempo_lo / 10.0, ck_tempo_hi / 10.0, ck_n_on, 4u * beats,
               ck_worst_ms(t0, bpm[i], 1));
        TT_CHECK(fabs(ck_tempo_lo / 10.0 - bpm[i]) <= 0.1 && fabs(ck_tempo_hi / 10.0 - bpm[i]) <= 0.1);
        TT_CHECK(ck_n_on == 4u * beats);
        TT_CHECK(ck_worst_ms(t0, bpm[i], 1) < 0.5);
    }
}

/* a clock of n ticks from `from`, its tempo going from bpm0 to bpm1 linearly over ticks a..b (bpm0
 * before, bpm1 after); each tick's true time into ck_tick_at */
static double ck_tick_at[8192];
static void ck_ticks_ramp(double from, double bpm0, double bpm1, uint32_t a, uint32_t b, uint32_t n)
{
    double t = from;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double bpm = i <= a ? bpm0 : i >= b ? bpm1 : bpm0 + (bpm1 - bpm0) * (i - a) / (double)(b - a);
        ck_tick_at[i] = t;
        ck_msg_at(t, 0, 0xF8u);
        t += 60.0 / bpm / SEQ_CLK_PPQN;
    }
}
/* worst |onset j - tick (k0 + 6j)'s time|, steps first..last-1, ms */
static double ck_worst_ticks_ms(uint32_t k0, uint32_t first, uint32_t last)
{
    double worst = 0;
    uint32_t j;
    for (j = first; j < last && j < ck_n_on; j++) {
        double d = fabs(ck_on[j] - ck_tick_at[k0 + 6u * j]) * 1000.0;
        worst = d > worst ? d : worst;
    }
    return worst;
}

/* The tempo changes under a playing pattern: a jump, 120 to 140 BPM, is back on the grid (1 ms) within
 * 2 beats; a ramp, 100 to 160 BPM over 16 beats (8 s), stays within 3 ms of it; neither loses or adds a
 * step. */
TT_TEST(clock_follows_tempo_changes)
{
    double after_jump, during_ramp;
    uint32_t n_jump, n_ramp;
    ck_fresh(SEQ_CLK_MIDI);
    ck_ticks_ramp(0.05, 120.0, 140.0, 24u + 8u * 24u, 24u + 8u * 24u + 1u, 24u + 16u * 24u + 1u);
    ck_msg_at(ck_tick_at[24] - 0.001, 0, 0xFAu);
    ck_run(ck_tick_at[24u + 16u * 24u] - 0.012);
    after_jump = ck_worst_ticks_ms(24u, 32u + 8u, 64u);  /* (the jump at step 32; 2 beats on) */
    n_jump = ck_n_on;
    ck_fresh(SEQ_CLK_MIDI);
    ck_ticks_ramp(0.05, 100.0, 160.0, 24u + 4u * 24u, 24u + 20u * 24u, 24u + 24u * 24u + 1u);
    ck_msg_at(ck_tick_at[24] - 0.001, 0, 0xFAu);
    ck_run(ck_tick_at[24u + 24u * 24u] - 0.012);
    during_ramp = ck_worst_ticks_ms(24u, 1u, 96u);
    n_ramp = ck_n_on;
    printf("    120 -> 140 BPM: %.3f ms off from 2 beats on (%u steps); 100 -> 160 ramp: %.3f ms (%u steps)\n",
           after_jump, n_jump, during_ramp, n_ramp);
    TT_CHECK(n_jump == 64u && n_ramp == 96u);
    TT_CHECK(after_jump < 1.0);
    TT_CHECK(during_ramp < 3.0);
}

/* Start, stop and continue: FC lets every note go and stops; FB goes on from the step after the last
 * one played, on the next tick; FA, even while playing, starts again from step 1 on the next tick; FC
 * while stopped, and ticks alone, start nothing. */
TT_TEST(clock_start_stop_and_continue)
{
    double dt = 60.0 / 120.0 / SEQ_CLK_PPQN;
    uint32_t i, ok = 1, before_fb;
    ck_fresh(SEQ_CLK_MIDI);
    ck_ticks_ramp(0.05, 120.0, 120.0, 0, 1, 24u * 24u);   /* 24 beats of steady clock, from 0.05 s */
    ck_msg_at(ck_tick_at[10] + dt / 2, 0, 0xFCu);          /* a stop while stopped: nothing */
    ck_msg_at(ck_tick_at[24] - dt / 2, 0, 0xFAu);          /* start: step 1 on tick 24 */
    ck_msg_at(ck_tick_at[24 + 6 * 8] - dt / 2, 0, 0xFCu);  /* stop before step 9 */
    ck_msg_at(ck_tick_at[120] - dt / 2, 0, 0xFBu);         /* continue: step 9 on tick 120 */
    ck_msg_at(ck_tick_at[120 + 6 * 4] - dt / 2, 0, 0xFAu); /* start again, playing: step 1 on tick 144 */
    ck_run(ck_tick_at[144 + 6 * 4] - 0.012);
    before_fb = 8;
    printf("    start/stop/continue: %u notes on, %u off; notes", ck_n_on, ck_n_off);
    for (i = 0; i < ck_n_on; i++)
        printf(" %u", ck_note[i] - 48u + 1u);
    printf("\n");
    TT_CHECK(ck_n_on == 8u + 4u + 4u);
    for (i = 0; i < before_fb && i < ck_n_on; i++) {          /* steps 1..8 from tick 24 */
        ok &= ck_note[i] == 48u + i;
        ok &= fabs(ck_on[i] - ck_tick_at[24 + 6 * i]) < (i ? 0.0005 : 0.008);
    }
    TT_CHECK(ok);
    for (i = 0; i < 4 && 8 + i < ck_n_on; i++) {               /* steps 9..12 from tick 120 */
        TT_CHECK(ck_note[8 + i] == 48u + 8u + i);
        TT_CHECK(fabs(ck_on[8 + i] - ck_tick_at[120 + 6 * i]) < (i ? 0.001 : 0.008));
    }
    for (i = 0; i < 4 && 12 + i < ck_n_on; i++) {              /* steps 1..4 again from tick 144 */
        TT_CHECK(ck_note[12 + i] == 48u + i);
        TT_CHECK(fabs(ck_on[12 + i] - ck_tick_at[144 + 6 * i]) < (i ? 0.001 : 0.008));
    }
    TT_CHECK(ck_n_off >= 8u + 4u);                             /* every step's note let go */
    TT_CHECK(seq_playing(&ck));
    ck_fresh(SEQ_CLK_MIDI);                                    /* ticks alone: nothing plays */
    ck_ticks_ramp(0.05, 120.0, 120.0, 0, 1, 96u);
    ck_run(1.0);
    TT_CHECK(ck_n_on == 0u && !seq_playing(&ck));
}

static void run_seq_clock_tests(void)
{
    TT_RUN(clock_steps_follow_a_steady_clock);
    TT_RUN(clock_steps_ride_out_a_jittery_clock);
    TT_RUN(clock_measures_the_tempo_over_its_range);
    TT_RUN(clock_follows_tempo_changes);
    TT_RUN(clock_start_stop_and_continue);
}
