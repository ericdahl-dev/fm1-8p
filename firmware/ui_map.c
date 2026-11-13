/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_map.h"

/* visual.md: Membrane */
const ui_theme UI_MEMBRANE = {
    .bg = 0x18E4u, .surface = 0x2145u, .surface_hi = 0x31A7u, .line = 0x3A09u, .text = 0xEF3Au,
    .dim = 0xA513u, .ink = 0x1082u, .window = 0x0861u, .led = 0xF984u,
    .section = {0xEC07u, 0xEDC7u, 0xCE15u, 0x4D9Au, 0xDB53u, 0x8E4Cu, 0x9C5Cu, 0x5657u, 0xFB4Du, 0xCE15u, 0xEF3Au},
    .ghost = 26u,                       /* 10 % */
    .chip = 1u,
    .meter_segments = 0u,
    .focus = {256u, 256u, 0u},          /* a 2 px frame */
    .hatch = 0u,
};
