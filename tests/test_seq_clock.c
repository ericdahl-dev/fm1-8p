/* The sequencer under external MIDI clock (#38): ticks stamped on a simulated host counter as the main
 * loop would see them, the audio interrupt's blocks stamped on the same counter, at seq's public API. */
#include <math.h>
#include <stdlib.h>
#include "../firmware/seq.h"

#define CK_FS 44100u
#define CK_BLOCK 256u
static seq ck;
#define CK_PLAY 0x01u                          /* (a ck_msg that is a PLAY press, not MIDI) */
/* a clock event: when it arrives (s) and when the main loop stamps it (s, >= arrival) */
typedef struct { double at, stamp; uint8_t st; } ck_msg;
static ck_msg ck_msgs[40000];
static uint32_t ck_n_msgs;
static double ck_on[4096];                     /* each note on's time, s from the render's start */
static uint8_t ck_note[4096];                  /* and its note (step i plays 48 + i) */
static double ck_off[4096];                    /* each note off's time */
static uint32_t ck_n_on, ck_n_off;
static uint32_t ck_done, ck_m;                 /* ck_run's frames rendered and messages handled so far */
static uint32_t ck_rng = 12345u;
static double ck_watch_from = 1e9;             /* from this time on, the tempo shown's range */
static uint8_t ck_out[8192];                   /* MIDI real-time sent out (#286), as the main loop takes it */
static double ck_out_t[8192];                  /* and when: the end of the block it was taken after */
static uint32_t ck_n_out;
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
    ck_n_msgs = ck_n_on = ck_n_off = ck_done = ck_m = ck_n_out = 0;
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
/* render on to `secs` s from the start (a later call carries on): between blocks the main loop handles
 * what has been stamped by then, and polls */
static void ck_run(double secs)
{
    static seq_event ev[SEQ_EVENTS];
    uint32_t frames = (uint32_t)(secs * CK_FS), done = ck_done, m = ck_m, k, got;
    qsort(ck_msgs + m, ck_n_msgs - m, sizeof ck_msgs[0], ck_by_stamp);   /* as the main loop sees them */
    while (done < frames) {
        double now = (double)done / CK_FS;
        while (m < ck_n_msgs && ck_msgs[m].stamp <= now) {
            if (ck_msgs[m].st == CK_PLAY) {    /* a PLAY press, as ui_seq_transport */
                if (ck.want)
                    seq_stop(&ck);
                else
                    seq_play(&ck);
            } else {
                seq_clock_in(&ck, ck_msgs[m].st, ck_host(ck_msgs[m].stamp));
            }
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
                else if (!ev[k].vel) {
                    if (ck_n_off < 4096)
                        ck_off[ck_n_off] = (double)(done + ev[k].at) / CK_FS;
                    ck_n_off++;
                }
            }
        done += CK_BLOCK;
        {                                      /* the main loop sends what's due (#286) */
            uint8_t b;
            while (seq_clock_out_take(&ck, &b) && ck_n_out < 8192u) {
                ck_out[ck_n_out] = b;
                ck_out_t[ck_n_out++] = (double)done / CK_FS;
            }
        }
    }
    ck_done = done;
    ck_m = m;
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

/* The tempo changes under a playing pattern: a jump, 120 to 140 BPM (or 140 to 70), is back on the grid
 * (1 ms) within 2 beats; a ramp, 100 to 160 BPM over 16 beats (8 s), stays within 3 ms of it; neither loses or adds a
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
    {                                          /* and down by half at once (a gap a stall would leave) */
        double after_half;
        uint32_t n_half;
        ck_fresh(SEQ_CLK_MIDI);
        ck_ticks_ramp(0.05, 140.0, 70.0, 24u + 8u * 24u, 24u + 8u * 24u + 1u, 24u + 16u * 24u + 1u);
        ck_msg_at(ck_tick_at[24] - 0.001, 0, 0xFAu);
        ck_run(ck_tick_at[24u + 16u * 24u] - 0.012);
        after_half = ck_worst_ticks_ms(24u, 32u + 8u, 64u);
        n_half = ck_n_on;
        printf("    140 -> 70 BPM at once: %.3f ms off from 2 beats on (%u steps)\n", after_half, n_half);
        TT_CHECK(n_half == 64u && after_half < 1.0);
    }
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

/* the onsets' spacing, s: 1 when every one from `first` on is `step` apart (within 0.1 ms) */
static int ck_spaced(uint32_t first, double step)
{
    uint32_t j;
    for (j = first + 1u; j < ck_n_on; j++)
        if (fabs(ck_on[j] - ck_on[j - 1] - step) > 0.0001)
            return 0;
    return ck_n_on > first + 1u;
}

/* AUTO: the clock stops coming while it plays. Within its time out (250 ms here) the transport stops as
 * MIDI stop does (Eric), every note let go, and the clock is gone (INT, no tempo). PLAY then runs on the
 * internal clock, at the pattern's tempo. */
TT_TEST(clock_lost_stops_and_the_internal_clock_takes_over)
{
    double last;
    uint32_t n;
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_tempo(&ck, 900);                   /* the pattern's: 90 BPM */
    ck_ticks_ramp(0.05, 120.0, 120.0, 0, 1, 24u + 6u * 8u + 1u);
    last = ck_tick_at[24 + 6 * 8];
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    ck_msg_at(last + 0.5, 0, CK_PLAY);         /* after it's gone */
    ck_run(last + 0.45);
    n = ck_n_on;
    printf("    clock lost: %u steps, %u notes off, playing %u, source %s, tempo %u\n", n, ck_n_off,
           seq_playing(&ck), seq_clock_external(&ck) ? "MIDI" : "INT", seq_clock_tempo(&ck));
    TT_CHECK(n == 9u);                         /* steps 1..9, the last on the last tick */
    TT_CHECK(!seq_playing(&ck) && ck_n_off == ck_n_on);
    TT_CHECK(!seq_clock_external(&ck) && seq_clock_tempo(&ck) == 0u);
    ck_run(last + 0.5 + 1.0);
    TT_CHECK(seq_playing(&ck));
    TT_CHECK(ck_spaced(9u, 60.0 / 90.0 / SEQ_PPQ));    /* on the internal clock: 90 BPM's steps */
}

/* G13. INT ignores the clock (and plays its own); MIDI with no clock waits on PLAY (WAITING FOR CLOCK,
 * nothing plays) and starts on MIDI start; AUTO with a clock coming starts on its next tick, and plays
 * its own when none does; a second PLAY while waiting disarms. */
TT_TEST(clock_source_int_midi_and_auto)
{
    ck_fresh(SEQ_CLK_INT);                     /* INT: a clock and a start do nothing */
    ck_ticks_ramp(0.05, 100.0, 100.0, 0, 1, 24u * 8u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    ck_run(1.0);
    TT_CHECK(ck_n_on == 0u && !seq_clock_external(&ck));
    ck_msg_at(1.0, 0, CK_PLAY);                /* PLAY: its own 120 BPM */
    ck_run(3.0);
    TT_CHECK(ck_spaced(0u, 60.0 / 120.0 / SEQ_PPQ));
    ck_fresh(SEQ_CLK_MIDI);                    /* MIDI, no clock: PLAY waits */
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_run(1.0);
    TT_CHECK(ck_n_on == 0u && seq_waiting(&ck) && seq_clock_external(&ck));
    ck_ticks_ramp(1.0, 100.0, 100.0, 0, 1, 24u * 8u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    ck_run(ck_tick_at[24 + 6 * 8] - 0.012);
    TT_CHECK(!seq_waiting(&ck) && ck_n_on == 8u && ck_note[0] == 48u);
    TT_CHECK(fabs(ck_on[1] - ck_tick_at[30]) < 0.001);
    ck_fresh(SEQ_CLK_AUTO);                    /* AUTO, a clock coming (with no start, as many send it):
                                                * PLAY starts on its next tick, from step 1 (Eric) */
    ck_ticks_ramp(0.05, 100.0, 100.0, 0, 1, 24u * 8u);
    ck_msg_at(ck_tick_at[30] - 0.002, 0, CK_PLAY);
    ck_run(ck_tick_at[30 + 6 * 4] - 0.012);
    TT_CHECK(ck_n_on == 4u && !seq_waiting(&ck) && seq_clock_external(&ck) && ck_note[0] == 48u);
    TT_CHECK(fabs(ck_on[1] - ck_tick_at[36]) < 0.001);
    ck_msg_at(ck_tick_at[60] - 0.002, 0, 0xFAu);   /* and a start still starts again */
    ck_run(ck_tick_at[60 + 6 * 2] - 0.012);
    TT_CHECK(ck_note[ck_n_on - 2] == 48u);
    ck_fresh(SEQ_CLK_AUTO);                    /* AUTO, no clock: its own */
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_run(1.2);
    TT_CHECK(!seq_waiting(&ck) && !seq_clock_external(&ck) && ck_spaced(0u, 60.0 / 120.0 / SEQ_PPQ));
    ck_fresh(SEQ_CLK_MIDI);                    /* a second PLAY while waiting disarms */
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_msg_at(0.2, 0, CK_PLAY);
    ck_run(0.3);
    TT_CHECK(!seq_waiting(&ck) && !seq_playing(&ck));
}

/* #349: a stop and then PLAY (armed, the clock in charge) inside one block. The interrupt takes the
 * stop ask, and must stop: the transport waits for the next MIDI start, not plays on from the stop
 * (seq_play's armed path sets `want` with no ask of its own). */
TT_TEST(clock_stop_then_play_inside_one_block_stops)
{
    double at;
    uint32_t n;
    ck_fresh(SEQ_CLK_MIDI);
    ck_ticks_ramp(0.05, 100.0, 100.0, 0, 1, 24u * 16u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    ck_run(ck_tick_at[24 + 6 * 4] - 0.012);
    TT_CHECK(seq_playing(&ck) && ck_n_on == 4u);
    at = ck_tick_at[24 + 6 * 4] - 0.006;          /* two presses before the same block */
    ck_msg_at(at, 0, CK_PLAY);
    ck_msg_at(at + 0.0001, 0, CK_PLAY);
    n = ck_n_on;
    ck_run(ck_tick_at[24 + 6 * 12]);               /* the clock ticks on, no MIDI start */
    TT_CHECK(seq_waiting(&ck));                    /* armed (seq_playing says so too) */
    TT_CHECK_EQ(ck_n_on, n);                       /* and no step played */
}

/* #373: three asks before one block, under MIDI clock: a start (0xFA, then its tick: clock_tick asks to
 * play), then STOP, then PLAY (the clock in charge, so it only arms: `want` = 1, no ask). The interrupt
 * takes both asks at once and must not start: the last ask was the stop. The transport waits, armed, for
 * the next MIDI start; none comes, so no step plays. The presses go as ui_seq_transport's: STOP while
 * `want` is set, PLAY when it isn't. */
TT_TEST(clock_start_stop_then_play_inside_one_block_stays_armed)
{
    const double t0 = 0.1, dt = 60.0 / 100.0 / SEQ_CLK_PPQN;
    const uint32_t at = 24u;                      /* the start's tick, delivered by hand */
    uint32_t i;
    ck_fresh(SEQ_CLK_MIDI);
    for (i = 0; i < 24u * 8u; i++)
        if (i != at)
            ck_msg_at(t0 + i * dt, 0, 0xF8u);
    ck_run(t0 + at * dt);                         /* a running clock, not followed yet */
    TT_CHECK_EQ(ck_n_on, 0u);
    seq_clock_in(&ck, 0xFAu, ck_host(t0 + at * dt - 0.001));   /* the main loop, before one block: */
    seq_clock_in(&ck, 0xF8u, ck_host(t0 + at * dt));           /* a start and its tick ask to play */
    TT_CHECK(ck.want);
    ck_msg_at((double)ck_done / CK_FS - 2e-6, 0, CK_PLAY);    /* then two presses, both handled before the */
    ck_msg_at((double)ck_done / CK_FS - 1e-6, 0, CK_PLAY);    /* next block, as ui_seq_transport: STOP, PLAY */
    ck_run(t0 + 24u * 8u * dt);                   /* the clock ticks on, no MIDI start */
    TT_CHECK(seq_waiting(&ck));                   /* still armed */
    TT_CHECK(!ck.playing);
    TT_CHECK_EQ(ck_n_on, 0u);                     /* and no step played */
}

/* #349, kind C (the seqlock, the interrupt writing): the interrupt runs a block inside the main loop's
 * read of the block's time, once, mid-run (gap "blk" 1). The read takes the record again, whole, so the
 * steps stay on the steady clock's grid as with no gap: within 0.05 ms once locked (no jitter here) */
static uint32_t ck_blk_hits;
static void ck_gap_blk(const char *where, uint32_t n)
{
    static seq_event ev[SEQ_EVENTS];
    (void)n;
    if (strcmp(where, "blk") == 0 && ck_blk_hits++ == 0) {
        seq_block_time(&ck, ck_host((double)ck_done / CK_FS));
        (void)seq_render(&ck, CK_BLOCK, CK_FS, SEQ_NOTES, ev);
        ck_done += CK_BLOCK;
    }
}
TT_TEST(clock_read_of_the_block_time_inside_a_block_stays_on_the_grid)
{
    const double t0 = 0.1, dt = 60.0 / 120.0 / SEQ_CLK_PPQN;
    const uint32_t gap = 24u * 8u + 3u;           /* a tick mid-step (no step starts in its block) */
    uint32_t i;
    ck_fresh(SEQ_CLK_MIDI);
    ck_msg_at(0.05, 0, 0xFAu);
    for (i = 0; i < 24u * 20u; i++)
        if (i != gap)
            ck_msg_at(t0 + i * dt, 0, 0xF8u);
    ck_run(t0 + gap * dt);                        /* to the gap's tick, then it by hand */
    ck_blk_hits = 0;
    handoff_gap = ck_gap_blk;
    seq_clock_in(&ck, 0xF8u, ck_host(t0 + gap * dt));
    handoff_gap = 0;
    ck_run(t0 + 24u * 20u * dt - 0.012);
    printf("    a block inside the read of the block time: %u steps, worst %.3f ms off the grid\n",
           ck_n_on, ck_worst_ms(t0, 120.0, 4));
    TT_CHECK(ck_blk_hits >= 1u);                  /* (it did cut in) */
    TT_CHECK(ck_n_on == 80u);
    TT_CHECK(ck_worst_ms(t0, 120.0, 4) < 0.05);   /* (0.011; a read with no retry, 0.147) */
}

/* #349, kind B: the interrupt between an ask's payload (`want`) and its counter (gaps "play" 1 and "stop"
 * 0), on its own clock, PLAY and STOP called between blocks as the main loop does. That block takes no ask
 * (the counter hasn't moved), so it plays on or stays stopped; the next takes the ask: PLAY starts from
 * step 1, once, and STOP stops. */
static uint32_t ck_b_hits, ck_b_on;
static const char *ck_b_where;
static uint32_t ck_b_n;
static void ck_gap_b(const char *where, uint32_t n)
{
    static seq_event ev[SEQ_EVENTS];
    uint32_t got, k;
    if (strcmp(where, ck_b_where) == 0 && n == ck_b_n && ck_b_hits++ == 0) {
        seq_block_time(&ck, ck_host((double)ck_done / CK_FS));
        got = seq_render(&ck, CK_BLOCK, CK_FS, SEQ_NOTES, ev);
        for (k = 0; k < got; k++)               /* (that block's note-ons, which ck_run doesn't see) */
            ck_b_on += ev[k].kind == SEQ_EV_NOTE && ev[k].vel;
        ck_done += CK_BLOCK;
    }
}
TT_TEST(seq_ask_with_the_interrupt_between_want_and_its_count_is_taken_once)
{
    uint32_t n;
    double t;
    ck_fresh(SEQ_CLK_AUTO);                     /* no clock coming: its own */
    ck_run(0.2);
    ck_b_where = "play", ck_b_n = 1, ck_b_hits = 0, ck_b_on = 0;
    handoff_gap = ck_gap_b;
    seq_play(&ck);                              /* PLAY, the interrupt between want and ask_play */
    handoff_gap = 0;
    TT_CHECK_EQ(ck_b_hits, 1u);                 /* (it did cut in there) */
    TT_CHECK_EQ(ck_b_on, 0u);                   /* that block, no ask yet: nothing plays (no start twice) */
    t = (double)ck_done / CK_FS;
    ck_run(t + 1.0);                            /* 120 BPM: a step 125 ms */
    TT_CHECK(ck.playing);
    TT_CHECK(ck_n_on >= 7u && ck_n_on <= 9u);   /* step 1 at once, then one every 125 ms */
    TT_CHECK(ck_n_on && ck_on[0] - t < (double)CK_BLOCK / CK_FS + 1e-9);   /* the next block */
    TT_CHECK(ck_n_on && ck_note[0] == 48u);     /* from step 1 */
    ck_b_where = "stop", ck_b_n = 0, ck_b_hits = 0;
    handoff_gap = ck_gap_b;
    seq_stop(&ck);                              /* STOP, the interrupt between want and ask_stop */
    handoff_gap = 0;
    TT_CHECK_EQ(ck_b_hits, 1u);
    n = ck_n_on;
    ck_run((double)ck_done / CK_FS + 1.0);
    TT_CHECK(!ck.playing);
    TT_CHECK_EQ(ck_n_on, n);                    /* stopped: no step after */
}

/* SWING and GATE under clock: at 66 % the second step of each pair starts 1.32 steps into it, and a
 * 25 % gate lets go a quarter step in, of the clock's step (within 0.5 ms once locked) */
TT_TEST(clock_swing_and_gate_follow_the_clock)
{
    double dt = 60.0 / 100.0 / SEQ_CLK_PPQN, step = 6 * dt, worst_on = 0, worst_off = 0;
    uint32_t j;
    ck_fresh(SEQ_CLK_MIDI);
    seq_set_swing(&ck, 66);
    seq_set_gate(&ck, 25);
    ck_ticks_ramp(0.05, 100.0, 100.0, 0, 1, 24u + 24u * 16u + 1u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    ck_run(ck_tick_at[24 + 24 * 16] - 0.012);
    for (j = 8; j < ck_n_on && j < 64; j++) {
        double pair = ck_tick_at[24 + 12 * (j / 2)], on = pair + (j & 1u ? 1.32 * step : 0);
        double len = j & 1u ? 0.68 * step : 1.32 * step;
        worst_on = fmax(worst_on, fabs(ck_on[j] - on) * 1000);
        worst_off = fmax(worst_off, fabs(ck_off[j] - (on + 0.25 * len)) * 1000);
    }
    printf("    swing 66 %%, gate 25 %% at 100 BPM: worst %.3f ms on, %.3f ms off (%u steps)\n", worst_on,
           worst_off, ck_n_on);
    TT_CHECK(ck_n_on == 64u);
    TT_CHECK(worst_on < 0.5 && worst_off < 0.5);
}

/* A hiccup: ticks 96..104 are held up and come all at once with tick 105 (a USB stall of 208 ms, short
 * of the 250 ms time out). The steps wait for them rather than run on, none is lost or doubled, the tempo shown
 * doesn't jump, and 2 beats on they are back on the grid (1 ms). */
TT_TEST(clock_rides_out_a_burst_of_late_ticks)
{
    uint32_t i, lo = 0xFFFFFFFFu, hi = 0;
    ck_fresh(SEQ_CLK_MIDI);
    ck_ticks_ramp(0.05, 120.0, 120.0, 0, 1, 24u + 24u * 12u + 1u);
    for (i = 96; i < 105; i++)
        ck_msgs[i].stamp = ck_tick_at[105];
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    ck_watch_from = ck_tick_at[48];
    ck_run(ck_tick_at[24 + 24 * 12] - 0.012);
    lo = ck_tempo_lo, hi = ck_tempo_hi;
    printf("    a burst of 9 late ticks: %u steps, tempo shown %.1f..%.1f, %.3f ms off from 2 beats after\n", ck_n_on,
           lo / 10.0, hi / 10.0, ck_worst_ticks_ms(24u, 22u, 48u));
    TT_CHECK(ck_n_on == 48u);
    for (i = 0; i < ck_n_on; i++)
        TT_CHECK(ck_note[i] == 48u + (i % 16u));
    TT_CHECK(lo >= 1190u && hi <= 1210u);
    TT_CHECK(ck_worst_ticks_ms(24u, 22u, 48u) < 1.0);
}

/* Edits under the clock act as on the internal one: a pattern loaded while playing takes over after
 * the pass's last step, on its tick; LENGTH 4 wraps there; a transpose plays from the next step. */
TT_TEST(clock_edits_act_as_on_the_internal_clock)
{
    seq_pattern p;
    uint8_t n = 72;
    uint32_t i;
    ck_fresh(SEQ_CLK_MIDI);
    ck_ticks_ramp(0.05, 120.0, 120.0, 0, 1, 24u + 24u * 12u + 1u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    ck_run(ck_tick_at[24 + 6 * 4] - 0.003);    /* steps 1..4 played */
    seq_pattern_empty(&p);
    for (i = 0; i < SEQ_STEPS; i++)
        p.step[i].kind = SEQ_NOTES_ON, p.step[i].n = 1, p.step[i].note[0] = n;
    seq_load_pattern(&ck, &p);                 /* queued: after step 16 */
    ck_run(ck_tick_at[24 + 6 * 20] - 0.003);   /* to step 21 */
    TT_CHECK(ck_n_on == 20u && ck_note[15] == 48u + 15u && ck_note[16] == 72u);
    TT_CHECK(fabs(ck_on[16] - ck_tick_at[24 + 6 * 16]) < 0.0005);
    TT_CHECK(seq_main_tick(&ck));              /* (the main loop, each draw: it's the edit buffer now) */
    seq_set_length(&ck, 4);
    seq_set_transpose(&ck, 2);
    ck_run(ck_tick_at[24 + 6 * 32] - 0.012);
    printf("    edits under clock: %u steps, a loaded pattern from step 17 (%u), transposed to %u\n", ck_n_on,
           ck_note[16], ck_note[ck_n_on - 1]);
    TT_CHECK(ck_n_on == 32u && ck_note[ck_n_on - 1] == 74u);
    TT_CHECK(seq_step_now(&ck) < 4);
    TT_CHECK(fabs(ck_on[31] - ck_tick_at[24 + 6 * 31]) < 0.0005);
}

/* From the review: G13 to INT while the clock runs the pattern goes on, on the pattern's own tempo (it
 * doesn't wait for ticks it now ignores and stop); a continue with no start before it plays from step 1 */
TT_TEST(clock_to_int_plays_on_and_a_first_continue_starts)
{
    ck_fresh(SEQ_CLK_MIDI);
    seq_set_tempo(&ck, 900);
    ck_ticks_ramp(0.05, 120.0, 120.0, 0, 1, 24u * 24u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    ck_run(ck_tick_at[24 + 6 * 4] - 0.003);    /* 4 steps on the clock */
    seq_set_clock_source(&ck, SEQ_CLK_INT);
    ck_run(ck_tick_at[24 + 6 * 4] + 2.0);
    printf("    to INT mid-clock: %u steps, playing %u\n", ck_n_on, seq_playing(&ck));
    TT_CHECK(seq_playing(&ck) && ck_n_on > 12u);
    TT_CHECK(ck_spaced(5u, 60.0 / 90.0 / SEQ_PPQ));     /* its own 90 BPM */
    ck_fresh(SEQ_CLK_MIDI);
    ck_ticks_ramp(0.05, 120.0, 120.0, 0, 1, 24u * 8u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFBu);
    ck_run(ck_tick_at[24 + 6 * 2] - 0.012);
    TT_CHECK(ck_n_on == 2u && ck_note[0] == 48u);
}

/* Two first ticks a hair apart (1 us: a glitch, or two clocks at once) read no faster than the
 * shortest tick taken, 1 ms (2500.0 BPM), at any point after, so what shows the tempo never sees more
 * than 4 digits (agent1's review: 39761 BPM once overflowed a 4-digit buffer) */
TT_TEST(clock_tempo_shown_is_held_to_the_shortest_tick)
{
    uint32_t i, worst = 0;
    seq_init(&ck);
    seq_set_clock_source(&ck, SEQ_CLK_MIDI);
    seq_clock_in(&ck, 0xF8u, 1000000u);
    seq_clock_in(&ck, 0xF8u, 1000024u);         /* 24 counts: 1 us */
    worst = seq_clock_tempo(&ck);
    for (i = 2; i < 200; i++) {                 /* then a steady 120 BPM */
        uint32_t tp;
        seq_clock_in(&ck, 0xF8u, 1000024u + i * 500000u);
        tp = seq_clock_tempo(&ck);
        worst = tp > worst ? tp : worst;
    }
    printf("    two ticks 1 us apart: the tempo shown at most %.1f BPM\n", worst / 10.0);
    TT_CHECK(worst <= 25000u);
}

/* AUTO, a clock coming: PLAY arms for its next tick, but the clock goes before one comes. That is the
 * clock lost (spec: as a MIDI stop): disarmed, nothing waiting, back on INT; ticks coming again later
 * don't start it, and the next PLAY with no clock runs on the internal clock (Codex P2-a) */
TT_TEST(clock_lost_while_armed_disarms)
{
    double last;
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_tempo(&ck, 900);                   /* the pattern's: 90 BPM; the clock's 140 */
    last = ck_ticks(0.05, 140.0, 24, 0.0) - 60.0 / 140.0 / SEQ_CLK_PPQN;
    ck_msg_at(last + 0.001, 0, CK_PLAY);       /* armed, and then no tick */
    ck_run(last + 0.1);
    TT_CHECK(seq_waiting(&ck) && ck.want);
    ck_run(last + 0.6);                        /* gone (250 ms) */
    TT_CHECK(!seq_waiting(&ck));
    TT_CHECK(!ck.want);
    TT_CHECK(!seq_clock_external(&ck));
    (void)ck_ticks(2.0, 140.0, 48, 0.0);       /* ticks again: nothing starts */
    ck_run(2.0 + 48 * 60.0 / 140.0 / SEQ_CLK_PPQN + 0.4);
    TT_CHECK_EQ(ck_n_on, 0u);
    ck_msg_at(3.5, 0, CK_PLAY);                /* the clock gone again: PLAY plays its own 90 BPM */
    ck_run(5.5);
    TT_CHECK(seq_playing(&ck) && ck_n_on >= 4u);
    TT_CHECK(ck_spaced(0u, 60.0 / 90.0 / SEQ_PPQ));
}

/* G13 changed while PLAY waits: armed under MIDI and switched to AUTO, it starts on the clock's next
 * tick; armed under AUTO (for its next tick) and switched to MIDI, it waits for a start again, and
 * ticks alone don't start it (Codex P2-a) */
TT_TEST(clock_source_switched_while_armed)
{
    ck_fresh(SEQ_CLK_MIDI);
    ck_ticks_ramp(0.05, 140.0, 140.0, 0, 1, 24u * 8u);
    ck_msg_at(ck_tick_at[10] - 0.002, 0, CK_PLAY);   /* MIDI: armed, waiting for a start */
    ck_run(ck_tick_at[20] - 0.002);
    TT_CHECK(seq_waiting(&ck) && ck_n_on == 0u);
    seq_set_clock_source(&ck, SEQ_CLK_AUTO);   /* AUTO: on the next tick */
    ck_run(ck_tick_at[20 + 6 * 3] - 0.012);
    TT_CHECK(!seq_waiting(&ck) && ck_n_on == 3u && ck_note[0] == 48u);
    TT_CHECK(fabs(ck_on[1] - ck_tick_at[26]) < 0.001);      /* (locked: the second step on its tick) */
    ck_fresh(SEQ_CLK_AUTO);
    ck_ticks_ramp(0.05, 140.0, 140.0, 0, 1, 24u * 8u);
    ck_msg_at(ck_tick_at[10] + 0.002, 0, CK_PLAY);   /* AUTO: armed for the next tick ... */
    ck_run(ck_tick_at[10] + 0.003);
    seq_set_clock_source(&ck, SEQ_CLK_MIDI);   /* ... switched to MIDI before it: a start, then */
    ck_run(ck_tick_at[40] - 0.002);
    TT_CHECK(seq_waiting(&ck) && ck_n_on == 0u);
    ck_msg_at(ck_tick_at[40] - 0.001, 0, 0xFAu);
    ck_run(ck_tick_at[40 + 6 * 2] - 0.012);
    TT_CHECK(ck_n_on == 2u && ck_note[0] == 48u);
}

/* MIDI (G13) armed when the clock goes: it waits on, as MIDI does with no clock (spec), and a later
 * start and ticks start it from step 1 */
TT_TEST(clock_lost_while_armed_under_midi_waits_on)
{
    double last;
    ck_fresh(SEQ_CLK_MIDI);
    last = ck_ticks(0.05, 140.0, 24, 0.0);
    ck_msg_at(0.2, 0, CK_PLAY);
    ck_run(last + 0.6);                        /* the clock gone */
    TT_CHECK(seq_waiting(&ck) && ck.want && ck_n_on == 0u);
    ck_ticks_ramp(2.0, 140.0, 140.0, 0, 1, 24u * 4u);
    ck_msg_at(ck_tick_at[12] - 0.002, 0, 0xFAu);
    ck_run(ck_tick_at[12 + 6 * 2] - 0.012);
    TT_CHECK(!seq_waiting(&ck) && ck_n_on == 2u && ck_note[0] == 48u);
}

/* A start (or continue) that comes before any tick waits for the clock, and says so: WAITING FOR CLOCK
 * until the first tick starts it (MIDI or AUTO; Codex P2-c) */
TT_TEST(clock_start_before_any_tick_is_waiting)
{
    static const uint32_t SRC[2] = {SEQ_CLK_AUTO, SEQ_CLK_MIDI};
    uint32_t k;
    for (k = 0; k < 2u; k++) {
        ck_fresh(SRC[k]);
        ck_msg_at(0.1, 0, 0xFAu);              /* a start, no clock yet */
        ck_run(0.2);
        TT_CHECK(seq_waiting(&ck));
        TT_CHECK_EQ(ck_n_on, 0u);
        ck_ticks_ramp(0.3, 140.0, 140.0, 0, 1, 24u * 2u);
        ck_run(ck_tick_at[6 * 2] - 0.012);
        TT_CHECK(!seq_waiting(&ck) && ck_n_on >= 1u && ck_note[0] == 48u);
    }
}

/* PLAY that runs on the internal clock waits for nothing, whatever start came in before: armed under MIDI
 * with a start in and no tick, G13 to AUTO with no clock (it plays its own); or AUTO, a bare start with no
 * clock, then PLAY. Neither shows WAITING FOR CLOCK while its own steps play (review of 921ec2d) */
TT_TEST(clock_play_on_its_own_clock_is_not_waiting)
{
    ck_fresh(SEQ_CLK_MIDI);
    ck_msg_at(0.1, 0, CK_PLAY);                /* armed */
    ck_msg_at(0.2, 0, 0xFAu);                  /* a start, no tick */
    ck_run(0.3);
    seq_set_clock_source(&ck, SEQ_CLK_AUTO);   /* AUTO, no clock: its own */
    ck_run(1.5);
    TT_CHECK(seq_playing(&ck) && ck_n_on >= 2u);
    TT_CHECK(!seq_waiting(&ck));
    ck_fresh(SEQ_CLK_AUTO);
    ck_msg_at(0.1, 0, 0xFAu);                  /* AUTO: a bare start, no clock */
    ck_msg_at(0.15, 0, CK_PLAY);               /* PLAY: its own */
    ck_run(1.5);
    TT_CHECK(seq_playing(&ck) && ck_n_on >= 2u);
    TT_CHECK(!seq_waiting(&ck));
}

/* AUTO, a start with no clock ever after it: it waits as long as a clock takes to count as gone (250
 * ms), then no more (AUTO with no clock is its own clock, spec). Under MIDI it waits on (review of 921ec2d) */
TT_TEST(clock_auto_start_with_no_clock_stops_waiting)
{
    ck_fresh(SEQ_CLK_AUTO);
    ck_msg_at(0.1, 0, 0xFAu);
    ck_run(0.2);
    TT_CHECK(seq_waiting(&ck));
    ck_run(0.5);                               /* 400 ms on, no tick */
    TT_CHECK(!seq_waiting(&ck));
    TT_CHECK(!seq_playing(&ck) && ck_n_on == 0u);
    ck_fresh(SEQ_CLK_MIDI);
    ck_msg_at(0.1, 0, 0xFAu);
    ck_run(0.5);
    TT_CHECK(seq_waiting(&ck));
}

/* Armed under MIDI, a continue comes in, and G13 goes to AUTO before the next tick: it still continues
 * from where it stopped, not from step 1 (review of 921ec2d) */
TT_TEST(clock_switch_to_auto_keeps_a_continue)
{
    uint32_t n;
    ck_fresh(SEQ_CLK_MIDI);
    ck_ticks_ramp(0.05, 140.0, 140.0, 0, 1, 24u * 12u);
    ck_msg_at(ck_tick_at[12] - 0.002, 0, 0xFAu);   /* played: steps 1..4 */
    ck_msg_at(ck_tick_at[12 + 6 * 4] - 0.002, 0, 0xFCu);
    ck_msg_at(ck_tick_at[60] - 0.002, 0, CK_PLAY);  /* armed again */
    ck_msg_at(ck_tick_at[72] + 0.002, 0, 0xFBu);    /* a continue, then before the next tick: */
    ck_run(ck_tick_at[72] + 0.008);
    n = ck_n_on;                               /* (4 or 5: the stop came 2 ms before step 5's tick) */
    TT_CHECK(n >= 4u && n <= 5u);
    seq_set_clock_source(&ck, SEQ_CLK_AUTO);
    ck_run(ck_tick_at[73 + 6 * 2] - 0.012);
    TT_CHECK(ck_n_on > n);
    TT_CHECK_EQ(ck_note[n], 48u + n);           /* the step after the last played, not step 1 */
}

/* the out bytes of kind st, how many */
static uint32_t ck_outs(uint8_t st)
{
    uint32_t i, c = 0;
    for (i = 0; i < ck_n_out; i++)
        c += ck_out[i] == st;
    return c;
}

/* G14 CLOCK OUT on, the sequencer on its own clock (120 BPM, a step 125 ms): a start, then 24 ticks a
 * quarter, 6 a step, the tick on each step's start taken in the same block as its note (#286) */
TT_TEST(clock_out_ticks_with_the_steps)
{
    uint32_t j, k, ticks;
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_clock_out(&ck, 1);
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_run(0.1 + 8 * 0.125 - 0.02);            /* steps 1..8 */
    TT_CHECK(ck_n_out > 0u && ck_out[0] == 0xFAu);
    TT_CHECK_EQ(ck_n_on, 8u);
    ticks = ck_outs(0xF8u);
    TT_CHECK(ticks >= 6u * 7u + 1u && ticks <= 6u * 8u);
    for (j = 0, k = 0; k < ck_n_out && j < ck_n_on; k++) {    /* tick 6j: in step j's note's block */
        uint32_t n, i;
        for (i = 0, n = 0; i <= k; i++)
            n += ck_out[i] == 0xF8u;
        if (ck_out[k] == 0xF8u && n == 6u * j + 1u) {
            TT_CHECK(fabs(ck_out_t[k] - ck_on[j]) <= (double)CK_BLOCK / CK_FS + 1e-9);
            j++;
        }
    }
    TT_CHECK_EQ(j, 8u);
}

/* #373 (FW review): STOP and then PLAY on its own clock before one block, with the interrupt between
 * PLAY's last-ask and its count (gap "play" 1). That block sees the stop's count alone, so it stops; the
 * next takes the play and starts from step 1, once. Clock out (G14) shows it: one stop, one start more,
 * never a restart twice (FC FA FC FA). */
TT_TEST(seq_stop_then_play_with_the_interrupt_before_its_count_starts_once)
{
    ck_fresh(SEQ_CLK_AUTO);                     /* no clock coming: its own */
    seq_set_clock_out(&ck, 1);
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_run(0.6);
    TT_CHECK_EQ(ck_outs(0xFAu), 1u);
    seq_stop(&ck);                              /* STOP, not taken yet */
    ck_b_where = "play", ck_b_n = 1, ck_b_hits = 0, ck_b_on = 0;
    handoff_gap = ck_gap_b;
    seq_play(&ck);                              /* PLAY, the interrupt before its count */
    handoff_gap = 0;
    TT_CHECK_EQ(ck_b_hits, 1u);
    TT_CHECK_EQ(ck_b_on, 0u);                   /* that block: the stop, no start */
    ck_run((double)ck_done / CK_FS + 1.0);
    TT_CHECK(ck.playing);
    TT_CHECK_EQ(ck_outs(0xFCu), 1u);            /* one stop */
    TT_CHECK_EQ(ck_outs(0xFAu), 2u);            /* and one start more, not two */
}

/* G14 on: PLAY sends a start and STOP a stop, after the ticks before it; PLAY again, a start again. Never
 * two starts or two stops in a row; no tick after a stop till the next start; a stop with nothing
 * playing sends nothing (#286) */
TT_TEST(clock_out_start_and_stop)
{
    uint32_t i;
    uint8_t last = 0;
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_clock_out(&ck, 1);
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_msg_at(0.6, 0, CK_PLAY);                /* stop */
    ck_msg_at(0.8, 0, CK_PLAY);                /* again */
    ck_msg_at(1.2, 0, CK_PLAY);                /* stop */
    ck_run(1.6);
    TT_CHECK_EQ(ck_outs(0xFAu), 2u);
    TT_CHECK_EQ(ck_outs(0xFCu), 2u);
    TT_CHECK_EQ(ck_out[0], 0xFAu);
    TT_CHECK_EQ(ck_out[ck_n_out - 1u], 0xFCu);  /* (nothing after the last stop) */
    for (i = 0; i < ck_n_out; i++) {
        if (ck_out[i] == 0xFAu || ck_out[i] == 0xFCu) {
            TT_CHECK(ck_out[i] != last);        /* start, stop, start, stop */
            last = ck_out[i];
        } else {
            TT_CHECK_EQ(last, 0xFAu);           /* a tick only between a start and its stop */
        }
    }
    seq_stop(&ck);                              /* stopped already: nothing more */
    ck_run(1.8);
    TT_CHECK_EQ(ck_outs(0xFCu), 2u);
}

/* #349: G14 on, its own clock, a STOP and then the module let go (seq_clock_out_stop) inside one block,
 * with the interrupt cutting in between its read of out_on and its clear (gap "out_stop" 1): one MIDI
 * stop, not two. The same with a restart (PLAY asked again) pending instead of the stop. */
static uint32_t ck_gap_hits, ck_gap_at;
static void ck_gap_block(const char *where, uint32_t n)
{
    static seq_event ev[SEQ_EVENTS];
    if (strcmp(where, "out_stop") == 0 && n == ck_gap_at && ck_gap_hits++ == 0) {
        seq_block_time(&ck, ck_host((double)ck_done / CK_FS));
        (void)seq_render(&ck, CK_BLOCK, CK_FS, SEQ_NOTES, ev);
    }
}
TT_TEST(clock_out_stop_inside_a_pending_ask_sends_one_stop)
{
    uint8_t b;
    uint32_t k, g;
    for (g = 0; g < 2u; g++)                    /* gap 0: after out_block; 1: between out_on's read and clear */
        for (k = 0; k < 2u; k++) {
            ck_fresh(SEQ_CLK_AUTO);
            seq_set_clock_out(&ck, 1);
            ck_msg_at(0.1, 0, CK_PLAY);
            ck_run(0.5);
            TT_CHECK_EQ(ck_outs(0xFAu), 1u);
            if (k == 0)
                seq_stop(&ck);                  /* STOP: the interrupt is to send its stop */
            else
                seq_play(&ck);                  /* PLAY again: a restart, stop then start */
            ck_gap_at = g;
            ck_gap_hits = 0;
            handoff_gap = ck_gap_block;
            seq_clock_out_stop(&ck);            /* let go, the interrupt inside it */
            handoff_gap = 0;
            TT_CHECK_EQ(ck_gap_hits, 1u);       /* (it did cut in there) */
            seq_stop(&ck);                      /* then release's seq_stop and its one drain */
            while (seq_clock_out_take(&ck, &b) && ck_n_out < 8192u)
                ck_out[ck_n_out++] = b;
            if (ck_outs(0xFCu) != 1u || ck_outs(0xFAu) != 1u)
                printf("    gap %u, %s pending: %u stops, %u starts\n", (unsigned)g, k ? "a restart" : "a stop",
                       (unsigned)ck_outs(0xFCu), (unsigned)ck_outs(0xFAu));
            TT_CHECK_EQ(ck_outs(0xFCu), 1u);
            TT_CHECK_EQ(ck_outs(0xFAu), 1u);    /* (and no start after it) */
            TT_CHECK_EQ(ck_out[ck_n_out - 1u], 0xFCu);
        }
}

/* #349: the let-go in every order. A pending ask (none, STOP, a restart, PLAY from stopped) and one
 * interrupt block landing anywhere: before seq_clock_out_stop, at its gaps 0 and 1, between it and
 * release's seq_stop, between that and the one drain, or not at all; on its own clock and followed.
 * Whatever the order: starts and stops alternate, as many of each, and clock out ends stopped. */
static uint32_t ck_mx_where, ck_mx_hits;
static void ck_mx_block(void)
{
    static seq_event ev[SEQ_EVENTS];
    ck_mx_hits++;
    seq_block_time(&ck, ck_host((double)ck_done / CK_FS));
    (void)seq_render(&ck, CK_BLOCK, CK_FS, SEQ_NOTES, ev);
    ck_done += CK_BLOCK;
}
static void ck_mx_gap(const char *where, uint32_t n)
{
    if (strcmp(where, "out_stop") == 0 && n + 1u == ck_mx_where && !ck_mx_hits)
        ck_mx_block();
}
/* 1 when starts (FA, or a continue's FB) and stops (FC) alternate from a start, as many of each */
static int ck_alternate(void)
{
    uint32_t i, fa = 0, fc = 0;
    uint8_t last = 0xFCu;
    for (i = 0; i < ck_n_out; i++) {
        uint8_t st = ck_out[i] == 0xFBu ? 0xFAu : ck_out[i];
        if (st != 0xFAu && st != 0xFCu)
            continue;
        if (st == last)
            return 0;
        last = st;
        fa += last == 0xFAu;
        fc += last == 0xFCu;
    }
    return fa == fc;
}
TT_TEST(clock_out_let_go_in_every_order_ends_stopped)
{
    uint8_t b;
    uint32_t ext, ask, where, bad = 0, n = 0, on;
    for (ext = 0; ext < 2u; ext++)
        for (ask = 0; ask < 4u; ask++)
            for (where = 0; where < 6u; where++) {  /* 0 before, 1 gap 0, 2 gap 1 (there only with out_on set at
                                                     * the call: it returns before it otherwise), 3 after, 4
                                                     * before the drain, 5 none */
                ck_fresh(SEQ_CLK_AUTO);
                seq_set_clock_out(&ck, 1);
                if (ext) {                          /* followed: a clock and its start */
                    ck_ticks_ramp(0.05, 140.0, 140.0, 0, 1, 24u * 2u);
                    ck_msg_at(ck_tick_at[12] - 0.002, 0, 0xFAu);
                    ck_run(ck_tick_at[24u * 2u - 1u] - 0.005);
                } else {
                    ck_msg_at(0.1, 0, CK_PLAY);
                    ck_run(0.5);
                }
                if (ask == 1u)
                    seq_stop(&ck);
                else if (ask == 2u)
                    seq_play(&ck);
                else if (ask == 3u) {               /* stopped, then PLAY */
                    seq_stop(&ck);
                    ck_mx_hits = 0;
                    ck_mx_block();
                    seq_play(&ck);
                }
                ck_mx_where = where;
                ck_mx_hits = 0;
                if (where == 0u)
                    ck_mx_block();
                on = ck.out_on;                     /* (gap 1 is there only while it is set) */
                handoff_gap = ck_mx_gap;
                seq_clock_out_stop(&ck);            /* release, as fm1_8p.c's */
                handoff_gap = 0;
                if (where == 3u)
                    ck_mx_block();
                seq_stop(&ck);
                if (where == 4u)
                    ck_mx_block();
                while (seq_clock_out_take(&ck, &b) && ck_n_out < 8192u)
                    ck_out[ck_n_out++] = b;
                n++;
                if ((where < 5u && (where != 2u || on) && ck_mx_hits != 1u) || ck_mx_hits > 1u || !ck_alternate() ||
                    (ck_outs(0xFAu) && ck_out[ck_n_out - 1u] != 0xFCu)) {
                    bad++;
                    printf("    let go, %s, ask %u, block at %u: %u starts, %u stops, last %02X\n", ext ? "followed" : "own",
                           (unsigned)ask, (unsigned)where, (unsigned)ck_outs(0xFAu), (unsigned)ck_outs(0xFCu),
                           ck_n_out ? (unsigned)ck_out[ck_n_out - 1u] : 0u);
                }
            }
    printf("    let go: %u orders, %u wrong\n", (unsigned)n, (unsigned)bad);
    TT_CHECK_EQ(bad, 0u);
}

/* #364: G14 on, its own clock playing, a STOP (or a restart) asked and not yet taken when a MIDI start and
 * its tick come in: clock_tick's followed start stops the own run's clock out first. The interrupt cuts
 * in between its read of out_on and its clear (gap "clock_start" 0) and takes the pending ask. One MIDI
 * stop between the own run's start and the followed run's, never two: FA FC FA. */
TT_TEST(clock_start_with_an_ask_pending_sends_one_stop)
{
    uint32_t k;
    for (k = 0; k < 2u; k++) {
        ck_fresh(SEQ_CLK_AUTO);                 /* no clock yet: its own */
        seq_set_clock_out(&ck, 1);
        ck_msg_at(0.1, 0, CK_PLAY);
        ck_run(0.5);
        TT_CHECK_EQ(ck_outs(0xFAu), 1u);
        if (k == 0)
            seq_stop(&ck);                      /* STOP, not taken yet */
        else
            seq_play(&ck);                      /* PLAY again (a restart), not taken yet */
        seq_clock_in(&ck, 0xFAu, ck_host(0.5)); /* a MIDI start, then its tick: */
        ck_b_where = "clock_start", ck_b_n = 0, ck_b_hits = 0, ck_b_on = 0;
        handoff_gap = ck_gap_b;
        seq_clock_in(&ck, 0xF8u, ck_host(0.501));   /* clock_tick's start, the interrupt inside it */
        handoff_gap = 0;
        TT_CHECK_EQ(ck_b_hits, 1u);             /* (it did cut in there) */
        ck_ticks_ramp(0.52, 140.0, 140.0, 0, 1, 24u);
        ck_run(0.52 + 23 * 60.0 / 140.0 / SEQ_CLK_PPQN + 0.01);
        {                                       /* the starts and stops, ticks left out: FA FC FA */
            uint32_t q, m = 0;
            uint8_t seen[4] = { 0, 0, 0, 0 };
            for (q = 0; q < ck_n_out; q++)
                if (ck_out[q] != 0xF8u && m < 4u)
                    seen[m++] = ck_out[q];
            if (m != 3u || seen[0] != 0xFAu || seen[1] != 0xFCu || seen[2] != 0xFAu)
                printf("    %s pending: %u starts and stops, %02X %02X %02X %02X\n", k ? "a restart" : "a stop",
                       (unsigned)m, seen[0], seen[1], seen[2], seen[3]);
            TT_CHECK_EQ(m, 3u);                 /* the own run's start, its stop once, the followed start */
            TT_CHECK(seen[0] == 0xFAu && seen[1] == 0xFCu && seen[2] == 0xFAu);
        }
    }
}

/* #364: the followed start in every order. Before a MIDI start and its tick: nothing pending, a STOP, a
 * restart, or a let-go (seq_clock_out_stop); the interrupt cuts into clock_tick's start at each of its gaps
 * "clock_start" 0..4. Starts (FA, FB) and stops (FC) alternate from a start; with a run going the last is
 * the followed run's start, one stop fewer than starts; after a let-go nothing more goes out (FA FC). */
TT_TEST(clock_start_in_every_order_alternates)
{
    uint32_t ask, gap, bad = 0;
    for (ask = 0; ask < 4u; ask++)
        for (gap = 0; gap < 5u; gap++) {
            uint32_t q, m = 0, fa = 0, fc = 0, ok = 1;
            uint8_t last = 0xFCu;
            ck_fresh(SEQ_CLK_AUTO);
            seq_set_clock_out(&ck, 1);
            ck_msg_at(0.1, 0, CK_PLAY);
            ck_run(0.5);
            if (ask == 1u)
                seq_stop(&ck);
            else if (ask == 2u)
                seq_play(&ck);
            else if (ask == 3u)
                seq_clock_out_stop(&ck);
            seq_clock_in(&ck, 0xFAu, ck_host(0.5));
            ck_b_where = "clock_start", ck_b_n = gap, ck_b_hits = 0, ck_b_on = 0;
            handoff_gap = ck_gap_b;
            seq_clock_in(&ck, 0xF8u, ck_host(0.501));
            handoff_gap = 0;
            ck_ticks_ramp(0.52, 140.0, 140.0, 0, 1, 24u);
            ck_run(0.52 + 23 * 60.0 / 140.0 / SEQ_CLK_PPQN + 0.01);
            for (q = 0; q < ck_n_out; q++) {
                uint8_t st = ck_out[q] == 0xFBu ? 0xFAu : ck_out[q];
                if (st != 0xFAu && st != 0xFCu)
                    continue;
                ok &= st != last;
                last = st;
                fa += st == 0xFAu;
                fc += st == 0xFCu;
                m++;
            }
            if (ask == 3u)
                ok &= fa == 1u && fc == 1u;         /* let go: FA FC, then nothing */
            else
                ok &= last == 0xFAu && fc + 1u == fa;   /* the followed run's start last */
            ok &= ck_b_hits == ((ask == 3u && gap == 0u) ? 0u : 1u);   /* (gap 0 needs out_on set) */
            if (!ok) {
                bad++;
                printf("    clock start, ask %u, gap %u: %u starts, %u stops, %u hits\n", (unsigned)ask,
                       (unsigned)gap, (unsigned)fa, (unsigned)fc, (unsigned)ck_b_hits);
            }
            (void)m;
        }
    TT_CHECK_EQ(bad, 0u);
}

/* #377: an ask's last-ask record ahead of its count, trusted on another ask's count. Own clock, G14 on.
 * (1) Stopped; PLAY, STOP, PLAY before one block (ui_seq_transport's toggle: want 0, 1, 0), the interrupt
 * inside the second PLAY (gap "play" 1): that block sees the first play's and the stop's counts, with the
 * second play's record already written, and must start nothing; one start follows. (2) Playing, a restart
 * asked when a MIDI start and its tick come in, the interrupt at "clock_start" 2 (the followed start's
 * payload written, its count not yet): that block must not start the followed run early; one start does. */
TT_TEST(seq_a_last_ask_ahead_of_its_count_starts_once)
{
    uint32_t fa;
    ck_fresh(SEQ_CLK_AUTO);                     /* (1) its own clock */
    seq_set_clock_out(&ck, 1);
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_msg_at(0.3, 0, CK_PLAY);                 /* stop: stopped */
    ck_run(0.5);
    TT_CHECK(!ck.playing && !ck.want);
    fa = ck_outs(0xFAu);
    seq_play(&ck);                              /* PLAY */
    TT_CHECK(ck.want);
    seq_stop(&ck);                              /* STOP */
    TT_CHECK(!ck.want);
    ck_b_where = "play", ck_b_n = 1, ck_b_hits = 0, ck_b_on = 0;
    handoff_gap = ck_gap_b;
    seq_play(&ck);                              /* PLAY, the interrupt before its count */
    handoff_gap = 0;
    TT_CHECK_EQ(ck_b_hits, 1u);
    TT_CHECK_EQ(ck_b_on, 0u);                   /* that block: no start */
    ck_run((double)ck_done / CK_FS + 0.5);
    TT_CHECK(ck.playing);
    TT_CHECK_EQ(ck_outs(0xFAu), fa + 1u);       /* one start */
    ck_fresh(SEQ_CLK_AUTO);                     /* (2) playing on its own clock */
    seq_set_clock_out(&ck, 1);
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_run(0.5);
    seq_play(&ck);                              /* PLAY again: a restart, not taken yet */
    seq_clock_in(&ck, 0xFAu, ck_host(0.5));
    ck_b_where = "clock_start", ck_b_n = 2, ck_b_hits = 0, ck_b_on = 0;
    handoff_gap = ck_gap_b;
    seq_clock_in(&ck, 0xF8u, ck_host(0.501));   /* clock_tick's start, the interrupt at gap 2 */
    handoff_gap = 0;
    TT_CHECK_EQ(ck_b_hits, 1u);
    TT_CHECK_EQ(ck_b_on, 0u);                   /* that block: not the followed run yet, nor the restart */
    ck_ticks_ramp(0.52, 140.0, 140.0, 0, 1, 24u);
    ck_run(0.52 + 23 * 60.0 / 140.0 / SEQ_CLK_PPQN + 0.01);
    TT_CHECK(ck.playing && ck.ext);             /* then the followed run, */
    TT_CHECK_EQ(ck_outs(0xFAu), 2u);            /* started once: FA (own) FC FA (followed) */
    TT_CHECK_EQ(ck_outs(0xFCu), 1u);
}

/* #380: G14 on, stopped, MIDI clock: a MIDI start and its tick start the followed run (the main loop
 * sends its start, FA, and asks to play), then the clock source goes to INT before the interrupt takes the
 * ask (ext 0, no ask of its own). The run goes on on its own clock, and clock out says so once: one FA, its
 * ticks after it, no second start. */
TT_TEST(clock_source_to_int_with_a_start_pending_sends_one_start)
{
    uint32_t q, fa = 0, f8_after = 0;
    ck_fresh(SEQ_CLK_MIDI);
    seq_set_clock_out(&ck, 1);
    ck_run(0.2);
    seq_clock_in(&ck, 0xFAu, ck_host(0.2));
    seq_clock_in(&ck, 0xF8u, ck_host(0.201));   /* the followed start: FA, and the ask */
    seq_set_clock_source(&ck, SEQ_CLK_INT);     /* before one block: INT */
    ck_run(1.0);
    TT_CHECK(ck.playing && !ck.ext);            /* on, on its own clock */
    for (q = 0; q < ck_n_out; q++) {
        fa += ck_out[q] == 0xFAu || ck_out[q] == 0xFBu;
        f8_after += fa && ck_out[q] == 0xF8u;
    }
    if (fa != 1u)
        printf("    INT with a start pending: %u starts\n", (unsigned)fa);
    TT_CHECK_EQ(fa, 1u);                        /* one start, */
    TT_CHECK(f8_after > 0u);                    /* and its clock after it */
    TT_CHECK_EQ(ck_outs(0xFCu), 0u);
    {                                           /* tick 0 once: the main loop passed it on with the start, */
        uint32_t first = 0, n0 = 0;             /* so no second tick in the same block (FW review) */
        for (q = 0; q < ck_n_out; q++)
            if (ck_out[q] == 0xF8u) {
                if (!n0++)
                    first = q;
                else if (ck_out_t[q] - ck_out_t[first] < 1e-9)
                    printf("    INT with a start pending: a second tick with tick 0\n");
            }
        TT_CHECK(n0 >= 2u && ck_out_t[first + 1u] - ck_out_t[first] > 1e-9);
    }
    seq_stop(&ck);                              /* and a stop after it closes the run */
    ck_run(1.2);
    TT_CHECK_EQ(ck_outs(0xFCu), 1u);
}

/* #380 (FW review): a MIDI continue (FB), then INT before the interrupt took it: the run goes on from the
 * step after the last, as a continue says, with clock out on (the FB passed on) or off. And a PLAY after
 * them, before the same block, is the last ask: from step 1. */
static uint32_t ck_continue_then_int(uint32_t out, uint32_t play_too)
{
    static const uint8_t N[4] = {60, 62, 64, 65};
    uint32_t k, at;
    ck_fresh(SEQ_CLK_MIDI);
    seq_set_clock_out(&ck, out);
    seq_set_length(&ck, 4);
    for (k = 0; k < 4u; k++)
        seq_set_step(&ck, k, &N[k], 1, 100);  /* 60 62 64 65: each step its own */
    ck_ticks_ramp(0.05, 120.0, 120.0, 0, 1, 24u);
    ck_msg_at(ck_tick_at[0] - 0.002, 0, 0xFAu);
    ck_run(ck_tick_at[6] + 0.005);              /* steps 1 and 2 played (60, 62) */
    ck_msg_at(ck_tick_at[6] + 0.006, 0, 0xFCu);
    ck_run(ck_tick_at[6] + 0.1);                /* stopped after step 2 */
    if (ck.playing)
        return 0;
    at = ck_n_on;
    seq_clock_in(&ck, 0xFBu, ck_host(0.5));     /* a continue, its tick: asked */
    seq_clock_in(&ck, 0xF8u, ck_host(0.501));
    seq_set_clock_source(&ck, SEQ_CLK_INT);     /* before one block */
    if (play_too) {
        seq_stop(&ck);                          /* PLAY twice as the UI toggles: STOP (want set), PLAY */
        seq_play(&ck);
    }
    ck_run(0.6);
    return ck_n_on > at ? ck_note[at] : 0u;
}
TT_TEST(clock_source_to_int_with_a_continue_pending_goes_on)
{
    TT_CHECK_EQ(ck_continue_then_int(1, 0), 64u);   /* step 3, the one after the last, clock out on, */
    TT_CHECK_EQ(ck_continue_then_int(0, 0), 64u);   /* and off */
    TT_CHECK_EQ(ck_continue_then_int(1, 1), 60u);   /* a PLAY after it: from step 1 */
    TT_CHECK_EQ(ck_continue_then_int(0, 1), 60u);
}

/* #380 (FW review): the same start pending, INT, and then STOP before the interrupt takes either. The
 * start went out (the main loop sent it): one stop closes it, FA FC. */
TT_TEST(clock_source_to_int_then_stop_with_a_start_pending_closes_it)
{
    ck_fresh(SEQ_CLK_MIDI);
    seq_set_clock_out(&ck, 1);
    ck_run(0.2);
    seq_clock_in(&ck, 0xFAu, ck_host(0.2));
    seq_clock_in(&ck, 0xF8u, ck_host(0.201));
    seq_set_clock_source(&ck, SEQ_CLK_INT);
    seq_stop(&ck);                              /* all before one block */
    ck_run(0.6);
    TT_CHECK(!ck.playing);
    TT_CHECK_EQ(ck_outs(0xFAu), 1u);
    TT_CHECK_EQ(ck_outs(0xFCu), 1u);            /* the start closed */
}

/* A restart from step 1 while playing (a play asked again with no stop between) sends a stop, then the
 * start: never two starts in a row (#286) */
TT_TEST(clock_out_restart_stops_first)
{
    uint32_t i;
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_clock_out(&ck, 1);
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_run(0.4);
    seq_play(&ck);                             /* again, playing: from step 1 */
    ck_run(0.7);
    TT_CHECK_EQ(ck_outs(0xFAu), 2u);
    TT_CHECK_EQ(ck_outs(0xFCu), 1u);
    for (i = 0; i + 1u < ck_n_out; i++)
        TT_CHECK(!(ck_out[i] == 0xFAu && ck_out[i + 1u] == 0xFAu));
}

/* G14 off: nothing goes out. A G14 change takes effect at the next PLAY (Eric, Q3): turned on while
 * playing, nothing till then; turned off while playing, that run keeps its ticks and ends with its stop */
TT_TEST(clock_out_off_and_changed_while_playing)
{
    uint32_t n;
    ck_fresh(SEQ_CLK_AUTO);                    /* off */
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_run(0.5);
    TT_CHECK_EQ(ck_n_out, 0u);
    seq_set_clock_out(&ck, 1);                 /* on, while playing: nothing yet */
    ck_run(0.8);
    TT_CHECK_EQ(ck_n_out, 0u);
    ck_msg_at(0.8, 0, CK_PLAY);                /* stop: nothing (it never started out) */
    ck_msg_at(1.0, 0, CK_PLAY);                /* the next PLAY: on now */
    ck_run(1.3);
    TT_CHECK(ck_n_out > 0u && ck_out[0] == 0xFAu);
    seq_set_clock_out(&ck, 0);                 /* off, while playing: this run goes on sending */
    n = ck_outs(0xF8u);
    ck_run(1.6);
    TT_CHECK(ck_outs(0xF8u) > n);
    ck_msg_at(1.6, 0, CK_PLAY);                /* and ends with its stop */
    ck_run(1.8);
    TT_CHECK_EQ(ck_out[ck_n_out - 1u], 0xFCu);
    n = ck_n_out;
    ck_msg_at(1.9, 0, CK_PLAY);                /* the next run: off */
    ck_run(2.3);
    TT_CHECK_EQ(ck_n_out, n);
}

/* TEMPO changed while playing, many times (120, 157.3, 91.7 ... BPM, each from a step's start): the clock
 * out follows, still 6 ticks a step, the tick on each step's start in that step's note's block, with no
 * drift after 64 steps (#286) */
TT_TEST(clock_out_follows_tempo_changes)
{
    static const uint32_t T[4] = {1200, 1573, 917, 1342};
    uint32_t j, k, n, i;
    double t = 0.1;
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_clock_out(&ck, 1);
    ck_msg_at(0.1, 0, CK_PLAY);
    for (i = 0; i < 16u; i++) {                /* a new tempo every 4 steps */
        seq_set_tempo(&ck, T[i % 4u]);
        t += 4.0 * 60.0 / (T[i % 4u] / 10.0) / SEQ_PPQ;
        ck_run(t - 0.03);
    }
    TT_CHECK(ck_n_on >= 60u);
    for (j = 0, k = 0, n = 0; k < ck_n_out && j < ck_n_on; k++) {
        if (ck_out[k] != 0xF8u)
            continue;
        if (n++ == 6u * j) {                   /* tick 6j: in step j's note's block */
            if (fabs(ck_out_t[k] - ck_on[j]) > (double)CK_BLOCK / CK_FS + 1e-9)
                printf("    step %u: its tick %.4f s, its note %.4f s\n", j, ck_out_t[k], ck_on[j]);
            TT_CHECK(fabs(ck_out_t[k] - ck_on[j]) <= (double)CK_BLOCK / CK_FS + 1e-9);
            j++;
        }
    }
    TT_CHECK(j >= 60u);
}

/* the clock bytes of a kind among ck_msgs that the main loop had taken by time `to` */
static uint32_t ck_msgs_of(uint8_t st, double from, double to)
{
    uint32_t i, c = 0;
    for (i = 0; i < ck_n_msgs; i++)
        c += ck_msgs[i].st == st && ck_msgs[i].stamp >= from && ck_msgs[i].stamp <= to;
    return c;
}

/* Following a clock (G13 MIDI) with G14 on (Eric, Q2: pass it on): the start goes out as the transport
 * starts on it, then each tick that comes in goes out once, at once (not timed anew, and none of its own:
 * a tick in isn't counted twice); a stop that comes in goes out as the transport stops (#286) */
TT_TEST(clock_out_passes_a_followed_clock_on)
{
    double stop;
    uint32_t in;
    ck_fresh(SEQ_CLK_MIDI);
    seq_set_clock_out(&ck, 1);
    ck_ticks_ramp(0.05, 133.0, 133.0, 0, 1, 24u * 6u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    stop = ck_tick_at[24 + 6 * 12] + 0.005;
    ck_msg_at(stop, 0, 0xFCu);
    ck_run(stop + 0.3);
    in = ck_msgs_of(0xF8u, ck_tick_at[24] - 0.001, stop);     /* the ticks from the start's on */
    TT_CHECK(ck_n_out > 0u && ck_out[0] == 0xFAu);
    TT_CHECK_EQ(ck_outs(0xF8u), in);           /* one out a tick in, while it ran on them */
    TT_CHECK_EQ(ck_outs(0xFAu), 1u);
    TT_CHECK_EQ(ck_outs(0xFCu), 1u);
    TT_CHECK_EQ(ck_out[ck_n_out - 1u], 0xFCu);
}

/* Following, a stop that comes right after a tick (in the same block): the tick goes out before the
 * stop, never after it; and a clock lost while running sends a stop too (#286) */
TT_TEST(clock_out_a_followed_stop_comes_after_its_ticks)
{
    uint32_t i, seen_stop = 0;
    ck_fresh(SEQ_CLK_MIDI);
    seq_set_clock_out(&ck, 1);
    ck_ticks_ramp(0.05, 133.0, 133.0, 0, 1, 24u * 4u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);
    ck_msg_at(ck_tick_at[48] + 0.001, 0, 0xFCu);
    ck_run(ck_tick_at[24 * 4 - 1] + 0.1);
    for (i = 0; i < ck_n_out; i++) {
        if (ck_out[i] == 0xFCu)
            seen_stop = 1;
        else
            TT_CHECK(!(seen_stop && ck_out[i] == 0xF8u));
    }
    TT_CHECK(seen_stop);
    ck_fresh(SEQ_CLK_AUTO);                    /* lost while running: a stop */
    seq_set_clock_out(&ck, 1);
    ck_ticks_ramp(0.05, 133.0, 133.0, 0, 1, 24u * 2u);
    ck_msg_at(ck_tick_at[12] - 0.002, 0, 0xFAu);
    ck_run(ck_tick_at[47] + 0.5);
    TT_CHECK_EQ(ck_outs(0xFCu), 1u);
    TT_CHECK_EQ(ck_out[ck_n_out - 1u], 0xFCu);
}

/* AUTO playing its own clock (G14 on), then a start and a clock come in and it hands over to them: its
 * own ticks, its stop, then the start, then the ticks passed on, one out a tick in; never two starts in a
 * row, none of its own ticks after its stop (#286) */
TT_TEST(clock_out_hand_over_to_a_clock)
{
    uint32_t i, fa = 0, fc_at = 0, fa_at = 0, in;
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_clock_out(&ck, 1);
    ck_msg_at(0.1, 0, CK_PLAY);                /* its own */
    ck_ticks_ramp(1.0, 140.0, 140.0, 0, 1, 24u * 4u);
    ck_msg_at(ck_tick_at[24] - 0.002, 0, 0xFAu);   /* (ticks before it: no start, it goes on its own) */
    ck_run(ck_tick_at[24 * 4 - 1] - 0.01);
    for (i = 0; i < ck_n_out; i++) {
        if (ck_out[i] == 0xFAu && ++fa == 2u)
            fa_at = i;
        if (ck_out[i] == 0xFCu && !fc_at)
            fc_at = i;
    }
    TT_CHECK_EQ(fa, 2u);
    TT_CHECK(fc_at > 0u && fc_at + 1u == fa_at);   /* its stop, then the start */
    in = ck_msgs_of(0xF8u, ck_tick_at[24] - 0.001, ck_tick_at[24 * 4 - 1] - 0.01);   /* (to where it ran) */
    for (i = fa_at + 1u; i < ck_n_out; i++)
        TT_CHECK_EQ(ck_out[i], 0xF8u);
    TT_CHECK_EQ(ck_n_out - fa_at - 1u, in);    /* one out a tick in, after the start */
}

/* Following, the clock lost (its stop queued by the main loop), then PLAY on its own clock before the
 * main loop has sent anything: the followed run's stop goes out before the new run's start, never after
 * (review of e2d7a23: a stop after the start leaves the gear down the line stopped while the FM-1 plays) */
TT_TEST(clock_out_a_followed_stop_before_a_new_start)
{
    uint32_t i, fc = 9999, fa2 = 0, fa = 0;
    double last;
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_clock_out(&ck, 1);
    ck_ticks_ramp(0.05, 140.0, 140.0, 0, 1, 24u * 2u);
    ck_msg_at(ck_tick_at[12] - 0.002, 0, 0xFAu);
    last = ck_tick_at[24u * 2u - 1u];
    ck_run(last + 0.01);
    seq_clock_poll(&ck, ck_host(last + 0.4));  /* gone: its stop, queued, not sent yet */
    seq_play(&ck);                             /* PLAY at once: its own */
    ck_run(last + 0.6);
    for (i = 0; i < ck_n_out; i++) {
        if (ck_out[i] == 0xFCu && fc == 9999u)
            fc = i;
        if (ck_out[i] == 0xFAu && ++fa == 2u)
            fa2 = i;
    }
    TT_CHECK_EQ(fa, 2u);
    TT_CHECK(fc < fa2);
}

/* The module switched away (release) while clock out runs: a stop goes out, once, and nothing after it
 * (review of e2d7a23: the gear down the line kept running) */
TT_TEST(clock_out_ends_with_a_stop_when_let_go)
{
    uint8_t b;
    uint32_t n;
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_clock_out(&ck, 1);
    ck_msg_at(0.1, 0, CK_PLAY);
    ck_run(0.5);
    seq_clock_out_stop(&ck);                   /* release */
    while (seq_clock_out_take(&ck, &b) && ck_n_out < 8192u)
        ck_out[ck_n_out++] = b;
    TT_CHECK_EQ(ck_out[ck_n_out - 1u], 0xFCu);
    TT_CHECK_EQ(ck_outs(0xFCu), 1u);
    n = ck_n_out;
    ck_run(0.8);                               /* (if the interrupt runs on: nothing more) */
    TT_CHECK_EQ(ck_n_out, n);
}

/* #349: the same with a STOP, or a restart (PLAY again), asked just before the let-go and not yet taken
 * by the interrupt: release sends what is due, once, with no block between (nothing drains after it),
 * so the stop goes out then, once (FW review of f6609ec: it was left to an interrupt that never ran) */
TT_TEST(clock_out_ends_with_a_stop_when_let_go_with_an_ask_pending)
{
    uint8_t b;
    uint32_t k;
    for (k = 0; k < 2u; k++) {
        ck_fresh(SEQ_CLK_AUTO);
        seq_set_clock_out(&ck, 1);
        ck_msg_at(0.1, 0, CK_PLAY);
        ck_run(0.5);
        if (k == 0)
            seq_stop(&ck);                     /* STOP, not yet taken */
        else
            seq_play(&ck);                     /* PLAY again, not yet taken */
        seq_clock_out_stop(&ck);               /* release, as fm1_8p.c's: then seq_stop, then the drain */
        seq_stop(&ck);
        while (seq_clock_out_take(&ck, &b) && ck_n_out < 8192u)
            ck_out[ck_n_out++] = b;
        TT_CHECK_EQ(ck_outs(0xFCu), 1u);
        TT_CHECK_EQ(ck_out[ck_n_out - 1u], 0xFCu);
    }
}

/* the interrupt cutting in at a gap of the main loop's clock_tick start (handoff_test.h, gap
 * "clock_start" 1-4): one block */
static uint32_t ck_hook_at, ck_hook_hits;
static void ck_hook(const char *where, uint32_t point)
{
    static seq_event ev[SEQ_EVENTS];
    if (strcmp(where, "clock_start") == 0 && point == ck_hook_at) {
        ck_hook_hits++;
        seq_block_time(&ck, ck_host((double)ck_done / CK_FS));
        (void)seq_render(&ck, CK_BLOCK, CK_FS, SEQ_NOTES, ev);
        ck_done += CK_BLOCK;
    }
}

/* G14 on, its own clock playing; STOP (its ask not yet taken), then a start and a clock come in, and the
 * interrupt cuts in anywhere in the main loop's start of the followed run: the clock out stops its own
 * run once, starts the followed one, and passes every tick on after that; never a stop after the start
 * (Codex on #296: FC FA FC, then no clock while the FM-1 played) */
TT_TEST(clock_out_a_stop_pending_when_a_clock_starts)
{
    uint32_t at, i, fa_at, fc_after;
    for (at = 1; at <= 4u; at++) {
        ck_fresh(SEQ_CLK_AUTO);
        seq_set_clock_out(&ck, 1);
        ck_msg_at(0.1, 0, CK_PLAY);
        ck_run(0.5);                           /* its own run, sending */
        seq_stop(&ck);                         /* STOP: asked, not yet taken */
        seq_clock_in(&ck, 0xFAu, ck_host(0.5));
        ck_hook_at = at;
        ck_hook_hits = 0;
        handoff_gap = ck_hook;
        seq_clock_in(&ck, 0xF8u, ck_host(0.501));   /* the start's tick: the interrupt cuts in at `at` */
        handoff_gap = 0;
        TT_CHECK_EQ(ck_hook_hits, 1u);         /* (it did cut in there) */
        ck_ticks_ramp(0.52, 140.0, 140.0, 0, 1, 24u);
        ck_run(0.52 + 23 * 60.0 / 140.0 / SEQ_CLK_PPQN + 0.01);
        for (i = 0, fa_at = 9999u; i < ck_n_out; i++)
            if (ck_out[i] == 0xFAu)
                fa_at = i;                     /* the last start */
        for (i = fa_at + 1u, fc_after = 0; i < ck_n_out; i++)
            fc_after += ck_out[i] == 0xFCu;
        if (fc_after || ck_n_out - fa_at - 1u < 24u)
            printf("    cut in at %u: %u stops after the start, %u ticks after it\n", at, fc_after,
                   ck_n_out - fa_at - 1u);
        TT_CHECK_EQ(fc_after, 0u);
        TT_CHECK(ck_n_out - fa_at - 1u >= 24u);   /* the start's tick and the 24 after, passed on */
    }
}

/* Let go (release) with a PLAY asked but not yet taken: no start goes out after, so no gear is left
 * started; the next PLAY (the module back) sends again (Codex P3 on #296) */
TT_TEST(clock_out_let_go_with_a_play_pending)
{
    ck_fresh(SEQ_CLK_AUTO);
    seq_set_clock_out(&ck, 1);
    static seq_event ev[SEQ_EVENTS];
    uint8_t b;
    seq_play(&ck);                             /* asked, the interrupt hasn't taken it */
    seq_clock_out_stop(&ck);                   /* release: */
    seq_block_time(&ck, 0);                    /* (the interrupt cuts in: takes the PLAY) */
    (void)seq_render(&ck, CK_BLOCK, CK_FS, SEQ_NOTES, ev);
    seq_stop(&ck);
    while (seq_clock_out_take(&ck, &b) && ck_n_out < 8192u)   /* release drains, then nothing more */
        ck_out[ck_n_out++] = b;
    TT_CHECK(ck_n_out == 0u || ck_out[ck_n_out - 1u] == 0xFCu);   /* nothing, or ended with a stop */
    TT_CHECK_EQ(ck_outs(0xFAu), 0u);           /* (nothing started) */
    ck_done += CK_BLOCK;
    ck_run(0.3);
    ck_msg_at(0.3, 0, CK_PLAY);                /* back: PLAY sends again */
    ck_run(0.6);
    TT_CHECK(ck_n_out > 0u && ck_out[0] == 0xFAu);
}

static void run_seq_clock_tests(void)
{
    TT_RUN(clock_out_ticks_with_the_steps);
    TT_RUN(clock_out_stop_inside_a_pending_ask_sends_one_stop);
    TT_RUN(clock_out_start_and_stop);
    TT_RUN(clock_source_to_int_with_a_start_pending_sends_one_start);
    TT_RUN(clock_source_to_int_then_stop_with_a_start_pending_closes_it);
    TT_RUN(clock_source_to_int_with_a_continue_pending_goes_on);
    TT_RUN(clock_start_with_an_ask_pending_sends_one_stop);
    TT_RUN(clock_start_in_every_order_alternates);
    TT_RUN(seq_a_last_ask_ahead_of_its_count_starts_once);
    TT_RUN(clock_out_restart_stops_first);
    TT_RUN(clock_out_off_and_changed_while_playing);
    TT_RUN(clock_out_follows_tempo_changes);
    TT_RUN(clock_out_passes_a_followed_clock_on);
    TT_RUN(clock_out_a_followed_stop_comes_after_its_ticks);
    TT_RUN(clock_out_hand_over_to_a_clock);
    TT_RUN(clock_out_a_followed_stop_before_a_new_start);
    TT_RUN(clock_out_ends_with_a_stop_when_let_go);
    TT_RUN(clock_out_ends_with_a_stop_when_let_go_with_an_ask_pending);
    TT_RUN(clock_out_let_go_in_every_order_ends_stopped);
    TT_RUN(clock_out_a_stop_pending_when_a_clock_starts);
    TT_RUN(clock_out_let_go_with_a_play_pending);
    TT_RUN(clock_lost_while_armed_disarms);
    TT_RUN(clock_source_switched_while_armed);
    TT_RUN(clock_lost_while_armed_under_midi_waits_on);
    TT_RUN(clock_start_before_any_tick_is_waiting);
    TT_RUN(clock_play_on_its_own_clock_is_not_waiting);
    TT_RUN(clock_auto_start_with_no_clock_stops_waiting);
    TT_RUN(clock_switch_to_auto_keeps_a_continue);
    TT_RUN(clock_steps_follow_a_steady_clock);
    TT_RUN(clock_tempo_shown_is_held_to_the_shortest_tick);
    TT_RUN(clock_steps_ride_out_a_jittery_clock);
    TT_RUN(clock_measures_the_tempo_over_its_range);
    TT_RUN(clock_follows_tempo_changes);
    TT_RUN(clock_start_stop_and_continue);
    TT_RUN(clock_lost_stops_and_the_internal_clock_takes_over);
    TT_RUN(clock_source_int_midi_and_auto);
    TT_RUN(clock_read_of_the_block_time_inside_a_block_stays_on_the_grid);
    TT_RUN(seq_ask_with_the_interrupt_between_want_and_its_count_is_taken_once);
    TT_RUN(clock_stop_then_play_inside_one_block_stops);
    TT_RUN(clock_start_stop_then_play_inside_one_block_stays_armed);
    TT_RUN(seq_stop_then_play_with_the_interrupt_before_its_count_starts_once);
    TT_RUN(clock_swing_and_gate_follow_the_clock);
    TT_RUN(clock_rides_out_a_burst_of_late_ticks);
    TT_RUN(clock_edits_act_as_on_the_internal_clock);
    TT_RUN(clock_to_int_plays_on_and_a_first_continue_starts);
}
