/* Pattern slots (firmware/pattern_store.c, #40): a sequencer pattern as a flash record in its own banks,
 * separate from patches (decision Q2). Uses test_patch_store.c's RAM flash. */
#include "../firmware/pattern_store.h"

static pat_lib pat_lib_a, pat_lib_b;

/* a pattern with every kind of step and every setting off its default */
static void pat_sample(seq_pattern *p)
{
    static const uint8_t chord[6] = {48, 52, 55, 59, 62, 65};
    uint32_t i;
    seq_pattern_empty(p);
    p->length = 12;
    p->tempo = 1373;
    p->swing_pct = 66;
    p->gate_pct = 35;
    for (i = 0; i < 6; i++)
        p->step[0].note[i] = chord[i];
    p->step[0].kind = SEQ_NOTES_ON;
    p->step[0].n = 6;
    p->step[0].vel = 127;
    p->step[1].kind = SEQ_TIE;
    p->step[3].kind = SEQ_NOTES_ON;
    p->step[3].n = 1;
    p->step[3].note[0] = 0;
    p->step[3].vel = 1;
    p->step[11].kind = SEQ_NOTES_ON;
    p->step[11].n = 2;
    p->step[11].note[0] = 127;
    p->step[11].note[1] = 60;
    p->step[11].vel = 64;
    p->step[0].bend = -8192;                    /* #42: bend, mod and move */
    p->step[0].mod = 127;
    p->step[0].glide = 1;
    p->step[3].bend = 4097;
    p->step[3].mod = 33;
    p->step[11].bend = 8191;
}

static int pat_same(const seq_pattern *a, const seq_pattern *b)
{
    uint32_t i, k, same = a->length == b->length && a->tempo == b->tempo && a->swing_pct == b->swing_pct &&
                          a->gate_pct == b->gate_pct;
    for (i = 0; i < SEQ_STEPS; i++) {
        same &= a->step[i].kind == b->step[i].kind && a->step[i].n == b->step[i].n &&
                a->step[i].vel == b->step[i].vel && a->step[i].bend == b->step[i].bend &&
                a->step[i].mod == b->step[i].mod && a->step[i].glide == b->step[i].glide;
        for (k = 0; k < a->step[i].n && k < SEQ_NOTES; k++)
            same &= a->step[i].note[k] == b->step[i].note[k];
    }
    return (int)same;
}

/* #40: saving then loading a pattern slot gives the identical pattern and its name, after a power cycle
 * (a fresh library reads the banks), in both banks. */
TT_TEST(pattern_saved_loads_identically)
{
    static seq_pattern in, out;
    char name[STORE_NAME_LEN + 1u];
    uint32_t s, same = 1;
    store_flash_blank();
    pat_lib_load(&pat_lib_a, &store_fake_io);
    pat_sample(&in);
    for (s = 0; s < PAT_SLOTS; s += 21)          /* 0, 21, 42, 63: both banks */
        same &= pat_slot_write(&pat_lib_a, &store_fake_io, s, &in, "BASSLINE 2") == 0;
    TT_CHECK(same);
    TT_CHECK_EQ(store_flash.len[1], -1);        /* the patches' banks untouched */
    TT_CHECK_EQ(store_flash.len[2], -1);
    pat_lib_load(&pat_lib_b, &store_fake_io);
    for (s = 0; s < PAT_SLOTS; s += 21) {
        same &= pat_slot_read(&pat_lib_b, s, &out, name) == 0 && pat_same(&in, &out) &&
                strcmp(name, "BASSLINE 2") == 0;
        same &= pat_slot_used(&pat_lib_b, s);
    }
    TT_CHECK(same);
    TT_CHECK(!pat_slot_used(&pat_lib_b, 1));
}

/* #40: an empty slot (fresh flash, or no bank) loads as the empty pattern: 16 rests, LENGTH 16, the
 * default TEMPO, SWING and GATE, and an empty name. */
TT_TEST(pattern_empty_slot_loads_the_empty_pattern)
{
    static seq_pattern out, empty;
    char name[STORE_NAME_LEN + 1u];
    uint32_t s, ok = 1;
    store_flash_blank();
    pat_lib_load(&pat_lib_a, &store_fake_io);
    seq_pattern_empty(&empty);
    TT_CHECK_EQ(empty.length, SEQ_STEPS);
    TT_CHECK_EQ(empty.tempo, SEQ_TEMPO_DEF);
    for (s = 0; s < PAT_SLOTS; s++) {
        pat_sample(&out);
        name[0] = 'X';
        ok &= pat_slot_read(&pat_lib_a, s, &out, name) == -1 && pat_same(&out, &empty) && name[0] == '\0';
    }
    TT_CHECK(ok);
}

/* A failed flash write leaves the slot as it was, in RAM too. */
TT_TEST(pattern_failed_write_leaves_the_slot)
{
    static seq_pattern in, out;
    char name[STORE_NAME_LEN + 1u];
    store_flash_blank();
    pat_lib_load(&pat_lib_a, &store_fake_io);
    pat_sample(&in);
    store_flash.fail = 1;
    TT_CHECK_EQ(pat_slot_write(&pat_lib_a, &store_fake_io, 5, &in, "X"), -1);
    TT_CHECK_EQ(pat_slot_read(&pat_lib_a, 5, &out, name), -1);
    TT_CHECK(!pat_slot_used(&pat_lib_a, 5));
}

/* A damaged record (another version, a length that disagrees, a step size too small) is an empty slot. */
TT_TEST(pattern_damaged_record_is_empty)
{
    static uint8_t rec[STORE_SLOT_BYTES];
    static seq_pattern in, out;
    char name[STORE_NAME_LEN + 1u];
    pat_sample(&in);
    (void)pat_encode(&in, "A", rec);
    TT_CHECK_EQ(pat_decode(rec, STORE_SLOT_BYTES, &out, name), 0);
    rec[1] = 2;
    TT_CHECK_EQ(pat_decode(rec, STORE_SLOT_BYTES, &out, name), -1);
    (void)pat_encode(&in, "A", rec);
    rec[0]++;
    TT_CHECK_EQ(pat_decode(rec, STORE_SLOT_BYTES, &out, name), -1);
    (void)pat_encode(&in, "A", rec);
    rec[20] = 8;                                /* steps under v1's 9 bytes */
    TT_CHECK_EQ(pat_decode(rec, STORE_SLOT_BYTES, &out, name), -1);
    (void)pat_encode(&in, "A", rec);
    TT_CHECK_EQ(pat_decode(rec, 30, &out, name), -1);   /* longer than the bytes read */
}

/* A version-1 record from before #42 (9-byte steps) loads, its steps at center, 0 and JUMP; a newer
 * record with wider steps (14 bytes: the most 16 steps can have in a slot) still loads what this firmware
 * knows of each. */
TT_TEST(pattern_older_and_newer_steps_load)
{
    static uint8_t now[STORE_SLOT_BYTES], other[STORE_SLOT_BYTES];
    static seq_pattern in, out, expect;
    char name[STORE_NAME_LEN + 1u];
    uint32_t i, k, n, size;
    pat_sample(&in);
    TT_CHECK_EQ(pat_encode(&in, "STEPS", now), 21u + 13u * SEQ_STEPS);
    for (size = 9; size <= 14u; size += 5) {
        for (i = 0; i < 21u; i++)
            other[i] = now[i];
        other[20] = (uint8_t)size;
        n = 21;
        TT_CHECK(21u + size * SEQ_STEPS <= STORE_SLOT_BYTES);   /* (before writing them) */
        for (i = 0; i < SEQ_STEPS; i++) {
            for (k = 0; k < size && k < 13u; k++)
                other[n + k] = now[21u + 13u * i + k];
            for (; k < size; k++)
                other[n + k] = 0xA5;            /* a newer firmware's fields */
            n += size;
        }
        TT_CHECK(n <= STORE_SLOT_BYTES);
        other[0] = (uint8_t)n;
        TT_CHECK_EQ(pat_decode(other, STORE_SLOT_BYTES, &out, name), 0);
        expect = in;
        if (size == 9u)
            for (i = 0; i < SEQ_STEPS; i++) {
                expect.step[i].bend = 0;
                expect.step[i].mod = 0;
                expect.step[i].glide = 0;
            }
        TT_CHECK(pat_same(&expect, &out));
        TT_CHECK(strcmp(name, "STEPS") == 0);
    }
}

/* Out-of-range values load clamped: length 1..16, the tempo, swing and gate ranges, a step's note count
 * to SEQ_NOTES, its notes and velocity to MIDI's; an unknown step kind is a rest. */
TT_TEST(pattern_out_of_range_values_load_clamped)
{
    static uint8_t rec[STORE_SLOT_BYTES];
    static seq_pattern in, out;
    char name[STORE_NAME_LEN + 1u];
    pat_sample(&in);
    (void)pat_encode(&in, "", rec);
    rec[14] = 40;                               /* length */
    rec[15] = 0xFF;                             /* tempo 65535 */
    rec[16] = 0xFF;
    rec[17] = 99;                               /* swing */
    rec[18] = 0;                                /* gate */
    rec[21] = 9;                                /* step 1's kind */
    rec[21 + 13 + 1] = 0;                       /* (step 2: a tie, no notes) */
    rec[21 + 13 * 3 + 1] = 200;                 /* step 4's note count */
    rec[21 + 13 * 3 + 2] = 0;                   /* its velocity */
    rec[21 + 13 * 3 + 3] = 200;                 /* its first note */
    rec[21 + 13 * 3 + 9] = 0xFF;                /* its bend: 32767 */
    rec[21 + 13 * 3 + 10] = 0x7F;
    rec[21 + 13 * 3 + 11] = 200;                /* its mod */
    TT_CHECK_EQ(pat_decode(rec, STORE_SLOT_BYTES, &out, name), 0);
    TT_CHECK_EQ(out.length, SEQ_STEPS);
    TT_CHECK_EQ(out.tempo, SEQ_TEMPO_MAX);
    TT_CHECK_EQ(out.swing_pct, 75u);
    TT_CHECK_EQ(out.gate_pct, SEQ_GATE_MIN);
    TT_CHECK_EQ(out.step[0].kind, SEQ_REST);
    TT_CHECK_EQ(out.step[3].n, SEQ_NOTES);
    TT_CHECK_EQ(out.step[3].vel, 1u);
    TT_CHECK_EQ(out.step[3].note[0], 127u);
    TT_CHECK_EQ(out.step[3].bend, 8191);
    TT_CHECK_EQ(out.step[3].mod, 127u);
}

static void run_pattern_store_tests(void)
{
    TT_RUN(pattern_saved_loads_identically);
    TT_RUN(pattern_empty_slot_loads_the_empty_pattern);
    TT_RUN(pattern_failed_write_leaves_the_slot);
    TT_RUN(pattern_damaged_record_is_empty);
    TT_RUN(pattern_older_and_newer_steps_load);
    TT_RUN(pattern_out_of_range_values_load_clamped);
}
