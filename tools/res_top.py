"""The resonant top's tables (#168): the filter's coefficients at the top of the cutoff range, with resonance.

With resonance, from RT_C0 cents of cutoff up, engine/filter.c reads its two sections' t (tan(pi f0 / fs)), their
damping k and section 1's double zero c from these tables, bilinear between the knots, at 44.1 and 48 kHz (and
linear between the two sets for a rate between). The values are a fit, made outside the repository: PG-8X 2.0.13
(SELFNOISE 0, HPF 1, chorus off) rendered at each rate with a C3 saw at every VCF FREQ 100..127 and 22 VCF RES
settings, its response read at every harmonic (131 Hz apart) re its own open filter, and the engine's analytic
response

    H = a (1 + c s)^2 (1 + s)^2 t^4 / (s^2 + k t s + t^2)^2,   s = j tan(pi f / fs),   a = PG-8X's passband

least-squares fitted to it at the knots with a second-difference penalty (2026-11-27; the open cutoff, 14400,
stands for PG-8X's VCF FREQ 127, where its own sum of cutoff sources stops). No PG-8X render is in the
repository: the knots below are the fit's result, as the integers the engine reads.

tools/jx8p.py generates engine/res_top_tables.h from this (`make generate`); tests/test_res_top.py checks it.
"""

# Cutoff knots, cents from MIDI note 0, and resonance knots, per mille
CUTOFF = (11500, 12100, 12650, 13200, 13475, 13750, 14030, 14170, 14309, 14400)
RESONANCE = (112, 180, 290, 390, 575, 795, 860, 900, 935)

# The scales the integers are in: t x 2^12, k x 2^13, c x 2^15
T_SHIFT, K_SHIFT, C_SHIFT = 12, 13, 15

# By rate: a row per cutoff knot, a column per resonance knot
T = {
    44100: (
        (1794, 1789, 1756, 1734, 1705, 1673, 1656, 1645, 1629),
        (2563, 2558, 2539, 2516, 2475, 2420, 2396, 2379, 2355),
        (3550, 3546, 3603, 3612, 3582, 3504, 3469, 3439, 3397),
        (4771, 4782, 5040, 5194, 5316, 5350, 5348, 5326, 5281),
        (5260, 5368, 5806, 6238, 6676, 7072, 7196, 7222, 7200),
        (5295, 5579, 6147, 7087, 7913, 9063, 9654, 9988, 10274),
        (5257, 5465, 5881, 6498, 7513, 9367, 12824, 14705, 16814),
        (5433, 5640, 6064, 6658, 7502, 9075, 11696, 14732, 18238),
        (5967, 6325, 6917, 7687, 8439, 9614, 11708, 14206, 16099),
        (7007, 7656, 8670, 9838, 10959, 12173, 13714, 15325, 15960),
    ),
    48000: (
        (1632, 1628, 1597, 1577, 1553, 1523, 1509, 1500, 1485),
        (2331, 2326, 2299, 2274, 2235, 2187, 2166, 2150, 2130),
        (3243, 3235, 3254, 3243, 3198, 3122, 3089, 3063, 3027),
        (4398, 4394, 4563, 4645, 4686, 4654, 4633, 4605, 4560),
        (5009, 5032, 5390, 5659, 5889, 6014, 6037, 6019, 5970),
        (5302, 5473, 5939, 6483, 7037, 7692, 7941, 8032, 8069),
        (5281, 5517, 6061, 7030, 8068, 9667, 10869, 11514, 12151),
        (5333, 5551, 6007, 6716, 7667, 9303, 11711, 13682, 15907),
        (5647, 5948, 6436, 7019, 7837, 9305, 11974, 14900, 18554),
        (6336, 6908, 7807, 8933, 9902, 11023, 15246, 20368, 22422),
    ),
}

K = {
    44100: (
        (12614, 11710, 9831, 8125, 5853, 3482, 2694, 2098, 1376),
        (13160, 12234, 10375, 8606, 6207, 3685, 2851, 2220, 1454),
        (14150, 13198, 11394, 9535, 6921, 4110, 3174, 2468, 1612),
        (15566, 14692, 13172, 11383, 8602, 5263, 4068, 3170, 2066),
        (16146, 15493, 14375, 12938, 10351, 6655, 5160, 4031, 2617),
        (16254, 15913, 15326, 14519, 12624, 9343, 7517, 5998, 3892),
        (16372, 16088, 15903, 15616, 15129, 14717, 12587, 11117, 8467),
        (16564, 16306, 16232, 16067, 15835, 16463, 16256, 15719, 16444),
        (17044, 16896, 16878, 16711, 16250, 16649, 17035, 17132, 18885),
        (18033, 18232, 18724, 18994, 18536, 17894, 18432, 18892, 19066),
    ),
    48000: (
        (12479, 11592, 9718, 8035, 5794, 3447, 2668, 2079, 1363),
        (12948, 12030, 10163, 8418, 6066, 3602, 2788, 2170, 1422),
        (13814, 12854, 11011, 9171, 6629, 3931, 3038, 2363, 1545),
        (15055, 14131, 12467, 10618, 7878, 4754, 3672, 2858, 1863),
        (15777, 14959, 13610, 11938, 9185, 5692, 4398, 3426, 2225),
        (16120, 15587, 14632, 13387, 11046, 7383, 5749, 4504, 2915),
        (16237, 15899, 15489, 14898, 13480, 10846, 8950, 7367, 4972),
        (16360, 16058, 15809, 15429, 14715, 14039, 12422, 10730, 7899),
        (16665, 16437, 16259, 15943, 15498, 15916, 15425, 14589, 13922),
        (17280, 17349, 17643, 17767, 17222, 16829, 17708, 18942, 19190),
    ),
}

C = {
    44100: (
        (17718, 18071, 18395, 18806, 19537, 20469, 20620, 20895, 20946),
        (17168, 17570, 17675, 18057, 18874, 19992, 20121, 20320, 20395),
        (16230, 16809, 16571, 16837, 17589, 18726, 18829, 19063, 19181),
        (15116, 15869, 15577, 15899, 16772, 17807, 17708, 17865, 17880),
        (15247, 15563, 15191, 14936, 15577, 16201, 15836, 15911, 15865),
        (17502, 16850, 16312, 14442, 15208, 16213, 15416, 15077, 14408),
        (19564, 19364, 19826, 19517, 19986, 21919, 14503, 13130, 12236),
        (18891, 18721, 19083, 18926, 20089, 22938, 18370, 15002, 15431),
        (16033, 15201, 14860, 14244, 15577, 18784, 16142, 13647, 15155),
        (12169, 10750, 9501, 8439, 8302, 8690, 7623, 6663, 6746),
    ),
    48000: (
        (18345, 18634, 19041, 19409, 20093, 21114, 21193, 21321, 21585),
        (17703, 18109, 18302, 18712, 19565, 20677, 20814, 21006, 21057),
        (16664, 17214, 17121, 17464, 18320, 19535, 19675, 19913, 20041),
        (15696, 16483, 16252, 16633, 17539, 18683, 18667, 18858, 18899),
        (15233, 15975, 15458, 15470, 16232, 17188, 17063, 17250, 17317),
        (16339, 16339, 16066, 15571, 16355, 16811, 16143, 16066, 15808),
        (18743, 18391, 18011, 15974, 16242, 17027, 15183, 14732, 14015),
        (19203, 18970, 19202, 18459, 19287, 21889, 17227, 14854, 13102),
        (17713, 17059, 17210, 17339, 18822, 22553, 18075, 14968, 14395),
        (14985, 13354, 12003, 10649, 10930, 12207, 7548, 4920, 4788),
    ),
}


def emit_h():
    """engine/res_top_tables.h: the knots and the tables, int16, for engine/filter.c."""
    for tab in (T, K, C):
        for rate in (44100, 48000):
            assert len(tab[rate]) == len(CUTOFF) and all(len(r) == len(RESONANCE) for r in tab[rate])
            assert all(0 < v < 32768 for r in tab[rate] for v in r)
    out = ["/* The resonant top's tables (#168): t, k and section 1's double zero c at the top of the cutoff",
           " * range with resonance, by cutoff knot (rows) and resonance knot (columns), for 44.1 kHz ([0]) and",
           " * 48 kHz ([1]). A fit to PG-8X 2.0.13, made outside the repository (tools/res_top.py says how).",
           " * Generated by tools/jx8p.py from tools/res_top.py: do not edit, run `make generate`.",
           " *",
           " * Flash data: read only by engine/filter.c's apply(), at control rate. t is x 2^%d, k x 2^%d," % (T_SHIFT, K_SHIFT),
           " * c x 2^%d. */" % C_SHIFT,
           "#ifndef FM1_8P_RES_TOP_TABLES_H", "#define FM1_8P_RES_TOP_TABLES_H", "",
           "#define RT_NC %d  /* cutoff knots */" % len(CUTOFF), "#define RT_NR %d  /* resonance knots */" % len(RESONANCE),
           "#define RT_T_SHIFT %d" % T_SHIFT, "#define RT_K_SHIFT %d" % K_SHIFT, "#define RT_C_SHIFT %d" % C_SHIFT, "",
           "static const int16_t rt_ck[RT_NC] = { %s };" % ", ".join(map(str, CUTOFF)),
           "static const int16_t rt_rk[RT_NR] = { %s };" % ", ".join(map(str, RESONANCE))]
    for name, tab in (("rt_t", T), ("rt_k", K), ("rt_c", C)):
        out += ["", "static const int16_t %s[2][RT_NC][RT_NR] = {" % name]
        for rate in (44100, 48000):
            out.append("    {   /* %d Hz */" % rate)
            out += ["        { %s }," % ", ".join("%5d" % v for v in row) for row in tab[rate]]
            out.append("    },")
        out.append("};")
    out += ["", "#endif", ""]
    return "\n".join(out)
