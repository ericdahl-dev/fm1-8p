# fm1-8p desktop build. Works with GNU make or BSD make on Linux and macOS.
#
#   make              build the render tool (build/fm1-8p-render)
#   make test         build and run the tests
#   make check-float-free   prove the engine contains no floating-point code
#   make render-demo  render examples/demo.mid to build/demo.wav

CC      ?= cc
WARN    = -Wall -Wextra -Werror -pedantic -Wshadow -Wconversion -Wno-sign-conversion
CFLAGS  ?= -O2 -g
STD     = -std=c99
BUILD   = build

ENGINE_SRC = engine/engine.c
HOST_SRC   = host/wav.c host/smf.c host/render.c
HOST_MAIN  = host/main.c
TEST_SRC   = tests/test_main.c

ENGINE_OBJ = $(ENGINE_SRC:%.c=$(BUILD)/%.o)
HOST_OBJ   = $(HOST_SRC:%.c=$(BUILD)/%.o)

.PHONY: all test check-float-free render-demo clean

all: $(BUILD)/fm1-8p-render

$(BUILD)/%.o: %.c engine/*.h host/*.h
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(CFLAGS) -c $< -o $@

$(BUILD)/fm1-8p-render: $(HOST_MAIN) $(ENGINE_OBJ) $(HOST_OBJ) host/*.h
	@mkdir -p $(BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) $(HOST_MAIN) $(ENGINE_OBJ) $(HOST_OBJ) -o $@

# Tests may use double and libm; -Wconversion is relaxed for them.
$(BUILD)/run-tests: $(TEST_SRC) tests/*.h tests/*.c $(ENGINE_OBJ) $(HOST_OBJ)
	@mkdir -p $(BUILD)
	$(CC) $(STD) -Wall -Wextra -Werror -Wno-unused-function $(CFLAGS) $(TEST_SRC) $(ENGINE_OBJ) $(HOST_OBJ) -lm -o $@

test: $(BUILD)/run-tests
	./$(BUILD)/run-tests

clean:
	rm -rf $(BUILD)
