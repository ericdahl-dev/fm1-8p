"""The scorecard's render command (#342), apart from tools/scorecard.py so a test can check it without numpy:
TREE's build/fm1-8p-render at 48 kHz, a 3 s tail, 6 voices, and no -d. The scorecard scores PG-8X against the
engine's own level, not the FM-1 module's output gain, so a change to that gain (#320) leaves the ruler alone."""
import os

SR = 48000


# s12 (#382): ours are scored over draws with DCO-2 started these thousandths of a cycle on (fm1-8p-render
# -o): an eighth of a cycle apart, they cancel a slow beat's cross term for its first 7 harmonics
OFFSETS_MILLI = (0, 125, 250, 375, 500, 625, 750, 875)


def render_command(tree, args, mid, path, offset_milli=None):
    """The argv that renders MID to PATH with TREE's renderer and the tone's ARGS (its -s settings); with
    OFFSET_MILLI, DCO-2 started that far on (-o)."""
    o = [] if offset_milli is None else ["-o", str(offset_milli)]
    return [os.path.join(tree, "build", "fm1-8p-render"), "-r", str(SR), "-t", "3000", "-v", "6", *o, *args,
            mid, path]
