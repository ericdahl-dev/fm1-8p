/*
 * Test entry point. The test files are included directly so they share
 * one tinytest instance (a unity build keeps the runner single-header).
 */
#include <string.h>

#include "tinytest.h"
#include "analysis.h"

/* The public header gives callers the engine's storage, not the chorus
 * functions: fm_chorus_* is internal (engine/chorus.h). */
#include "../engine/fm1_8p.h"
#ifdef FM_CHORUS_H
#error "fm1_8p.h must not include engine/chorus.h"
#endif

#include "test_engine.c"
#include "test_host.c"
#include "test_poly.c"
#include "test_unison.c"
#include "test_cpu_test.c"
#include "test_bench.c"
#include "test_osc.c"
#include "test_chorus.c"
#include "test_lfo.c"
#include "test_mod.c"
#include "test_filter.c"

int main(void)
{
    run_engine_tests();
    run_host_tests();
    run_poly_tests();
    run_unison_tests();
    run_cpu_test_tests();
    run_bench_tests();
    run_osc_tests();
    run_chorus_tests();
    run_lfo_tests();
    run_mod_tests();
    run_filter_tests();
    return tt_report();
}
