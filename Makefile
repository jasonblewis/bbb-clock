# Build identity baked into the binary (CLOCK_VERSION in clock.c; `clock -V`).
# In a git checkout: `git describe --always --dirty`. The on-device build dir
# (/root/bbb-build) is not a checkout, so scripts/deploy-clock.sh ships a
# VERSION file there instead. Override with `make VERSION=...` if needed.
VERSION ?= $(shell git describe --always --dirty 2>/dev/null || cat VERSION 2>/dev/null || echo unknown)
VERSION_FLAG = -DCLOCK_VERSION='"$(VERSION)"'

clock: clock.c
	gcc -std=c17 -g -Wall -Werror $(VERSION_FLAG) -o clock clock.c -lm

# Static-analysis lint. Surfaces the whole "silent narrowing / sign change"
# class -- the bug family behind the uint16_t moving-average overflow that
# corrupted the brightness ramp -- plus GCC's path-sensitive analyzer for
# out-of-bounds / uninitialised use. Non-fatal (the -Wconversion set is noisy
# by design); run before committing any arithmetic or buffer-indexing change
# and eyeball the new warnings.
lint: clock.c
	gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion -fanalyzer \
	    $(VERSION_FLAG) -fsyntax-only clock.c -lm

# Sanitizer build for runtime verification. ASan catches out-of-bounds buffer
# writes (e.g. a bad -c channel, an over-length sensor line); UBSan catches
# integer overflow, bad shifts, and signed conversions as they happen. Exercise
# the CLI paths through it, e.g.:
#   ./clock-asan -t        # let it run a while, Ctrl-C
#   ./clock-asan -c 999    # should error cleanly, not trip ASan
clock-asan: clock.c
	gcc -std=c17 -g -Wall -fsanitize=address,undefined $(VERSION_FLAG) -o clock-asan clock.c -lm

.PHONY: lint
