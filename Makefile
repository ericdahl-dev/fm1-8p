# fm1-8p desktop build. Needs GNU make (macOS ships it as `make`) and a C99
# compiler (gcc or clang). No other dependencies.
#
#   make                    build the render tool, build/fm1-8p-render
#   make test               build and run the tests
#   make check-float-free   prove the engine (and the synth module's play.c) has no floating point or heap
#                           use, and that the check catches both
#   make check-fm1-build    test the FM-1 build script's pin and report checks
#   make check-patches      test the JX-8P patch converter (python3) and that firmware/patches.c is
#                           what it generates from data/jx8p-factory-1-32.syx
#   make check-runner       test tools/run-tests.sh (make test fails on an empty or silent test binary)
#                           and that a build killed partway leaves no empty output make would reuse
#   make check              test + check-runner + check-float-free + check-fm1-build + check-patches
#   make gate BASE=<commit> [HEAD=<commit>]
#                           the no-sound gate (tools/gate.sh): renders every gate clip on BASE and HEAD
#                           (default: the commit checked out) and fails if any differs; prints how many
#                           clips reach each engine function (GATE_COVERAGE=0 skips that). Either may be
#                           a source directory instead of a commit.
#   make check-gate         test the gate itself (renders: minutes, so not part of make check)
#   make scorecard COMMIT=<commit> [BASE=<commit>]
#                           how close COMMIT's 32 factory tones are to PG-8X (tools/scorecard.sh: needs
#                           SCORECARD_REF, the reference renders, and a Python with numpy and scipy);
#                           appends to a JSONL history and prints the worst 5 and the tones moved
#   make check-scorecard    test the scorecard (SCORECARD_PYTHON: a Python with numpy and scipy)
#
# The FM-1 firmware itself is built by tools/fm1-build.sh (see README).
#   make render-demo        render examples/demo.mid to build/demo.wav
#   make bench              time the engine per voice count, build/fm1-8p-bench
#   make bench-full         the same on the full patch (every modulation on)

CC      ?= cc
CFLAGS  ?= -O2 -g
STD     = -std=c99
WARN    = -Wall -Wextra -Werror -pedantic -Wshadow -Wconversion -Wno-sign-conversion
BUILD   = build
# The Python tools neither read nor write __pycache__: -B writes no .pyc, and the prefix (never
# created, as nothing is written) keeps a stale .pyc beside the source from being read
# (tests/test_python_cache.sh).
PYTHON  = python3 -B -X pycache_prefix=$(CURDIR)/$(BUILD)/no-pycache

ENGINE_SRC = $(wildcard engine/*.c)
ENGINE_HDR = $(wildcard engine/*.h)
HOST_SRC   = host/wav.c host/smf.c host/render.c host/args.c
BENCH_SRC  = host/bench.c
HOST_HDR   = $(wildcard host/*.h)
CANARIES   = $(wildcard tests/canary/*.c)
FW_SRC     = firmware/cpu_test.c firmware/play.c firmware/ui.c firmware/ui_map.c firmware/ui_settings.c firmware/settings_store.c firmware/patches.c firmware/patch.c firmware/patch_store.c firmware/ui_save.c
FW_HDR     = firmware/cpu_test.h firmware/play.h firmware/ui.h firmware/patches.h firmware/patch_store.h firmware/ui_save.h

ENGINE_OBJ = $(ENGINE_SRC:%.c=$(BUILD)/%.o)
HOST_OBJ   = $(HOST_SRC:%.c=$(BUILD)/%.o)
BENCH_OBJ  = $(BENCH_SRC:%.c=$(BUILD)/%.o)
FW_OBJ     = $(FW_SRC:%.c=$(BUILD)/%.o)
RENDER     = $(BUILD)/fm1-8p-render
BENCH      = $(BUILD)/fm1-8p-bench

.PHONY: all bench bench-full test check check-float-free check-fm1-build check-runner check-patches render-demo clean \
	gate check-gate scorecard check-scorecard

all: $(RENDER) $(BENCH)

# Each compile and link writes $@.tmp, then renames it: a build killed partway (make too, as when
# the session running it dies) leaves no partial $@ that the next make would take as up to date.

$(BUILD)/%.o: %.c $(ENGINE_HDR) $(HOST_HDR) $(FW_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(CFLAGS) -c $< -o $@.tmp && mv $@.tmp $@

$(RENDER): host/main.c $(ENGINE_OBJ) $(HOST_OBJ)
	$(CC) $(STD) $(WARN) $(CFLAGS) host/main.c $(ENGINE_OBJ) $(HOST_OBJ) -lm -o $@.tmp && mv $@.tmp $@

$(BENCH): host/bench_main.c $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ)
	$(CC) $(STD) $(WARN) $(CFLAGS) host/bench_main.c $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ) -lm -o $@.tmp && mv $@.tmp $@

bench: $(BENCH)
	./$(BENCH)

bench-full: $(BENCH)
	./$(BENCH) -w full

# Tests may use double and libm (never the engine itself).
$(BUILD)/run-tests: tests/test_main.c $(wildcard tests/*.h tests/test_*.c) tests/filter_oracle.c $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ)
	$(CC) $(STD) -Wall -Wextra -Werror -Wno-unused-function $(CFLAGS) \
		tests/test_main.c $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ) -lm -o $@.tmp && mv $@.tmp $@

# Through tools/run-tests.sh: an empty binary, or one that reports no tests, fails.
test: $(BUILD)/run-tests
	sh tools/run-tests.sh ./$(BUILD)/run-tests

# The engine must pass; each canary (a float or heap use added to the
# engine sources) must make the same check fail.
check-float-free:
	CC="$(CC)" sh tools/check-engine.sh $(BUILD)/check-engine $(ENGINE_SRC)
	CC="$(CC)" sh tools/check-engine.sh $(BUILD)/check-play $(ENGINE_SRC) firmware/play.c firmware/ui.c firmware/ui_map.c firmware/ui_settings.c firmware/settings_store.c firmware/patches.c firmware/patch.c firmware/patch_store.c firmware/ui_save.c
	@for c in $(CANARIES); do \
		if CC="$(CC)" sh tools/check-engine.sh $(BUILD)/check-canary \
			$(ENGINE_SRC) $$c >/dev/null 2>&1; then \
			echo "check-float-free: canary $$c was NOT caught" >&2; exit 1; \
		fi; \
		echo "check-float-free: canary $$c caught as expected"; \
	done

check-fm1-build:
	sh tests/test_fm1_build.sh

check-runner:
	sh tests/test_run_tests.sh
	sh tests/test_build_outputs.sh

check-patches:
	PYTHON="$(PYTHON)" sh tests/test_python_cache.sh
	sh tests/test_python_without_numpy.sh
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'

check: test check-runner check-float-free check-fm1-build check-patches

gate:
	@test -n "$(BASE)" || { echo "usage: make gate BASE=<commit> [HEAD=<commit>]" >&2; exit 2; }
	GATE_COVERAGE="$(GATE_COVERAGE)" PYTHON="$(PYTHON)" sh tools/gate.sh "$(BASE)" $(if $(HEAD),"$(HEAD)")

check-gate:
	sh tests/test_gate.sh

scorecard:
	@test -n "$(COMMIT)" || { echo "usage: make scorecard COMMIT=<commit> [BASE=<commit>]" >&2; exit 2; }
	sh tools/scorecard.sh "$(COMMIT)" $(if $(BASE),"$(BASE)")

check-scorecard:
	$(if $(SCORECARD_PYTHON),$(SCORECARD_PYTHON),python3) -B -m unittest tests/test_scorecard.py

render-demo: $(RENDER)
	./$(RENDER) examples/demo.mid $(BUILD)/demo.wav

clean:
	rm -rf $(BUILD)
