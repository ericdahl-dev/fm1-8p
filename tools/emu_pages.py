#!/usr/bin/env python3
"""#47's per-page redraw cost: one fm1-emu run of the debug build visits every screen, and draw.c logs
each screen's worst draw() call when the screen changes ("fm1-8p PAGES", FM1_DEBUG builds only).

usage: emu_pages.py keys PART     part PART's run length in ms, then its key script (fm1-live's
                                  FM1_BENCH_KEYS); parts 0..6 (PARTS)
       emu_pages.py report LOG...   a Markdown table of the worst draw call per screen, judged against
                                  visual.md's 5 ms budget (exit 1: a screen over it, or no lines)

The tour: every edit page with each knob turned once; PLAY with a knob turned; GLO through its three
pages; SAVE with a letter changed, then HOME; the number jump's screen (SEL held)."""
import re
import sys

PAGES = ["1A", "1B", "2A", "2B", "3A", "4A", "4B", "5A", "5B", "6A", "6B", "7A", "8A", "8B",
         "9A", "9B", "0A", "0B"]
STATES = ["PLAY", "EDIT", "SAVE", "GLO"]
TICKS_PER_MS = 24000                    # the FM-1's tick: 24 MHz
BUDGET_MS = 5                           # visual.md: a draw() call takes at most 5 ms (Eric)
DWELL = 600                             # ms on each screen
PARTS = 7


def keys(part):
    """Part `part`'s key script and its length in ms. The host's log keeps only its last few dozen
    lines, so the tour is cut in parts: 0..5 are three edit pages each, 6 is PLAY, GLO, SAVE and the
    jump's screen."""
    ev, t = [], 1000

    def tap(button):
        nonlocal t
        ev.extend(["%d:%d" % (t, button), "%d:%d^" % (t + 60, button)])
        t += 200

    def turn(enc, clicks=1):
        nonlocal t
        ev.append("%d:e%d:%d" % (t, enc, clicks))
        t += 100

    if part < PARTS - 1:                # three edit pages, from page 3 x part (SELECT walks from 5A)
        tap(6)                          # EDIT: 5A
        first = (3 * part - PAGES.index("5A")) % len(PAGES)
        if first:
            turn(0, first)
        for page in range(3):
            for k in range(4):
                turn(2 + k)
            t += DWELL
            turn(0)
    else:
        turn(2)                         # PLAY: KNOB1 (the readout)
        t += DWELL
        tap(7)                          # GLO
        for page in range(3):
            for k in range(4):
                turn(2 + k)
            t += DWELL
            turn(0)
        tap(7)                          # back
        tap(9)                          # SAVE
        turn(2)
        t += DWELL
        tap(8)                          # HOME: canceled
        t += DWELL
        ev.append("%d:3" % t)           # SEL held: the jump's screen after 400 ms
        t += 1200
        ev.append("%d:3^" % t)
        t += DWELL
    tap(6)                              # one more screen change, so the last screen's worst is logged
    t += DWELL
    return ",".join(ev), t + 500


def screen_name(key):
    jump, state, page = key >> 16 & 1, key >> 8 & 0xFF, key & 0xFF
    if jump:
        return "JUMP"
    name = STATES[state] if state < len(STATES) else "state %d" % state
    return "%s %s" % (name, PAGES[page]) if name == "EDIT" and page < len(PAGES) else name


def report(texts):
    # (log_u32 writes hex)
    worst = {}
    for text in texts:
        keys_ = [int(x, 16) for x in re.findall(r"fm1-8p PAGES screen ([0-9a-fA-F]+)", text)]
        ticks = [int(x, 16) for x in re.findall(r"fm1-8p PAGES worst draw call, ticks ([0-9a-fA-F]+)", text)]
        for k, w in zip(keys_, ticks):
            name = screen_name(k)
            worst[name] = max(worst.get(name, 0), w)
    if not worst:
        print("**No per-screen lines (\"fm1-8p PAGES\"): run the debug build (FM1_DEBUG=1).**")
        return 1
    over = [n for n, w in worst.items() if w > BUDGET_MS * TICKS_PER_MS]
    print("| Screen | Worst draw() call | Within %d ms |" % BUDGET_MS)
    print("| --- | --- | --- |")
    for name, w in sorted(worst.items(), key=lambda kv: -kv[1]):
        print("| %s | %.2f ms (%d ticks) | %s |" % (name, w / TICKS_PER_MS, w,
                                                  "**no**" if name in over else "yes"))
    if over:
        print("\n**Over the %d ms budget: %s.**" % (BUDGET_MS, ", ".join(sorted(over))))
        return 1
    return 0


def main(argv):
    if len(argv) == 3 and argv[1] == "keys":
        script, ms = keys(int(argv[2]))
        print(ms)
        print(script)
        return 0
    if len(argv) >= 3 and argv[1] == "report":
        texts = []
        for path in argv[2:]:
            with open(path) as f:
                texts.append(f.read())
        return report(texts)
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
