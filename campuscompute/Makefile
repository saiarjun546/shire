# CampusCompute - Makefile
#
# Person 2 objects are listed in P2_OBJS so Person 1 can link them into the
# main shell: add their own objects to P1_OBJS and a main target.

CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDLIBS  = -pthread -lrt

BUILD   = build
P2_SRCS = src/logger.c src/ipc.c src/banker.c src/worker.c src/limits_demo.c
P2_OBJS = $(P2_SRCS:src/%.c=$(BUILD)/%.o)

# Person 1 adds e.g.: src/jobs.c src/scheduler.c src/main.c
P1_SRCS =
P1_OBJS = $(P1_SRCS:src/%.c=$(BUILD)/%.o)

.PHONY: all test clean check-ipc

all: p2test

p2test: $(BUILD)/p2test.o $(P2_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/p2test.o: tests/p2test.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

# header dependencies
$(BUILD)/logger.o:      include/logger.h
$(BUILD)/ipc.o:         include/ipc.h
$(BUILD)/banker.o:      include/banker.h
$(BUILD)/worker.o:      include/worker.h
$(BUILD)/limits_demo.o: include/limits_demo.h
$(BUILD)/p2test.o:      $(wildcard include/*.h)

test: p2test
	sh tests/run_p2_tests.sh

check-ipc: p2test
	./p2test check

clean:
	rm -rf $(BUILD) p2test campuscompute.log
