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
#   make check              test + check-float-free + check-fm1-build + check-patches
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

ENGINE_SRC = $(wildcard engine/*.c)
ENGINE_HDR = $(wildcard engine/*.h)
HOST_SRC   = host/wav.c host/smf.c host/render.c host/args.c
BENCH_SRC  = host/bench.c
HOST_HDR   = $(wildcard host/*.h)
CANARIES   = $(wildcard tests/canary/*.c)
FW_SRC     = firmware/cpu_test.c firmware/play.c firmware/ui.c firmware/patches.c firmware/patch.c
FW_HDR     = firmware/cpu_test.h firmware/play.h firmware/ui.h firmware/patches.h

ENGINE_OBJ = $(ENGINE_SRC:%.c=$(BUILD)/%.o)
HOST_OBJ   = $(HOST_SRC:%.c=$(BUILD)/%.o)
BENCH_OBJ  = $(BENCH_SRC:%.c=$(BUILD)/%.o)
FW_OBJ     = $(FW_SRC:%.c=$(BUILD)/%.o)
RENDER     = $(BUILD)/fm1-8p-render
BENCH      = $(BUILD)/fm1-8p-bench

.PHONY: all bench bench-full test check check-float-free check-fm1-build check-runner check-patches render-demo clean

all: $(RENDER) $(BENCH)

$(BUILD)/%.o: %.c $(ENGINE_HDR) $(HOST_HDR) $(FW_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(CFLAGS) -c $< -o $@

$(RENDER): host/main.c $(ENGINE_OBJ) $(HOST_OBJ)
	$(CC) $(STD) $(WARN) $(CFLAGS) host/main.c $(ENGINE_OBJ) $(HOST_OBJ) -lm -o $@

$(BENCH): host/bench_main.c $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ)
	$(CC) $(STD) $(WARN) $(CFLAGS) host/bench_main.c $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ) -lm -o $@

bench: $(BENCH)
	./$(BENCH)

bench-full: $(BENCH)
	./$(BENCH) -w full

# Tests may use double and libm (never the engine itself).
$(BUILD)/run-tests: tests/test_main.c $(wildcard tests/*.h tests/test_*.c) tests/filter_oracle.c $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ)
	$(CC) $(STD) -Wall -Wextra -Werror -Wno-unused-function $(CFLAGS) \
		tests/test_main.c $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ) -lm -o $@

# Through tools/run-tests.sh: an empty binary, or one that reports no tests, fails.
test: $(BUILD)/run-tests
	sh tools/run-tests.sh ./$(BUILD)/run-tests

# The engine must pass; each canary (a float or heap use added to the
# engine sources) must make the same check fail.
check-float-free:
	CC="$(CC)" sh tools/check-engine.sh $(BUILD)/check-engine $(ENGINE_SRC)
	CC="$(CC)" sh tools/check-engine.sh $(BUILD)/check-play $(ENGINE_SRC) firmware/play.c firmware/ui.c firmware/patches.c firmware/patch.c
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

check-patches:
	python3 -m unittest discover -s tests -p 'test_*.py'

check: test check-runner check-float-free check-fm1-build check-patches

render-demo: $(RENDER)
	./$(RENDER) examples/demo.mid $(BUILD)/demo.wav

clean:
	rm -rf $(BUILD)
