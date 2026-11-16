/*
 * p2test.c - Stand-alone driver for Person 2's modules.
 *
 * Lets every mechanism be run and tested on its own before it is wired
 * into Person 1's shell (FR8: "each implemented feature must be
 * independently testable"). Exit status: 0 = expected result, 1 = not.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "banker.h"
#include "ipc.h"
#include "limits_demo.h"
#include "logger.h"
#include "worker.h"

static void usage(const char *prog)
{
    printf("Usage: %s <command> [args]\n\n", prog);
    printf("Commands (Person 2 modules):\n");
    printf("  logger [P C M CAP]     FR3 producer-consumer logger\n");
    printf("                         P producers, C consumers, M msgs each, CAP queue size\n");
    printf("                         (default 3 2 5 4), log -> %s\n", LOGGER_DEFAULT_FILE);
    printf("  ipc [N]                FR4 shared memory + named semaphores, N messages (default 5)\n");
    printf("  banker safe            FR5 built-in safe test case\n");
    printf("  banker unsafe          FR5 built-in unsafe test case\n");
    printf("  banker file <path>     FR5 load a state from a file (see tests/banker_*.txt)\n");
    printf("  worker normal          FR6 child completes normally\n");
    printf("  worker fail            FR6 simulated failure: non-zero exit status\n");
    printf("  worker signal          FR6 simulated crash: child terminated by a signal\n");
    printf("  limits fsize [BYTES]   FR7 RLIMIT_FSIZE demo (default %ld bytes)\n", LIMITS_FSIZE_DEFAULT);
    printf("  limits cpu [SECS]      FR7 RLIMIT_CPU demo (default %d s)\n", LIMITS_CPU_DEFAULT);
    printf("  check                  list leftover IPC objects created by this program\n");
    printf("  all                    run every demo above in sequence with a summary\n");
    printf("  help                   show this message\n");
}

/* Strict integer parsing: whole string must be a number in [lo, hi]. */
static int parse_int(const char *s, long lo, long hi, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < lo || v > hi) {
        fprintf(stderr, "error: '%s' is not an integer in range %ld..%ld\n", s, lo, hi);
        return -1;
    }
    *out = v;
    return 0;
}

/* Lists /dev/shm entries whose names start with our prefixes
 * (named semaphores appear there as "sem.<name>"). */
static int ipc_check(void)
{
    DIR *d = opendir("/dev/shm");
    if (d == NULL) {
        perror("/dev/shm");
        return -1;
    }
    int found = 0;
    struct dirent *e;
    printf("Scanning /dev/shm for CampusCompute IPC objects (cc_shm_*, sem.cc_sem_*)...\n");
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "cc_shm_", 7) == 0 || strncmp(e->d_name, "sem.cc_sem_", 11) == 0) {
            printf("  LEFTOVER: /dev/shm/%s\n", e->d_name);
            found++;
        }
    }
    closedir(d);
    if (found == 0)
        printf("  none found - all program-created shared memory and semaphores were released\n");
    return found == 0 ? 0 : -1;
}

static int run_all(void)
{
    struct { const char *name; int ok; } r[10];
    int n = 0;

    r[n].name = "FR3 logger";            r[n++].ok = logger_demo() == 0;
    r[n].name = "FR4 shared-memory IPC"; r[n++].ok = ipc_demo() == 0;
    r[n].name = "FR5 banker safe";       r[n++].ok = banker_demo_safe() == 1;
    r[n].name = "FR5 banker unsafe";     r[n++].ok = banker_demo_unsafe() == 0;
    r[n].name = "FR6 worker normal";     r[n++].ok = worker_demo(WORKER_NORMAL) == 0;
    r[n].name = "FR6 worker fail (sim)"; r[n++].ok = worker_demo(WORKER_FAIL_EXIT) == 1;
    r[n].name = "FR6 worker signal (sim)"; r[n++].ok = worker_demo(WORKER_FAIL_SIGNAL) == 1;
    r[n].name = "FR7 RLIMIT_FSIZE";      r[n++].ok = limits_demo() == 0;
    printf("\n");
    r[n].name = "Cleanup check";         r[n++].ok = ipc_check() == 0;

    int pass = 0;
    printf("\n=========== Person 2 integrated summary ===========\n");
    for (int i = 0; i < n; i++) {
        printf("  %-26s %s\n", r[i].name, r[i].ok ? "PASS (expected result)" : "FAIL");
        pass += r[i].ok;
    }
    printf("  %d/%d checks gave the expected result\n", pass, n);
    printf("===================================================\n");
    return pass == n ? 0 : 1;
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);   /* line-buffered even when piped to a file */

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char *cmd = argv[1];
    long a, b, c, d;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (strcmp(cmd, "logger") == 0) {
        if (argc == 2)
            return logger_demo() == 0 ? 0 : 1;
        if (argc != 6) {
            fprintf(stderr, "usage: %s logger [producers consumers messages capacity]\n", argv[0]);
            return 1;
        }
        if (parse_int(argv[2], 1, LOGGER_MAX_THREADS, &a) || parse_int(argv[3], 1, LOGGER_MAX_THREADS, &b) ||
            parse_int(argv[4], 1, LOGGER_MAX_MSGS, &c) || parse_int(argv[5], 1, LOGGER_MAX_CAPACITY, &d))
            return 1;
        return logger_run((int)a, (int)b, (int)c, (int)d, LOGGER_DEFAULT_FILE) == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "ipc") == 0) {
        if (argc == 2)
            return ipc_demo() == 0 ? 0 : 1;
        if (argc != 3 || parse_int(argv[2], 1, IPC_MAX_MESSAGES, &a)) {
            fprintf(stderr, "usage: %s ipc [messages 1-%d]\n", argv[0], IPC_MAX_MESSAGES);
            return 1;
        }
        return ipc_run((int)a) == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "banker") == 0) {
        if (argc == 3 && strcmp(argv[2], "safe") == 0)
            return banker_demo_safe() == 1 ? 0 : 1;
        if (argc == 3 && strcmp(argv[2], "unsafe") == 0)
            return banker_demo_unsafe() == 0 ? 0 : 1;
        if (argc == 4 && strcmp(argv[2], "file") == 0) {
            banker_state s;
            if (banker_load_file(argv[3], &s) != 0)
                return 1;
            printf("\n=== FR5: Banker's Algorithm - state from %s ===\n", argv[3]);
            int r = banker_check_safety(&s);
            return r < 0 ? 1 : 0;   /* safe or unsafe are both valid answers */
        }
        fprintf(stderr, "usage: %s banker safe | banker unsafe | banker file <path>\n", argv[0]);
        return 1;
    }
    if (strcmp(cmd, "worker") == 0) {
        if (argc == 3 && strcmp(argv[2], "normal") == 0)
            return worker_demo(WORKER_NORMAL) == 0 ? 0 : 1;
        if (argc == 3 && strcmp(argv[2], "fail") == 0)
            return worker_demo(WORKER_FAIL_EXIT) == 1 ? 0 : 1;
        if (argc == 3 && strcmp(argv[2], "signal") == 0)
            return worker_demo(WORKER_FAIL_SIGNAL) == 1 ? 0 : 1;
        fprintf(stderr, "usage: %s worker normal | worker fail | worker signal\n", argv[0]);
        return 1;
    }
    if (strcmp(cmd, "limits") == 0) {
        if (argc >= 3 && argc <= 4 && strcmp(argv[2], "fsize") == 0) {
            a = LIMITS_FSIZE_DEFAULT;
            if (argc == 4 && parse_int(argv[3], 1, 1024L * 1024L, &a))
                return 1;
            return limits_fsize_demo(a) == 0 ? 0 : 1;
        }
        if (argc >= 3 && argc <= 4 && strcmp(argv[2], "cpu") == 0) {
            a = LIMITS_CPU_DEFAULT;
            if (argc == 4 && parse_int(argv[3], 1, 10, &a))
                return 1;
            return limits_cpu_demo((int)a) == 0 ? 0 : 1;
        }
        fprintf(stderr, "usage: %s limits fsize [bytes] | limits cpu [seconds]\n", argv[0]);
        return 1;
    }
    if (strcmp(cmd, "check") == 0)
        return ipc_check() == 0 ? 0 : 1;
    if (strcmp(cmd, "all") == 0)
        return run_all();

    fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
    usage(argv[0]);
    return 1;
}
