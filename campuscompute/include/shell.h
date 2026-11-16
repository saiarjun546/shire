/*
 * shell.h - Shire Shell core (read-eval loop, dispatch, external commands)
 *
 * Owner: Person 1
 *
 * The shell owns one job table (FR1) that the `jobs` and `schedule`
 * built-ins operate on. Person 2's modules are exposed as built-ins
 * (log, ipc, banker, process, limit) exactly as suggested in README.md.
 */
#ifndef SHELL_H
#define SHELL_H

#include <limits.h>
#include <stdio.h>

#include "jobs.h"

#define SHIRE_VERSION "1.0.0"

typedef struct {
    job_list jobs;                 /* job table used by jobs/schedule      */
    int      last_status;          /* status of the last command (0 = ok)  */
    int      running;              /* cleared by `exit`                    */
    int      exit_code;            /* process exit code once running == 0  */
    int      interactive;          /* stdin is a terminal                  */
    char     prev_dir[PATH_MAX];   /* for `cd -`; empty until first cd     */
} shell_state;

void shell_init(shell_state *sh);
void shell_free(shell_state *sh);

/* Print the start-up banner. */
void shell_banner(FILE *out);

/* Parse and run one command line. Returns the command's status (0 = success).
 * Never exits the process; `exit` only clears sh->running. */
int shell_execute(shell_state *sh, const char *line);

/* Run an external program found via PATH with fork/execvp/waitpid.
 * Returns its exit status, 128+signal if killed by a signal, 127 if the
 * command could not be found/executed, or 1 if fork/wait failed. */
int shell_run_external(char *const argv[]);

/* Read-eval loop on `in` until EOF or `exit`. Returns the exit code. */
int shell_run(shell_state *sh, FILE *in);

#endif /* SHELL_H */
