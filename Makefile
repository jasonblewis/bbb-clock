clock: clock.c
	gcc -std=c17 -g -Wall -Werror -o clock clock.c -lm

# Static-analysis lint. Surfaces the whole "silent narrowing / sign change"
# class -- the bug family behind the uint16_t moving-average overflow that
# corrupted the brightness ramp -- plus GCC's path-sensitive analyzer for
# out-of-bounds / uninitialised use. Non-fatal (the -Wconversion set is noisy
# by design); run before committing any arithmetic or buffer-indexing change
# and eyeball the new warnings.
lint: clock.c
	gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion -fanalyzer \
	    -fsyntax-only clock.c -lm

# Sanitizer build for runtime verification. ASan catches out-of-bounds buffer
# writes (e.g. a bad -c channel, an over-length sensor line); UBSan catches
# integer overflow, bad shifts, and signed conversions as they happen. Exercise
# the CLI paths through it, e.g.:
#   ./clock-asan -t        # let it run a while, Ctrl-C
#   ./clock-asan -c 999    # should error cleanly, not trip ASan
clock-asan: clock.c
	gcc -std=c17 -g -Wall -fsanitize=address,undefined -o clock-asan clock.c -lm

.PHONY: lint
