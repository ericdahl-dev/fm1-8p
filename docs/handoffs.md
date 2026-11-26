# Handoffs between the main loop and the audio interrupt

fm1-8p runs in two contexts that share `play` (firmware/play.h) and `seq` (firmware/seq.h):

- **The main loop:** the UI, MIDI in, the module's tick, the clock it follows (`seq_clock_in`,
  `clock_tick`) and the clock it sends (`seq_clock_out_take`).
- **The audio interrupt:** `render()` in firmware/fm1_8p/fm1_8p.c, which calls `seq_block_time`
  and then `play_render`, and everything they call (`apply_params`, `seq_render`, `start_step`,
  `out_put`).

The interrupt can run between any two of the main loop's stores; the main loop never runs inside
the interrupt. Every field both touch is one **handoff**, and each handoff is one of four kinds
below. Each field's comment in play.h and seq.h names its kind ("kind B, main writes"). A new
shared field picks a kind, follows its rule and gets a test (#349).

`BARRIER()` (play.c, seq.c) is a compiler barrier: the FM-1's one core runs both contexts, so only
the compiler can reorder. `HANDOFF_GAP(where, n)` (firmware/handoff_test.h) marks a point between
two of a handoff's stores. In host builds (`-DFM1_HANDOFF_TEST`) a test can run the interrupt
there; in the device build it is nothing.

## The four kinds

### A. Queue: one producer, one consumer

**Rule.** The producer writes the entry, then a barrier, then its index. The consumer reads the
index, a barrier, the entries, a barrier, then writes its own index. Each index has one writer. A
full queue drops the entry and counts it in a counter only the producer writes (kind B).

### B. Ask counter

**Rule.** The writer writes any payload, then a barrier, then `+1`. The reader keeps its own
`_seen` and acts when the two differ, once; the latest ask wins. The payload must not change
between the `+1` and the reader's act, or the reader acts on the new payload, not the asked one.

### C. Gated publish: close, write, open

**Rule.** The writer closes the record (a flag, or an odd generation), barrier, writes the whole
record, barrier, then opens it. The reader takes it only while it is open, and only once per
generation. A seqlock is the same with the reader retrying: read the generation, the record, the
generation again; an odd or changed generation reads again.

### D. Single word

**Rule.** One aligned word, one writer, read any time. The reader may see the old value or the
new, never a mix, and must be fine with either.

## Every shared field

Writes in `play_init` and `seq_init` come before the interrupt runs and are left out.

### play

| Field | Kind | Writer | Reader | Notes |
|---|---|---|---|---|
| `queue[]`, `head` | A (entries, producer index) | main: `put` | interrupt: `play_render` | |
| `tail` | A (consumer index) | interrupt: `play_render` | main: `put`, `play_silent` | |
| `lost` | B (count only) | main: `put`, on a full queue | interrupt, against `lost_seen` | |
| `target[]` | payload of `asked` (B) and of `loading` (C) | main: `set_target`, `load_stored` | interrupt: `apply_params` | |
| `asked[]` | **two writers** | main sets bits (`set_target`, `load_stored`); the interrupt clears them (`apply_params`) | interrupt | See "Exceptions". |
| `loading` | C (the gate) | main: `play_load_begin`, `play_load_end`, `play_load_prog` | interrupt | |
| `loads` | C (the generation) | main: `play_load_end` | interrupt, against `loads_seen` | |
| `sounding` | D | interrupt: `play_render` | main: `play_silent` | |

### seq

| Field | Kind | Writer | Reader | Notes |
|---|---|---|---|---|
| `edit`, `edit_base` | C (payload) | main, between `edit_begin` and `edit_end` (and `take_in`, `load_now`) | interrupt: `start_step` | |
| `editing` | C (the gate) | main: `edit_begin`, `edit_end`, `take_in` | interrupt | |
| `edits` | C (the generation) | main: `edit_end` | interrupt, against `edits_seen` | A takeover drops an edit of the old pattern (`edits_seen = edits`). |
| `ask_play`, `ask_stop` | B | main: `seq_play`, `seq_stop`, `clock_tick` (a followed start) | interrupt: `seq_render` | Payload: `ask_last` and `want`, and at a followed start `ext`, `ext_t0`, `ext_cont`, `ext_inc`. |
| `ask_last` | payload of `ask_play`/`ask_stop` (B) | main: first in each ask, before `want`, `ext*` and the count | interrupt, at an ask | Which ask came last: STOP, or PLAY with the count it publishes; an armed `seq_play` writes none (#373, #377). |
| `want` | payload of `ask_play`/`ask_stop` (B) | main | interrupt, at an ask | Also written with no ask; see "Exceptions". |
| `transpose` | D | main: `seq_set_transpose` | interrupt: `start_step` | |
| `next`, `ask_kind` | C (payload) | main, while `ask` is odd | interrupt: `start_step` | |
| `ask` | C (gate and generation in one: odd while written) | main: `ask_close`, `ask_open` | interrupt: `start_step` | Gaps "ask" 0-3. |
| `took` | D (the interrupt's acknowledgment) | interrupt: `start_step` | main, while `ask` is closed | |
| `took_pattern` | B, interrupt to main | interrupt: `start_step` | main: `take_in`, against `took_seen` | Payload: `next`, which the main loop can't change until it closes `ask`. |
| `ext` | B payload at a start; D otherwise | main: `seq_play`, `seq_set_clock_source`, `clock_tick` | interrupt, every block | Two kinds on one field. |
| `ext_inc` | B payload at a start; D on every tick after | main: `set_rate` (from `clock_tick`) | interrupt, every block | Two kinds on one field. |
| `ext_t0`, `ext_cont` | B payload | main: `clock_tick` | interrupt, at the ask | |
| `ext_hold` | D | main | interrupt | |
| `blk_seq`, `blk_t`, `blk_u`, `blk_inc`, `blk_fs` | C, a seqlock, the interrupt writing | interrupt: `seq_block_time` | main: `block_read` | |
| `playing`, `step_now`, `steps` | D | interrupt: `seq_render`, `start_step` | main (the UI polls `steps` to redraw) | |
| `out_ev[]`, `out_w` | A (entries, producer index), interrupt to main | interrupt: `out_put` | main: `seq_clock_out_take` | |
| `out_r` | A (consumer index) | main: `seq_clock_out_take` | interrupt: `out_put`, its full check | |
| `out_ticks` | D | interrupt: `out_adv`, `seq_render` | main: `out_pass`, `seq_clock_out_take` | Read before `out_w`, to order the two clock-out queues. |
| `out_want`, `out_block` | D | main: `seq_set_clock_out`; `seq_clock_out_stop`, `seq_play` | interrupt, at a start | |
| `out_on` | **two writers** | main: `seq_stop`, `seq_clock_out_stop`, `clock_tick`; interrupt: `seq_render` | both | See "Exceptions". |

`pattern_now` (firmware/pattern_now.h) has no handoff of its own: it is main loop only, and
reaches the interrupt through `seq`.

Fields only one context touches are not handoffs. In `play`, the interrupt alone has `engine`,
`buf`, `lost_seen`, `loads_seen`, `live_mod` and `seq_ev[]`; everything else is the main loop's.
In `seq`, the interrupt alone has the transport (`run`, `run_transpose`, `pos`, `acc`, `gated`,
`sounding[]`, `n_sounding`), the glide (`cur_*`, `from_*`, `to_*`, `glide_*`), `upos`, `fs`,
`out_acc` and the `_seen` counters; the main loop alone has `stored`, `key_transpose`, `took_seen`,
`adopted`, every `clk_*` field and the send queue (`out_sent`, `out_m[]`, `out_mw`, `out_mr`).

## Exceptions

These don't fit one kind. Each is named here and in its field's comment.

- **`asked[]`, two writers.** The main loop sets a bit with `|=`; the interrupt clears the word
  it applied. An interrupt inside the `|=` (gap "asked" 1) has its clear undone, so the next block
  sets that param again, to the same value. A same-value set changes nothing: every param, on all
  32 factory patches, at three moments of a held chord, renders the same, sample for sample
  (`play_param_set_again_by_the_asks_race_changes_nothing`). That test found #350 (a same-value
  SYNC, RING or X-MOD dropped a sync restart's correction), fixed in #353. So `asked[]` keeps its
  two writers.
- **`want`, written outside its asks.** An armed `seq_play` (the clock in charge) sets `want = 1`
  with no ask; `seq_set_clock_source` clears it with no ask. The interrupt reads `want` when it
  takes an ask, not as it was when the ask was made: a `seq_stop` and then an armed `seq_play`
  inside one block had the stop ask taken as a start (#349), and so did a play asked, then STOP, then
  an armed PLAY (#373). So which ask came last is a payload of its own, `ask_last`, written first in
  each ask; the armed `seq_play` writes none. A play's `ask_last` carries the count it will publish
  (`SEQ_LAST_PLAY_AT`, its low 7 bits: still one byte), so the interrupt starts only on a play whose
  count it already sees, with `want` still set (a clock source change clears it with no ask). A last
  ask not yet counted is a stop to that block, whatever earlier counts are new; the next block takes
  it (#377). `clock_tick` writes it before `ext_cont`, `ext_t0`, `ext_inc` and `ext`, so an ask pending
  at a followed start (an own restart, or an earlier followed start) isn't taken with this start's
  payload
  (`clock_stop_then_play_inside_one_block_stops`, `clock_start_stop_then_play_inside_one_block_stays_armed`,
  `seq_stop_then_play_with_the_interrupt_before_its_count_starts_once`,
  `seq_a_last_ask_ahead_of_its_count_starts_once`).
- **`out_on`, two writers, gated by `out_block`.** The interrupt sets it at a start and clears it
  at a stop; the main loop clears it at `seq_stop` (a followed run), `seq_clock_out_stop` and
  `clock_tick` (a followed start ends its own run first), and sets it at a followed start. The rule:
  `out_block` is the gate. `seq_clock_out_stop` (let go) sets it first, and while it is set the
  interrupt neither writes `out_on` nor sends a start or stop; `seq_play` clears it. So at let go
  `out_on` and its stop are the main loop's: it reads `out_on`, clears it and sends the one stop
  (`release` drains once, and nothing drains after), wherever an interrupt taking a pending stop or
  restart lands (gaps "out_stop" 0 and 1). Two earlier tries on this branch each failed one order
  (a stop left to an interrupt that never ran; an interrupt between `out_block` and the read), which
  is why its test runs every order.
  `clock_tick`'s followed start, which ends the own run's clock out first, had the same read and
  clear with `out_block` clear: a pending stop or restart taken between the two sent a second stop
  (#364). It now holds the gate for that read and clear and puts `out_block` back as it was before it
  latches G14, so a let-go stays let go (gap "clock_start" 0,
  `clock_start_with_an_ask_pending_sends_one_stop`). `out_on` set with nothing playing means the main
  loop has already sent this start and its tick 0 (a followed start's latch, then the MIDI tick passed
  on). If the interrupt then takes it on its own clock (the source changed to INT first, see `ext`
  below), it sends neither again and counts its next tick a unit on; and a stop taken there closes it
  with FC. Before, that order sent FA FA and a second tick 0, and a stop sent no FC (#380,
  `clock_source_to_int_with_a_start_pending_sends_one_start`,
  `clock_source_to_int_then_stop_with_a_start_pending_closes_it`).
- **`ext`, `ext_inc`: two kinds on one field.** Each is the payload of a followed start's ask
  (kind B), and afterwards a single word the interrupt reads every block (kind D): `ext_inc` moves
  with every clock tick. Both have one writer, the main loop. `seq_set_clock_source` (to INT, or AUTO
  with the clock gone) clears `ext` with no ask of its own, so an ask already made under `ext` 1 is
  taken on the own-clock path; the start branch allows for it (`out_on` above, #380).
- **The clock start's gaps.** `clock_tick`'s start of a followed run is pinned by gaps
  "clock_start" 1-4 (`clock_out_a_stop_pending_when_a_clock_starts`). These were `seq_test_hook`'s
  `TEST_POINT`s, which stayed in the device build; `HANDOFF_GAP` is now the one mechanism.

## Tests

Each kind's tests run the interrupt (a block) at a gap between the main loop's stores, through
`HANDOFF_GAP`, and check what the interrupt and the next blocks do. Each one fails on the bug it
is there for, red first or by a mutant.

| Kind | Handoff | Gap | Test |
|---|---|---|---|
| A | `play` events | "put" 1, between the entry and `head` | `play_event_put_with_the_interrupt_inside_plays_once_next_block` (the head written first: the note never sounds) |
| A | `play` events, full queue | none | `play_queue_overflow_*` |
| B | `asked[]` | "asked" 1, inside the `\|=` | `play_param_set_again_by_the_asks_race_changes_nothing` (found #350): every param on the patches with a mode of their own at the attack; with `TT_LONG=1` (CI) every patch at three moments |
| B | `ask_play`, `ask_stop` and their payload `ask_last` and `want` | "play" 1 and "stop" 0, between the payload and the count | `seq_ask_with_the_interrupt_between_want_and_its_count_is_taken_once` (`seq_play` counting before `want`: no step plays; `seq_stop` the same way is covered by the stop rule in `want`'s exception above); `seq_stop_then_play_with_the_interrupt_before_its_count_starts_once` (a stop's block seeing a play's `ask_last` early: one start, not two) |
| B | `ask_stop`, `want`, `ask_last` | none: two or three main-loop calls before one block | `clock_stop_then_play_inside_one_block_stops`, `clock_start_stop_then_play_inside_one_block_stays_armed` (#373) |
| B | `ask_last` ahead of its count | "play" 1 inside PLAY, STOP, PLAY; "clock_start" 2 with a restart pending | `seq_a_last_ask_ahead_of_its_count_starts_once` (#377; main started that block in both) |
| B | `ask_stop` and the pattern ask | "stop" 1 | `pattern_a_takeover_inside_a_stop_loads_the_queued_one` |
| C | patch load (`loading`, `loads`) | a block between `play_load_begin` and `play_load_end` | `play_load_patch_is_one_step_for_the_audio_interrupt` |
| C | step edit (`editing`, `edits`) | "edit" 1 | `seq_takeover_inside_an_edit_plays_the_new_pattern`, `pattern_a_takeover_inside_a_step_edit_drops_the_edit` |
| C | pattern ask (`ask`, `took`) | "ask" 0-3, "sync" 1 | `pattern_a_pass_end_in_any_gap_of_the_handoff_takes_nothing` and the other `pattern_*` gap tests |
| C | seqlock (`blk_seq`) | "blk" 1, inside `block_read` | `clock_read_of_the_block_time_inside_a_block_stays_on_the_grid` (no retry: 0.147 ms off the grid against 0.011) |
| B and D | followed start (`ext*`, `ask_play`, `out_on`) | "clock_start" 0-4 | `clock_out_a_stop_pending_when_a_clock_starts`; at 0 (#364), `clock_start_with_an_ask_pending_sends_one_stop`: a stop or a restart pending, FA FC FA (main: FA FC FC FA, FA FC FA FC) |
| two writers | `out_on` at let go | "out_stop" 0 and 1, and blocks before, between and after release's calls | `clock_out_let_go_in_every_order_ends_stopped`: 48 orders (no ask, STOP, restart, PLAY from stopped; six places for the block; own clock and followed), each must alternate starts and stops and end stopped. It catches all three wrong versions: the stop left to the interrupt (0 stops), no `out_block` check (2 stops at gap 1), the stop alone gated (0 stops at gap 0). Also `clock_out_stop_inside_a_pending_ask_sends_one_stop`, `clock_out_ends_with_a_stop_when_let_go_with_an_ask_pending` |

Kind D needs no gap: one store, read whole either side of it.
