/*
 * tinytest.h - a minimal single-header test runner for fm1-8p.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * No dependencies beyond the C standard library. Usage:
 *
 *   TT_TEST(name) { TT_CHECK(1 + 1 == 2); }
 *   int main(void) { TT_RUN(name); return tt_report(); }
 *
 * A failed check prints file, line and expression, marks the current test
 * as failed and returns from it.
 */
#ifndef TINYTEST_H
#define TINYTEST_H

#include <stdio.h>

static int tt_tests_run;
static int tt_tests_failed;
static int tt_current_failed;

#define TT_TEST(name) static void name(void)

#define TT_FAIL_(fmt, ...)                                                   \
    do {                                                                     \
        printf("  FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__);  \
        tt_current_failed = 1;                                               \
        return;                                                              \
    } while (0)

#define TT_CHECK(cond)                                                       \
    do {                                                                     \
        if (!(cond)) TT_FAIL_("%s", #cond);                                  \
    } while (0)

/* Integer equality; values are printed as long long. */
#define TT_CHECK_EQ(a, b)                                                    \
    do {                                                                     \
        long long tt_a_ = (long long)(a), tt_b_ = (long long)(b);            \
        if (tt_a_ != tt_b_)                                                  \
            TT_FAIL_("%s == %s (%lld != %lld)", #a, #b, tt_a_, tt_b_);       \
    } while (0)

/* |a - b| <= tol, compared as double (test code only, never the engine). */
#define TT_CHECK_NEAR(a, b, tol)                                             \
    do {                                                                     \
        double tt_a_ = (double)(a), tt_b_ = (double)(b);                     \
        double tt_d_ = tt_a_ - tt_b_;                                        \
        if (tt_d_ < 0) tt_d_ = -tt_d_;                                       \
        if (tt_d_ > (double)(tol))                                           \
            TT_FAIL_("%s ~= %s (%g vs %g, tol %g)", #a, #b, tt_a_, tt_b_,    \
                     (double)(tol));                                         \
    } while (0)

#define TT_RUN(name)                                                         \
    do {                                                                     \
        tt_current_failed = 0;                                               \
        name();                                                              \
        tt_tests_run++;                                                      \
        if (tt_current_failed) tt_tests_failed++;                            \
        printf("%s %s\n", tt_current_failed ? "not ok" : "ok    ", #name);   \
        fflush(stdout); /* a later abort must not lose it (piped stdout) */  \
    } while (0)

static int tt_report(void)
{
    printf("\n%d tests, %d failed\n", tt_tests_run, tt_tests_failed);
    return tt_tests_failed ? 1 : 0;
}

#endif /* TINYTEST_H */
