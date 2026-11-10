/*
 * Test entry point. The test files are included directly so they share
 * one tinytest instance (a unity build keeps the runner single-header).
 */
#include <string.h>

#include "tinytest.h"
#include "analysis.h"

#include "test_engine.c"
#include "test_host.c"
#include "test_cpu_test.c"
#include "test_osc.c"

int main(void)
{
    run_engine_tests();
    run_host_tests();
    run_cpu_test_tests();
    run_osc_tests();
    return tt_report();
}
