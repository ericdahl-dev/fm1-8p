/*
 * Placement: where the render path's hottest code runs on the FM-1 (#304).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * PROTOTYPE. With FM_RAM_TEXT (the FM-1 modules' cflags), FM_HOT puts a
 * function in the module's .ram_text (fm1-dock copies it to RAM on load),
 * away from the flash cache. A call from flash code to an FM_HOT function is
 * beyond the direct call's range, so it goes through FM_FAR. Elsewhere (the
 * host, the tests) both are nothing.
 */
#ifndef FM1_8P_PLACEMENT_H
#define FM1_8P_PLACEMENT_H

#ifdef FM_RAM_TEXT
#define FM_HOT __attribute__((section(".ram_text")))
static inline void *fm_far_(void *p) { void *volatile q = p; return q; }
#define FM_FAR(fn) ((__typeof__(&fn))fm_far_((void *)&fn))
/* a small helper both sides call: inlined into each caller, so it needs no call */
#define FM_BOTH inline __attribute__((always_inline))
#else
#define FM_HOT
#define FM_BOTH inline
#define FM_FAR(fn) (&fn)
#endif

#endif
