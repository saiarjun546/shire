/*
 * worker.c - FR6: Worker failure detection
 *
 * The parent never assumes success: it decodes the status from waitpid()
 * with WIFEXITED/WEXITSTATUS and WIFSIGNALED/WTERMSIG and reports exactly
 * what happened.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "worker.h"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* The "job" a worker runs: sum of squares 1..n (real computation). */
static unsigned long long run_job(unsigned long n)
{
    unsigned long long total = 0;
    for (unsigned long i = 1; i <= n; i++)
        total += (unsigned long long)i * i;
    return total;
}

static void child_main(worker_mode mode)
{
    pid_t me = getpid();
    printf("  [worker PID %d, parent %d] started job: sum of squares 1..1000000\n",
           (int)me, (int)getppid());
    fflush(stdout);

    unsigned long long result = run_job(1000000UL);

    switch (mode) {
    case WORKER_NORMAL:
        printf("  [worker PID %d] job done, result = %llu, exiting with status 0\n",
               (int)me, result);
        fflush(stdout);
        _exit(0);

    case WORKER_FAIL_EXIT:
        printf("  [worker PID %d] *** SIMULATED FAILURE *** pretending the job "
               "failed, exiting with status %d\n", (int)me, WORKER_SIMULATED_EXIT_CODE);
        fflush(stdout);
        _exit(WORKER_SIMULATED_EXIT_CODE);

    case WORKER_FAIL_SIGNAL:
        printf("  [worker PID %d] *** SIMULATED CRASH *** sending SIGTERM to itself\n",
               (int)me);
        fflush(stdout);
        raise(SIGTERM);
        _exit(99);   /* not reached: SIGTERM's default action terminates */
    }
    _exit(98);
}

int worker_demo(int simulate_failure)
{
    static const char *mode_names[] = {
        "normal completion",
        "simulated failure (non-zero exit status)",
        "simulated crash (terminated by signal)"
    };

    if (simulate_failure < WORKER_NORMAL || simulate_failure > WORKER_FAIL_SIGNAL) {
        fprintf(stderr, "worker: invalid mode %d (use 0=normal, 1=fail, 2=signal)\n",
                simulate_failure);
        return -1;
    }

    printf("\n=== FR6: Worker Process - %s ===\n", mode_names[simulate_failure]);
    if (simulate_failure != WORKER_NORMAL)
        printf("  NOTE: this failure is deliberately simulated for the demo; it is "
               "not a real OS failure.\n");

    fflush(stdout);           /* flush before fork so output is not printed twice */
    double start = now_ms();
    pid_t pid = fork();
    if (pid == -1) {
        perror("worker: fork");
        return -1;
    }
    if (pid == 0)
        child_main((worker_mode)simulate_failure);   /* never returns */

    printf("  [parent PID %d] created worker PID %d, waiting with waitpid()...\n",
           (int)getpid(), (int)pid);
    fflush(stdout);

    int status;
    pid_t r;
    while ((r = waitpid(pid, &status, 0)) == -1 && errno == EINTR)
        ;
    if (r == -1) {
        perror("worker: waitpid");
        return -1;
    }
    double elapsed = now_ms() - start;

    printf("  [parent] waitpid returned PID %d after %.1f ms, raw status = 0x%04x\n",
           (int)r, elapsed, status);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) {
            printf("  [parent] RESULT: worker exited normally (WIFEXITED, status 0) "
                   "-> job SUCCEEDED\n");
            return 0;
        }
        printf("  [parent] RESULT: worker exited with NON-ZERO status %d "
               "(WIFEXITED) -> FAILURE DETECTED\n", code);
        return 1;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("  [parent] RESULT: worker was TERMINATED BY SIGNAL %d (%s) "
               "(WIFSIGNALED) -> FAILURE DETECTED\n", sig, strsignal(sig));
        return 1;
    }
    printf("  [parent] RESULT: unexpected status 0x%04x\n", status);
    return 1;
}
