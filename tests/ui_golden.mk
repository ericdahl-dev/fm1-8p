# The display walk's program (tests/ui_golden_main.c): included by the Makefile, and passed with -f to
# an older tree's own Makefile by tools/ui_golden_diff.sh, so it builds against that tree's firmware.
$(BUILD)/ui-golden: tests/ui_golden_main.c tests/ui_format_walk.h $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ)
	$(CC) $(STD) -Wall -Wextra -Werror $(CFLAGS) \
		tests/ui_golden_main.c $(ENGINE_OBJ) $(HOST_OBJ) $(BENCH_OBJ) $(FW_OBJ) -lm -o $@.tmp && mv $@.tmp $@
