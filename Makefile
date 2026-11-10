# fm1-8p desktop build. Needs GNU make (macOS ships it as `make`) and a C99
# compiler (gcc or clang). No other dependencies.
#
#   make                    build the render tool, build/fm1-8p-render
#   make test               build and run the tests
#   make check-float-free   prove the engine has no floating point or heap
#                           use, and that the check catches both
#   make check              test + check-float-free
#   make render-demo        render examples/demo.mid to build/demo.wav

CC      ?= cc
CFLAGS  ?= -O2 -g
STD     = -std=c99
WARN    = -Wall -Wextra -Werror -pedantic -Wshadow -Wconversion -Wno-sign-conversion
BUILD   = build

ENGINE_SRC = $(wildcard engine/*.c)
ENGINE_HDR = $(wildcard engine/*.h)
HOST_SRC   = host/wav.c host/smf.c host/render.c
HOST_HDR   = $(wildcard host/*.h)
CANARIES   = $(wildcard tests/canary/*.c)

ENGINE_OBJ = $(ENGINE_SRC:%.c=$(BUILD)/%.o)
HOST_OBJ   = $(HOST_SRC:%.c=$(BUILD)/%.o)
RENDER     = $(BUILD)/fm1-8p-render

.PHONY: all test check check-float-free render-demo clean

all: $(RENDER)

$(BUILD)/%.o: %.c $(ENGINE_HDR) $(HOST_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(CFLAGS) -c $< -o $@

$(RENDER): host/main.c $(ENGINE_OBJ) $(HOST_OBJ)
	$(CC) $(STD) $(WARN) $(CFLAGS) host/main.c $(ENGINE_OBJ) $(HOST_OBJ) -o $@

# Tests may use double and libm (never the engine itself).
$(BUILD)/run-tests: tests/test_main.c $(wildcard tests/*.h tests/test_*.c) $(ENGINE_OBJ) $(HOST_OBJ)
	$(CC) $(STD) -Wall -Wextra -Werror -Wno-unused-function $(CFLAGS) \
		tests/test_main.c $(ENGINE_OBJ) $(HOST_OBJ) -lm -o $@

test: $(BUILD)/run-tests
	./$(BUILD)/run-tests

# The engine must pass; each canary (a float or heap use added to the
# engine sources) must make the same check fail.
check-float-free:
	CC="$(CC)" sh tools/check-engine.sh $(BUILD)/check-engine $(ENGINE_SRC)
	@for c in $(CANARIES); do \
		if CC="$(CC)" sh tools/check-engine.sh $(BUILD)/check-canary \
			$(ENGINE_SRC) $$c >/dev/null 2>&1; then \
			echo "check-float-free: canary $$c was NOT caught" >&2; exit 1; \
		fi; \
		echo "check-float-free: canary $$c caught as expected"; \
	done

check: test check-float-free

render-demo: $(RENDER)
	./$(RENDER) examples/demo.mid $(BUILD)/demo.wav

clean:
	rm -rf $(BUILD)
