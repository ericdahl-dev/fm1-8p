"""The scorecard's render command (#342), apart from tools/scorecard.py so a test can check it without numpy:
TREE's build/fm1-8p-render at 48 kHz, a 3 s tail, 6 voices, and no -d. The scorecard scores PG-8X against the
engine's own level, not the FM-1 module's output gain, so a change to that gain (#320) leaves the ruler alone."""
import os

SR = 48000


def render_command(tree, args, mid, path):
    """The argv that renders MID to PATH with TREE's renderer and the tone's ARGS (its -s settings)."""
    return [os.path.join(tree, "build", "fm1-8p-render"), "-r", str(SR), "-t", "3000", "-v", "6", *args, mid, path]
