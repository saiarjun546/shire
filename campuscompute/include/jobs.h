/*
 * jobs.h - FR1: Job management
 *
 * Owner: Person 1
 *
 * A job_list is a growable array of job_t records. Scheduling algorithms
 * (scheduler.h) write their results back into the records, so the
 * original input (id, arrival, burst, priority) is never modified and
 * results can be cleared at any time with job_list_reset_results().
 *
 * Priority convention (used by the Priority scheduler):
 *     SMALLER NUMBER = HIGHER PRIORITY   (0 is the highest priority).
 *
 * Fields that have not been computed yet hold JOB_UNSET (-1).
 */
#ifndef JOBS_H
#define JOBS_H

#include <stddef.h>
#include <stdio.h>

#define JOBS_MAX        100        /* maximum number of jobs in one list   */
#define JOB_VALUE_MAX   1000000    /* upper bound for every numeric field  */
#define JOB_UNSET       (-1)

typedef struct {
    int id;           /* unique, 1..JOB_VALUE_MAX                           */
    int arrival;      /* arrival time, 0..JOB_VALUE_MAX                     */
    int burst;        /* ORIGINAL CPU burst, 1..JOB_VALUE_MAX (never changed) */
    int priority;     /* 0..JOB_VALUE_MAX, smaller = more urgent            */
    int remaining;    /* CPU time still needed (starts equal to burst)      */
    int completion;   /* completion time, or JOB_UNSET                      */
    int turnaround;   /* completion - arrival, or JOB_UNSET                 */
    int waiting;      /* turnaround - burst, or JOB_UNSET                   */
} job_t;

typedef struct {
    job_t  *items;    /* heap array, NULL while empty                       */
    size_t  count;
    size_t  capacity;
} job_list;

typedef enum {
    JOB_OK = 0,
    JOB_ERR_NULL,        /* NULL list pointer                               */
    JOB_ERR_ID,          /* id outside 1..JOB_VALUE_MAX                     */
    JOB_ERR_ARRIVAL,     /* arrival outside 0..JOB_VALUE_MAX                */
    JOB_ERR_BURST,       /* burst outside 1..JOB_VALUE_MAX                  */
    JOB_ERR_PRIORITY,    /* priority outside 0..JOB_VALUE_MAX               */
    JOB_ERR_DUPLICATE,   /* a job with this id already exists               */
    JOB_ERR_FULL,        /* JOBS_MAX jobs already stored                    */
    JOB_ERR_NOMEM        /* allocation failed                               */
} job_status;

/* Initialise an empty list (no allocation). */
void job_list_init(job_list *jl);

/* Free all memory and return the list to the empty state. Safe to call
 * on an already-empty list and safe to call twice. */
void job_list_free(job_list *jl);

/* Validate and append a job. The list is unchanged on any error. */
job_status job_list_add(job_list *jl, int id, int arrival, int burst, int priority);

/* Human-readable text for a job_status value. */
const char *job_status_str(job_status st);

/* Find a job by id; NULL if absent. */
const job_t *job_list_find(const job_list *jl, int id);

/* Clear scheduling results on every job:
 * remaining = burst; completion = turnaround = waiting = JOB_UNSET. */
void job_list_reset_results(job_list *jl);

/* Print the job table. Uncomputed results are shown as "-". */
void job_list_print(const job_list *jl, FILE *out);

#endif /* JOBS_H */
