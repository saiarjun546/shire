# CampusCompute - Makefile
#
# Builds with Clang by default and strict C11:  make CC=gcc  also works.
# This Makefile is self-contained: it needs only a C compiler, make and
# glibc - NOT Buildroot - so CampusCompute builds on any normal Linux.
#
#   make            build the shell (shire) and the Person 2 driver (p2test)
#   make test       build and run every automated test
#   make sanitize   run the unit tests under AddressSanitizer + UBSan
#   make clean      remove all build output

CC      = clang
# _POSIX_C_SOURCE exposes clock_gettime/nanosleep/strsignal/sigaction under
# strict -std=c11 (plain ISO C hides them).
CFLAGS  = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -g -Iinclude
LDLIBS  = -pthread -lrt -lm

BUILD   = build

# Person 2 objects (logger, IPC, Banker, worker, limits)
P2_SRCS = src/logger.c src/ipc.c src/banker.c src/worker.c src/limits_demo.c
P2_OBJS = $(P2_SRCS:src/%.c=$(BUILD)/%.o)

# Person 1 objects (job management, schedulers, parser, shell)
P1_SRCS = src/jobs.c src/scheduler.c src/parser.c src/builtins.c src/shell.c
P1_OBJS = $(P1_SRCS:src/%.c=$(BUILD)/%.o)

HEADERS = $(wildcard include/*.h)

.PHONY: all test test-p1 test-p2 test-shell sanitize clean check-ipc

all: shire p2test

# ---- programs ----
shire: $(BUILD)/main.o $(P1_OBJS) $(P2_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

p2test: $(BUILD)/p2test.o $(P2_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# unit tests need only the pure-logic modules (no shell, no Person 2 code)
UNIT_OBJS = $(BUILD)/jobs.o $(BUILD)/scheduler.o $(BUILD)/parser.o

p1test: $(BUILD)/p1test.o $(UNIT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# ---- objects ----
$(BUILD)/p2test.o: tests/p2test.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/p1test.o: tests/p1test.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

# ---- tests ----
test: test-p1 test-p2 test-shell
	@echo "All test suites passed."

test-p1: p1test
	./p1test

test-p2: p2test
	sh tests/run_p2_tests.sh

test-shell: shire
	sh tests/run_shell_tests.sh

# Unit tests + shell tests under AddressSanitizer / UBSan / LeakSanitizer.
# Built in a separate directory so normal objects are not polluted.
SAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize:
	$(MAKE) --no-print-directory BUILD=build-san CFLAGS="$(CFLAGS) $(SAN_FLAGS)" \
	    LDLIBS="$(LDLIBS) $(SAN_FLAGS)" clean-san p1test
	./p1test
	$(MAKE) --no-print-directory BUILD=build-san CFLAGS="$(CFLAGS) $(SAN_FLAGS)" \
	    LDLIBS="$(LDLIBS) $(SAN_FLAGS)" shire
	SHIRE=./shire sh tests/run_shell_tests.sh
	@rm -rf build-san p1test shire

clean-san:
	rm -rf build-san p1test shire

check-ipc: p2test
	./p2test check

clean:
	rm -rf $(BUILD) build-san shire p1test p2test campuscompute.log
