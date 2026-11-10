/*
 * args.h - strict parsing of fm1-8p-render's numeric options.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef ARGS_H
#define ARGS_H

#include <stdint.h>

#include "render.h"

/*
 * Parses a decimal 0..4294967295 made only of digits (no sign, no
 * spaces). Returns 0 on success, -1 otherwise.
 */
int args_parse_u32(const char *s, uint32_t *out);

/*
 * Parses NAME=VALUE, where NAME is a parameter name from the engine's
 * table and VALUE a decimal int32 (optional leading '-'), and appends it
 * to o's settings. Returns 0 on success, -1 (o unchanged) otherwise.
 * The engine clamps the value to the parameter's range later.
 */
int args_parse_setting(const char *s, render_opts *o);

#endif /* ARGS_H */
