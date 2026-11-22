/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Test-only interleaving for the main-loop-to-interrupt handoffs (#324). HANDOFF_GAP(where, n) marks a
 * point between two of the main loop's stores where the interrupt may run on the device. Host builds
 * (FM1_HANDOFF_TEST, the Makefile's) call handoff_gap there when a test sets it, so a test can run the
 * interrupt at each gap of a handoff in turn; on the device it is nothing. `where` names the handoff,
 * n the gap (1 the first). */
#ifndef FM1_HANDOFF_TEST_H
#define FM1_HANDOFF_TEST_H

#include <stdint.h>

#ifdef FM1_HANDOFF_TEST
extern void (*handoff_gap)(const char *where, uint32_t n);
#define HANDOFF_GAP(where, n) do { if (handoff_gap) handoff_gap((where), (n)); } while (0)
#else
#define HANDOFF_GAP(where, n) ((void)0)
#endif

#endif
