/*
 * worker.h - FR6: Worker process creation and failure detection
 *
 * Owner: Person 2
 *
 * The parent fork()s a worker child, the child runs a small job, and the
 * parent collects its status with waitpid(). Failures are SIMULATED on
 * purpose and are labelled as such; they are not real OS faults.
 */
#ifndef WORKER_H
#define WORKER_H

typedef enum {
    WORKER_NORMAL      = 0,   /* child finishes the job and exits with 0      */
    WORKER_FAIL_EXIT   = 1,   /* simulated failure: child exits with code 3   */
    WORKER_FAIL_SIGNAL = 2    /* simulated crash: child sends itself SIGTERM  */
} worker_mode;

#define WORKER_SIMULATED_EXIT_CODE 3

/* Run one worker in the given mode (0, 1 or 2).
 * Returns 0 if the worker completed normally,
 *         1 if the parent detected a failure (non-zero exit or signal),
 *        -1 on invalid mode or a system-call error. */
int worker_demo(int simulate_failure);

#endif /* WORKER_H */
