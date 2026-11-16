/*
 * banker.h - FR5: Banker's safety algorithm
 *
 * Owner: Person 2
 *
 * Need[i][j] = Maximum[i][j] - Allocation[i][j]
 * Tie-breaking rule: processes are scanned in index order (P0, P1, ...)
 * repeatedly; the first process whose Need <= Work is chosen.
 */
#ifndef BANKER_H
#define BANKER_H

#include <stddef.h>

#define BANKER_MAX_P 10
#define BANKER_MAX_R 10

typedef struct {
    int n_proc;
    int n_res;
    int available[BANKER_MAX_R];
    int maximum[BANKER_MAX_P][BANKER_MAX_R];
    int allocation[BANKER_MAX_P][BANKER_MAX_R];
    int need[BANKER_MAX_P][BANKER_MAX_R];   /* filled by banker_validate() */
} banker_state;

/* Validate dimensions/values and compute Need.
 * Returns 0 if valid, -1 otherwise (reason written to err). */
int banker_validate(banker_state *s, char *err, size_t errlen);

/* Pure algorithm (no printing). Requires banker_validate() first.
 * Returns 1 if safe (safe_seq holds n_proc indices), 0 if unsafe.
 * finished[] (may be NULL) receives which processes could finish. */
int banker_is_safe(const banker_state *s, int safe_seq[], int finished[]);

/* Print Allocation / Maximum / Need / Available tables. */
void banker_print_state(const banker_state *s);

/* Validate, print, run the algorithm with a step-by-step trace.
 * Returns 1 safe, 0 unsafe, -1 invalid input. */
int banker_check_safety(banker_state *s);

/* Load a state from a text file (see tests/banker_*.txt for the format).
 * Returns 0 on success, -1 on error. */
int banker_load_file(const char *path, banker_state *s);

/* Built-in test cases (classic 5-process, 3-resource example). */
int banker_demo_safe(void);
int banker_demo_unsafe(void);

#endif /* BANKER_H */
