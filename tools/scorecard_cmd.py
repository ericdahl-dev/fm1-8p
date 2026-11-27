"""The scorecard's render command (#342), apart from tools/scorecard.py so a test can check it without numpy:
TREE's build/fm1-8p-render at 48 kHz, a 3 s tail, 6 voices, and no -d. The scorecard scores PG-8X against the
engine's own level, not the FM-1 module's output gain, so a change to that gain (#320) leaves the ruler alone."""
import os

SR = 48000


# s12 (#382): ours are scored over draws with DCO-2 started these thousandths of a cycle on (fm1-8p-render
# -o): an eighth of a cycle apart, they cancel a slow beat's cross term for its first 7 harmonics
OFFSETS_MILLI = (0, 125, 250, 375, 500, 625, 750, 875)


PER_LEAD = 4


def draw_plan(leads):
    """Our draws for a reference whose draws start LEADS ticks late (s13, #392): [(lead, DCO-2 offset)].
    Draw j takes the reference's draw j's lead, so what runs on the clock (the chorus's LFO, the LFO) meets
    the phases PG-8X's draw met; at each, PER_LEAD DCO-2 offsets a quarter cycle apart (draw j's set
    staggered by j / (PER_LEAD K)), so the beat cancels at every lead on its own instead of pairing with the
    chorus's phase there (one offset a lead left 1.6 dB of that pairing's luck on a slow-beating chorus
    tone; four, 0.03). One draw (v6), or draws all at one lead,
    keeps s12's: OFFSETS_MILLI at that lead."""
    leads = list(leads)
    if len(set(leads)) <= 1:   # one draw, or draws all at one lead: no clock phase to meet
        return [(leads[0] if leads else 0, o) for o in OFFSETS_MILLI]
    k = len(leads)
    return [(t, round(1000 * (j / (PER_LEAD * k) + m / PER_LEAD)) % 1000)
            for j, t in enumerate(leads) for m in range(PER_LEAD)]


def render_command(tree, args, mid, path, offset_milli=None):
    """The argv that renders MID to PATH with TREE's renderer and the tone's ARGS (its -s settings); with
    OFFSET_MILLI, DCO-2 started that far on (-o)."""
    o = [] if offset_milli is None else ["-o", str(offset_milli)]
    return [os.path.join(tree, "build", "fm1-8p-render"), "-r", str(SR), "-t", "3000", "-v", "6", *o, *args,
            mid, path]
