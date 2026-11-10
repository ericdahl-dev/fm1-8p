/*
 * Test entry point. The test files are included directly so they share
 * one tinytest instance (a unity build keeps the runner single-header).
 */
#include <string.h>

#include "tinytest.h"
#include "analysis.h"

#include "test_engine.c"
#include "test_host.c"

int main(void)
{
    run_engine_tests();
    run_host_tests();
    return tt_report();
}
