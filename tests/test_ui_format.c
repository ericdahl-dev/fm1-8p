/*
 * The screen's value text, pinned (#196): every param's display, at every value, matches the
 * committed golden (tests/ui_format_golden.h). `make ui-golden` writes the golden anew and prints
 * which readable rows moved; a change to it is a change to what the screen shows, to be reviewed.
 */
#include "ui_format_walk.h"
#include "ui_format_golden.h"

/* Every param is in the golden, once, in id order, by its own name: a new param fails here until the
 * golden is made again (make ui-golden). */
TT_TEST(ui_format_golden_covers_every_param)
{
    uint32_t i;
    TT_CHECK_EQ(UFW_GOLDEN_N, (uint32_t)FM_PARAM_COUNT);
    for (i = 0; i < UFW_GOLDEN_N && i < FM_PARAM_COUNT; i++) {
        const fm_param_info *in = fm_param_info_get((uint16_t)i);
        TT_CHECK_EQ(UFW_GOLDEN[i].id, i);
        TT_CHECK(in != NULL && strcmp(UFW_GOLDEN[i].name, in->name) == 0);
    }
}

/* Every value of every param reads as it did: one hash over its whole walk (min - 2 .. max + 2) */
TT_TEST(ui_format_every_value_reads_as_the_golden)
{
    uint32_t i, moved = 0;
    for (i = 0; i < UFW_GOLDEN_N; i++) {        /* (every param that moved is named, not only the first) */
        uint32_t points, h = ufw_hash(UFW_GOLDEN[i].id, &points);
        if (h != UFW_GOLDEN[i].hash || points != UFW_GOLDEN[i].points) {
            printf("    %s: its walk differs from the golden (make ui-golden shows how)\n", UFW_GOLDEN[i].name);
            moved++;
        }
    }
    TT_CHECK_EQ(moved, 0);
}

/* The golden's readable rows are the screen's text at those values now */
TT_TEST(ui_format_samples_read_as_the_golden)
{
    uint32_t i, k, moved = 0;
    for (i = 0; i < UFW_GOLDEN_N; i++) {
        const fm_param_info *in = fm_param_info_get(UFW_GOLDEN[i].id);
        TT_CHECK(in != NULL);
        for (k = 0; in && k < UFW_SAMPLES; k++) {
            char t[UFW_TEXT];
            int32_t v = ufw_sample_value(in, k);
            ufw_text(UFW_GOLDEN[i].id, v, t);
            if (strcmp(t, UFW_GOLDEN[i].sample[k]) != 0) {
                printf("    %s at %d: \"%s\", the golden says \"%s\"\n", in->name, (int)v, t,
                       UFW_GOLDEN[i].sample[k]);
                moved++;
            }
        }
    }
    TT_CHECK_EQ(moved, 0);
}

static void run_ui_format_tests(void)
{
    TT_RUN(ui_format_golden_covers_every_param);
    TT_RUN(ui_format_every_value_reads_as_the_golden);
    TT_RUN(ui_format_samples_read_as_the_golden);
}
